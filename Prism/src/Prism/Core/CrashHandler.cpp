#include "CrashHandler.h"
#include "Log.h"
#include "LogBuffer.h"
#include <PrismVersion.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#ifdef PRISM_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <DbgHelp.h> // MiniDumpWriteDump, SymFromAddr (stack trace)
    #pragma comment(lib, "Dbghelp.lib")
#else
    #include <signal.h>
    #include <csignal>
    #include <unistd.h>
    #if __has_include(<execinfo.h>)
        #include <execinfo.h>
        #define PRISM_HAS_EXECINFO 1
    #endif
#endif

namespace Prism {

    // ========================================================================
    // Parte portavel: pasta de relatorio + crash.log.
    // Tudo aqui e MELHOR ESFORCO: roda dentro de um processo que ja esta
    // quebrado, entao nenhuma falha daqui pode impedir a mensagem de erro nem
    // causar um segundo crash (por isso os try/catch(...) e error_code).
    // ========================================================================
    namespace {

        std::vector<std::pair<std::string, std::string>>& ContextEntries() {
            static std::vector<std::pair<std::string, std::string>> entries;
            return entries;
        }

        bool s_Reporting = false; // um crash DENTRO do handler nao pode reentrar nele

        std::string Timestamp(const char* format) {
            std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local{};
        #ifdef PRISM_PLATFORM_WINDOWS
            localtime_s(&local, &now);
        #else
            localtime_r(&now, &local);
        #endif
            char buffer[64] = {};
            std::strftime(buffer, sizeof(buffer), format, &local);
            return buffer;
        }

        // CrashReports/Crash_AAAA_MM_DD_HH_MM_SS. "" se nao conseguiu criar.
        std::string CreateReportDirectory() {
            try {
                namespace fs = std::filesystem;
                std::error_code error;
                fs::path directory = fs::path("CrashReports") / ("Crash_" + Timestamp("%Y_%m_%d_%H_%M_%S"));
                fs::create_directories(directory, error);
                if (error)
                    return {};
                return directory.string();
            } catch (...) {
                return {};
            }
        }

        const char* LevelName(LogLevel level) {
            switch (level) {
                case LogLevel::Trace:    return "TRACE";
                case LogLevel::Info:     return "INFO";
                case LogLevel::Warn:     return "WARN";
                case LogLevel::Error:    return "ERROR";
                case LogLevel::Critical: return "CRIT";
            }
            return "?";
        }

        void WriteReportFile(const std::string& directory, const std::string& summary,
                             const std::string& details, const std::string& stack) {
            try {
                std::ofstream out(std::filesystem::path(directory) / "crash.log", std::ios::trunc);
                if (!out)
                    return;

                out << "=== Prism Engine - relatorio de crash ===\n";
                out << "Resumo:     " << summary << "\n";
                if (!details.empty())
                    out << "Detalhe:    " << details << "\n";
                out << "Data:       " << Timestamp("%Y-%m-%d %H:%M:%S") << "\n";
                out << "Engine:     " << Version::Full() << "\n";
            #ifdef PRISM_DEBUG
                out << "Build:      Debug\n";
            #else
                out << "Build:      Release\n";
            #endif
            #if defined(_WIN32)
                out << "Plataforma: Windows\n";
            #elif defined(__APPLE__)
                out << "Plataforma: macOS\n";
            #elif defined(__linux__)
                out << "Plataforma: Linux\n";
            #else
                out << "Plataforma: desconhecida\n";
            #endif
                for (const auto& [key, value] : ContextEntries())
                    out << std::left << std::setw(12) << (key + ":") << value << "\n";

                out << "\n--- Stack trace ---\n" << (stack.empty() ? "(indisponivel)\n" : stack);

                out << "\n--- Ultimas mensagens do log ---\n";
                const auto& entries = LogBuffer::GetEntries();
                const size_t first = entries.size() > 80 ? entries.size() - 80 : 0;
                for (size_t i = first; i < entries.size(); i++)
                    out << "[" << LevelName(entries[i].Level) << "] " << entries[i].Scope << ": " << entries[i].Message << "\n";
            } catch (...) {
                // sem relatorio de texto; o resto do tratamento do crash segue
            }
        }

        std::string DescribeAddress(const void* address) {
            std::ostringstream text;
            text << "0x" << std::hex << reinterpret_cast<uintptr_t>(address);
            return text.str();
        }

    } // namespace

    void SetCrashContext(const std::string& key, const std::string& value) {
        for (auto& entry : ContextEntries()) {
            if (entry.first == key) {
                entry.second = value;
                return;
            }
        }
        ContextEntries().emplace_back(key, value);
    }

