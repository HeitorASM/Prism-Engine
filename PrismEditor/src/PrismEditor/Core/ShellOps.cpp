#include "ShellOps.h"

#include <Prism/Core/Log.h>

#include <cstdlib>
#include <string>

namespace PrismEditor::ShellOps {

    namespace {
        // Aspas, crase, $, & etc. no caminho quebrariam (ou injetariam) o comando do shell.
        bool SafeForShell(const std::string& s) {
            return s.find_first_of("\"'`$&|;<>%^\n\r") == std::string::npos;
        }
    }

    void RevealInFileExplorer(const std::filesystem::path& path) {
        const std::string p = path.string();
        if (!SafeForShell(p)) {
            PRISM_CORE_WARN("Nao foi possivel abrir no explorador: o caminho tem caracteres especiais.");
            return;
        }
        std::error_code ec;
        const bool isDirectory = std::filesystem::is_directory(path, ec);
#if defined(_WIN32)
        std::string command = isDirectory ? "start \"\" explorer \"" + p + "\"" : "explorer /select,\"" + p + "\"";
#elif defined(__APPLE__)
        std::string command = isDirectory ? "open \"" + p + "\"" : "open -R \"" + p + "\"";
#else
        std::string command = "xdg-open \"" + (isDirectory ? p : path.parent_path().string()) + "\" >/dev/null 2>&1 &";
#endif
        std::system(command.c_str());
    }

    void OpenTerminalAt(const std::filesystem::path& directory) {
        const std::string p = directory.string();
        if (!SafeForShell(p)) {
            PRISM_CORE_WARN("Nao foi possivel abrir o terminal: o caminho tem caracteres especiais.");
            return;
        }
#if defined(_WIN32)
        std::string command = "start \"\" /D \"" + p + "\" cmd";
#elif defined(__APPLE__)
        std::string command = "open -a Terminal \"" + p + "\"";
#else
        std::string command = "(cd \"" + p + "\" && (x-terminal-emulator || gnome-terminal || konsole || xterm) >/dev/null 2>&1 &) &";
#endif
        std::system(command.c_str());
    }

}
