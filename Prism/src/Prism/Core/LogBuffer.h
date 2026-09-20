#pragma once

// ============================================================================
// LogBuffer.h
// Guarda uma copia das ultimas N mensagens logadas (ver Log::SetSink em
// Log.h), para que qualquer painel de UI (hoje, o Console do editor) possa
// exibir o historico de log sem precisar redirecionar stdout/stderr.
//
// Fica no CORE da engine (nao no Editor) de proposito: a engine pode ser
// usada fora do editor e ainda assim fazer sentido inspecionar o log em
// runtime (por exemplo, um console de debug in-game). O ConsolePanel do
// editor so consome isto, nao e dono da logica de armazenamento.
// ============================================================================

#include "Log.h"
#include <deque>
#include <string>

// NOTA: sem mutex de proposito. A engine hoje e single-threaded (todo log
// acontece na thread principal, dentro do loop de Application::Run) - se um
// dia sistemas rodarem em threads separadas (ex: import de assets em
// background), este buffer precisa ganhar um mutex ANTES de ser usado de
// mais de uma thread. Nao adicionamos isso preventivamente para nao sugerir
// uma seguranca que o resto da engine ainda nao garante.

namespace Prism {

    struct LogEntry {
        LogLevel Level;
        std::string Scope;   // "PRISM" ou "APP" - ver Log::CoreLog/AppLog
        std::string Message;
    };

    class LogBuffer {
    public:
        // Chamado uma vez (ver Application::Application) para conectar este
        // buffer como o sink do Log - dai em diante toda PRISM_INFO/
        // PRISM_CORE_ERROR/etc tambem cai aqui.
        static void Install(size_t capacity = 2000);

        static const std::deque<LogEntry>& GetEntries() { return s_Entries; }
        static void Clear() { s_Entries.clear(); }

    private:
        static void OnLogMessage(LogLevel level, const std::string& scope, const std::string& message);

    private:
        inline static std::deque<LogEntry> s_Entries;
        inline static size_t s_Capacity = 2000;
    };

}