    std::string WriteCrashReport(const std::string& summary, const std::string& details) {
        const std::string directory = CreateReportDirectory();
        if (!directory.empty())
            WriteReportFile(directory, summary, details, "(nao capturado: a excecao ja foi tratada em main)\n");
        return directory;
    }

#ifdef PRISM_PLATFORM_WINDOWS

    // Traduz o codigo de excecao SEH para uma frase curta e legivel -
    // cobre os casos mais comuns de crash real de engine (a lista
    // completa de EXCEPTION_* do Windows e grande, mas exotica demais
    // para valer a pena cobrir tudo aqui); qualquer codigo nao listado
    // cai no "default" generico.
    static const char* DescribeExceptionCode(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:      return "acesso invalido a memoria (ponteiro nulo ou corrompido)";
            case EXCEPTION_STACK_OVERFLOW:        return "estouro de pilha (recursao infinita?)";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "divisao por zero";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "acesso fora dos limites de um array";
            case EXCEPTION_BREAKPOINT:            return "um assert interno falhou (PRISM_ASSERT ou JPH_ASSERT)";
            case EXCEPTION_ILLEGAL_INSTRUCTION:   return "instrucao de CPU invalida";
            default:                              return "erro interno inesperado";
        }
    }

    // Stack trace do ponto do crash. O filtro SEH roda SOBRE a pilha de quem
    // falhou, entao os primeiros quadros sao do proprio tratamento de excecao e
    // os seguintes sao o codigo que quebrou. O minidump continua sendo a fonte
    // exata (abra o .dmp no Visual Studio).
    static std::string CaptureStackTrace() {
        void* frames[64];
        const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);

        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(process, nullptr, TRUE);

        std::vector<char> symbolBuffer(sizeof(SYMBOL_INFO) + 256);
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer.data());
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        std::ostringstream text;
        for (USHORT i = 0; i < count; i++) {
            const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
            DWORD64 displacement = 0;
            text << "#" << std::setw(2) << std::setfill('0') << i << std::setfill(' ') << " " << DescribeAddress(frames[i]) << " ";
            if (SymFromAddr(process, address, &displacement, symbol))
                text << symbol->Name << "+0x" << std::hex << displacement << std::dec;
            else
                text << "???";

            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
                text << " (" << line.FileName << ":" << line.LineNumber << ")";
            text << "\n";
        }
        return text.str();
    }

    // Tenta escrever um arquivo .dmp (minidump) - MELHOR ESFORCO: se isto falhar
    // por qualquer motivo (disco cheio, sem permissao de escrita, DbgHelp.dll
    // ausente), seguimos sem o dump; a FALHA DE GERAR O DUMP nunca pode impedir a
    // mensagem de erro (ou pior, causar um segundo crash dentro do handler de
    // crash). O dump permite investigar o crash num debugger mesmo sem reproduzir
    // o bug de novo.
    static void TryWriteMiniDump(EXCEPTION_POINTERS* exceptionInfo, const std::string& path) {
        HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = exceptionInfo;
        dumpInfo.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                           MiniDumpNormal, exceptionInfo ? &dumpInfo : nullptr, nullptr, nullptr);

        CloseHandle(file);
    }

    // O handler em si - chamado pelo Windows quando uma excecao SEH sobe
    // sem ninguem ter capturado (nenhum __try/__except, nenhum try/catch
    // de C++ cobre excecoes SEH puras de qualquer forma - ver comentario
    // em CrashHandler.h). Retornar EXCEPTION_EXECUTE_HANDLER diz ao
    // Windows "eu tratei isso, pode encerrar o processo normalmente" -
    // SEM isso, o Windows mostraria sua PROPRIA caixa de dialogo de crash
    // por cima ou depois da nossa, o que seria confuso/redundante.
    static LONG WINAPI GlobalExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
        if (s_Reporting)
            return EXCEPTION_EXECUTE_HANDLER; // crash dentro do proprio handler: so encerra

        s_Reporting = true;

        DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
            ? exceptionInfo->ExceptionRecord->ExceptionCode
            : 0;

        std::ostringstream summary;
        summary << DescribeExceptionCode(code) << " (codigo 0x" << std::hex << code << ")";

        std::string details;
        if (exceptionInfo && exceptionInfo->ExceptionRecord)
            details = "endereco da excecao: " + DescribeAddress(exceptionInfo->ExceptionRecord->ExceptionAddress);

        // Loga antes do MessageBox (bloqueante) - se o log em si estiver
        // corrompido/impossivel (cenario extremo de memoria corrompida),
        // pelo menos tentamos; PRISM_CORE_ERROR nunca deveria lancar.
        PRISM_CORE_ERROR("CRASH FATAL: ", summary.str(), ". Encerrando.");

        // Em estouro de pilha sobra pouco espaco aqui: o relatorio pode sair
        // incompleto, mas o minidump (abaixo) e escrito mesmo assim.
        const std::string directory = CreateReportDirectory();
        if (!directory.empty())
            WriteReportFile(directory, summary.str(), details, CaptureStackTrace());

        TryWriteMiniDump(exceptionInfo, directory.empty() ? std::string("PrismEngine_crash.dmp") : directory + "\\crash.dmp");

        std::string message =
            "Ops! Parece que o PrismEngine sofreu um erro inesperado e "
            "precisa ser encerrado.\n\n";
        if (!directory.empty())
            message += "Um relatorio (crash.log) e um diagnostico (crash.dmp) foram salvos em:\n" + directory + "\n\n";
        else
            message += "Um arquivo de diagnostico (PrismEngine_crash.dmp) foi salvo na pasta do executavel, se possivel.\n\n";
        message += "Desculpe pelo transtorno - qualquer trabalho nao salvo nesta sessao sera perdido.";

        // MB_ICONERROR (X vermelho) + MB_OK (um unico botao de fechar) +
        // MB_TOPMOST (aparece na frente de qualquer janela da engine, mesmo em
        // fullscreen/Play Window) + MB_SYSTEMMODAL (bloqueia TODAS as janelas do
        // processo - a engine pode ter varias janelas GLFW, ver PlayWindow).
        MessageBoxA(nullptr, message.c_str(), "PrismEngine - Erro Fatal",
                    MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SYSTEMMODAL);

        return EXCEPTION_EXECUTE_HANDLER;
    }

    void InstallCrashHandler() {
        SetUnhandledExceptionFilter(GlobalExceptionFilter);
    }

