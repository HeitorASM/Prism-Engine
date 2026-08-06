#pragma once

// ============================================================================
// Log.h
// Sistema de logging minimo, dependencia zero (so <iostream>).
//
// Por que nao usar spdlog logo de cara?
// Porque a fundacao do projeto ainda esta sendo assentada, e cada dependencia
// externa nova e mais uma coisa pra configurar no vcxproj/CMake. Este logger
// usa a MESMA interface que um logger real teria (PRISM_CORE_INFO, etc),
// entao trocar a implementacao por spdlog no futuro nao exige mudar nenhuma
// linha de codigo que CHAMA o log - so a implementacao interna deste arquivo.
// ============================================================================

#include <iostream>
#include <string>

namespace Prism {

    enum class LogLevel {
        Trace, Info, Warn, Error, Critical
    };

    class Log {
    public:
        // Log do "motor" (engine) - prefixo [PRISM]
        template<typename... Args>
        static void CoreLog(LogLevel level, Args&&... args) {
            PrintPrefixed("[PRISM] ", level, std::forward<Args>(args)...);
        }

        // Log da aplicacao/jogo/editor - prefixo [APP]
        template<typename... Args>
        static void AppLog(LogLevel level, Args&&... args) {
            PrintPrefixed("[APP]   ", level, std::forward<Args>(args)...);
        }

    private:
        template<typename... Args>
        static void PrintPrefixed(const char* scope, LogLevel level, Args&&... args) {
            std::ostream& out = (level == LogLevel::Error || level == LogLevel::Critical) ? std::cerr : std::cout;
            out << scope << LevelTag(level) << " ";
            (out << ... << args);
            out << "\n";
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
