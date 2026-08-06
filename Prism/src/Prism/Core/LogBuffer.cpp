#include "LogBuffer.h"

namespace Prism {

    void LogBuffer::Install(size_t capacity) {
        s_Capacity = capacity;
        Log::SetSink(&LogBuffer::OnLogMessage);
    }

    void LogBuffer::OnLogMessage(LogLevel level, const std::string& scope, const std::string& message) {
        s_Entries.push_back(LogEntry{ level, scope, message });

        // Buffer circular simples: descarta a entrada mais antiga quando
        // estoura a capacidade. pop_front em deque e O(1) (diferente de
        // vector), por isso deque em vez de vector aqui.
        if (s_Entries.size() > s_Capacity)
            s_Entries.pop_front();
    }

}