#else // !PRISM_PLATFORM_WINDOWS

    namespace {

        const char* DescribeSignal(int signalNumber) {
            switch (signalNumber) {
                case SIGSEGV: return "acesso invalido a memoria (SIGSEGV)";
                case SIGABRT: return "abort() / assert interno (SIGABRT)";
                case SIGFPE:  return "erro aritmetico, ex: divisao por zero (SIGFPE)";
                case SIGILL:  return "instrucao ilegal ou assert interno (SIGILL)";
            #ifdef SIGBUS
                case SIGBUS:  return "acesso desalinhado ou invalido (SIGBUS)";
            #endif
                default:      return "sinal fatal inesperado";
            }
        }

        std::string CaptureStackTrace() {
        #ifdef PRISM_HAS_EXECINFO
            void* frames[64];
            const int count = backtrace(frames, 64);
            char** symbols = backtrace_symbols(frames, count);
            std::ostringstream text;
            for (int i = 0; i < count; i++)
                text << "#" << std::setw(2) << std::setfill('0') << i << std::setfill(' ') << " " << (symbols ? symbols[i] : DescribeAddress(frames[i])) << "\n";
            std::free(symbols);
            return text.str();
        #else
            return "(backtrace() indisponivel nesta plataforma)\n";
        #endif
        }

        // NAO e async-signal-safe (aloca memoria, usa iostream) - aceitavel porque o
        // processo vai morrer de qualquer forma e o s_Reporting impede reentrada.
        void FatalSignalHandler(int signalNumber) {
            if (s_Reporting)
                _exit(128 + signalNumber);
            s_Reporting = true;

            const std::string summary = DescribeSignal(signalNumber);
            PRISM_CORE_ERROR("CRASH FATAL: ", summary, ". Encerrando.");

            const std::string directory = CreateReportDirectory();
            if (!directory.empty()) {
                WriteReportFile(directory, summary, "", CaptureStackTrace());
                PRISM_CORE_ERROR("Relatorio de crash salvo em: ", directory);
            }

            // Devolve ao comportamento padrao (core dump, codigo de saida correto).
            std::signal(signalNumber, SIG_DFL);
            std::raise(signalNumber);
        }

    } // namespace

    void InstallCrashHandler() {
        // Pilha alternativa: sem ela, um estouro de pilha (SIGSEGV) nao consegue nem
        // rodar o handler.
        static char alternateStack[64 * 1024];
        stack_t stack{};
        stack.ss_sp = alternateStack;
        stack.ss_size = sizeof(alternateStack);
        sigaltstack(&stack, nullptr);

        struct sigaction action{};
        action.sa_handler = FatalSignalHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_ONSTACK;

        sigaction(SIGSEGV, &action, nullptr);
        sigaction(SIGABRT, &action, nullptr);
        sigaction(SIGFPE, &action, nullptr);
        sigaction(SIGILL, &action, nullptr);
    #ifdef SIGBUS
        sigaction(SIGBUS, &action, nullptr);
    #endif
    }

#endif

}
