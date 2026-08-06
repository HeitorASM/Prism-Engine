#pragma once

// ============================================================================
// Log.h
// Sistema de logging minimo, dependencia zero (so <iostream> + <functional>).
//
// Por que nao usar spdlog logo de cara?
// Porque a fundacao do projeto ainda esta sendo assentada, e cada dependencia
// externa nova e mais uma coisa pra configurar no vcxproj/CMake. Este logger
// usa a MESMA interface que um logger real teria (PRISM_CORE_INFO, etc),
// entao trocar a implementacao por spdlog no futuro nao exige mudar nenhuma
// linha de codigo que CHAMA o log - so a implementacao interna deste arquivo.
//
// Sink opcional: alem de escrever em stdout/stderr como sempre fez, o Log
// agora tambem pode encaminhar cada mensagem para um callback registrado via
// Log::SetSink() - e assim que o Console panel do editor (ver
// PrismEditor/Panels/ConsolePanel.h) recebe as mensagens sem que o Log
// precise saber que um editor/ImGui existe. Nenhum sink registrado = engine
// standalone (fora do editor) se comporta exatamente como antes.
// ============================================================================

#include <iostream>
#include <string>
#include <sstream>
#include <functional>

namespace Prism {

    enum class LogLevel {
        Trace, Info, Warn, Error, Critical
    };

    class Log {
    public:
        using Sink = std::function<void(LogLevel, const std::string& scope, const std::string& message)>;

        // Registra o callback que recebe uma copia de toda mensagem logada
        // dai em diante (nao ha buffer retroativo - mensagens de antes de
        // SetSink() nao sao reenviadas). Passar nullptr remove o sink.
        static void SetSink(Sink sink) { s_Sink = std::move(sink); }

        // Log do "motor" (engine) - prefixo [PRISM]
        template<typename... Args>
        static void CoreLog(LogLevel level, Args&&... args) {
            PrintPrefixed("[PRISM] ", "PRISM", level, std::forward<Args>(args)...);
        }

        // Log da aplicacao/jogo/editor - prefixo [APP]
        template<typename... Args>
        static void AppLog(LogLevel level, Args&&... args) {
            PrintPrefixed("[APP]   ", "APP", level, std::forward<Args>(args)...);
        }

    private:
        template<typename... Args>
        static void PrintPrefixed(const char* consolePrefix, const char* scopeName, LogLevel level, Args&&... args) {
            std::ostream& out = (level == LogLevel::Error || level == LogLevel::Critical) ? std::cerr : std::cout;
            out << consolePrefix << LevelTag(level) << " ";
            (out << ... << args);
            out << "\n";

            if (s_Sink) {
                // Monta a mesma mensagem numa string separada para o sink -
                // duplicar o "<< ... << args" e mais simples e barato do
                // que tentar capturar o que foi escrito no ostream acima
                // (que pode ser cout OU cerr dependendo do nivel).
                std::ostringstream oss;
                (oss << ... << args);
                s_Sink(level, scopeName, oss.str());
            }
        }

        static const char* LevelTag(LogLevel level) {
            switch (level) {
                case LogLevel::Trace:    return "[TRACE]";
                case LogLevel::Info:     return "[INFO] ";
                case LogLevel::Warn:     return "[WARN] ";
                case LogLevel::Error:    return "[ERROR]";
                case LogLevel::Critical: return "[CRIT] ";
            }
            return "[????]";
        }

        inline static Sink s_Sink;
    };

}

// Macros de conveniencia - use estas no dia a dia, nao chame Log::CoreLog direto.
#define PRISM_CORE_TRACE(...)    ::Prism::Log::CoreLog(::Prism::LogLevel::Trace, __VA_ARGS__)
#define PRISM_CORE_INFO(...)     ::Prism::Log::CoreLog(::Prism::LogLevel::Info, __VA_ARGS__)
#define PRISM_CORE_WARN(...)     ::Prism::Log::CoreLog(::Prism::LogLevel::Warn, __VA_ARGS__)
#define PRISM_CORE_ERROR(...)    ::Prism::Log::CoreLog(::Prism::LogLevel::Error, __VA_ARGS__)
#define PRISM_CORE_CRITICAL(...) ::Prism::Log::CoreLog(::Prism::LogLevel::Critical, __VA_ARGS__)

#define PRISM_TRACE(...)         ::Prism::Log::AppLog(::Prism::LogLevel::Trace, __VA_ARGS__)
#define PRISM_INFO(...)          ::Prism::Log::AppLog(::Prism::LogLevel::Info, __VA_ARGS__)
#define PRISM_WARN(...)          ::Prism::Log::AppLog(::Prism::LogLevel::Warn, __VA_ARGS__)
#define PRISM_ERROR(...)         ::Prism::Log::AppLog(::Prism::LogLevel::Error, __VA_ARGS__)
#define PRISM_CRITICAL(...)      ::Prism::Log::AppLog(::Prism::LogLevel::Critical, __VA_ARGS__)
