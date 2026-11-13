#include "Paths.h"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <string>
#endif

namespace Prism {

    std::filesystem::path GetExecutablePath() {
#if defined(_WIN32)
        // O buffer cresce ate o limite de caminho longo; MAX_PATH truncaria.
        std::wstring buffer(512, L'\0');
        for (;;) {
            DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
            if (length == 0)
                return {};
            if (length < buffer.size()) {
                buffer.resize(length);
                return std::filesystem::path(buffer);
            }
            if (buffer.size() >= 32768)
                return {};
            buffer.resize(buffer.size() * 2);
        }
#elif defined(__linux__)
        std::error_code ec;
        std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::path() : exe;
#else
        return {};
#endif
    }

    std::filesystem::path GetExecutableDirectory() {
        return GetExecutablePath().parent_path();
    }

}
