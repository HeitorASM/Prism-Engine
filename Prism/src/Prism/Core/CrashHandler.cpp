#include "CrashHandler.h"
#include "Log.h"

#ifdef PRISM_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <DbgHelp.h> // MiniDumpWriteDump (crash dump opcional, ver comentario abaixo)
    #pragma comment(lib, "Dbghelp.lib")
#endif

namespace Prism {

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

    // Tenta escrever um arquivo .dmp (minidump) ao lado do executavel -
    // MELHOR ESFORCO: se isto falhar por qualquer motivo (disco cheio,
    // sem permissao de escrita, DbgHelp.dll ausente), simplesmente
    // seguimos sem o dump, nunca deixamos a FALHA DE GERAR O DUMP
    // impedir a mensagem de erro (ou pior, causar um segundo crash
      // dentro do handler de crash). O dump, quando presente, permite
    // investigar o crash depois num debugger mesmo sem reproduzir o bug
    // de novo - muito mais util para reportar um bug real do que so a
    // mensagem na tela.
    static void TryWriteMiniDump(EXCEPTION_POINTERS* exceptionInfo) {
        HANDLE file = CreateFileA("PrismEngine_crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    // ("PrismEditor.exe parou de funcionar") por cima ou depois da nossa,
    // o que seria confuso/redundante.
    static LONG WINAPI GlobalExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
        DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
            ? exceptionInfo->ExceptionRecord->ExceptionCode
            : 0;

        // Loga antes do MessageBox (bloqueante) - se o log em si estiver
        // corrompido/impossivel (cenario extremo de memoria corrompida),
        // pelo menos tentamos; PRISM_CORE_ERROR nunca deveria lancar.
        PRISM_CORE_ERROR("CRASH FATAL: ", DescribeExceptionCode(code), " (codigo 0x", code, "). Encerrando.");

        TryWriteMiniDump(exceptionInfo);

        // MB_ICONERROR (X vermelho) + MB_OK (um unico botao de fechar,
        // ver pedido original) + MB_TOPMOST (garante que a caixa aparece
        // na frente de qualquer janela da engine, mesmo em fullscreen/
        // Play Window) + MB_SYSTEMMODAL (bloqueia TODAS as janelas do
        // processo, nao so a principal - relevante porque a engine pode
        // ter varias janelas GLFW abertas ao mesmo tempo, ver PlayWindow).
        MessageBoxA(
            nullptr,
            "Ops! Parece que o PrismEngine sofreu um erro inesperado e "
            "precisa ser encerrado.\n\n"
            "Um arquivo de diagnostico (PrismEngine_crash.dmp) foi salvo "
            "na pasta do executavel, se possivel.\n\n"
            "Desculpe pelo transtorno - qualquer trabalho nao salvo nesta "
            "sessao sera perdido.",
            "PrismEngine - Erro Fatal",
            MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SYSTEMMODAL);

        return EXCEPTION_EXECUTE_HANDLER;
    }

    void InstallCrashHandler() {
        SetUnhandledExceptionFilter(GlobalExceptionFilter);
    }

#else // !PRISM_PLATFORM_WINDOWS

    // Ver comentario "WINDOWS-ONLY por enquanto" em CrashHandler.h - em
    // qualquer outra plataforma, isto e um no-op silencioso (nao um
    // erro): a engine continua funcionando identica a antes deste
    // arquivo existir, so sem a tela de erro amigavel.
    void InstallCrashHandler() {}

#endif

}
