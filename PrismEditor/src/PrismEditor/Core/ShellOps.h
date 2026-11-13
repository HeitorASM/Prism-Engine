#pragma once

// ============================================================================
// ShellOps.h
// Pequenas acoes do sistema operacional usadas pelos paineis do editor:
// mostrar um arquivo/pasta no gerenciador de arquivos e abrir um terminal numa pasta.
// ============================================================================

#include <filesystem>

namespace PrismEditor::ShellOps {

    // Pasta: abre a pasta. Arquivo: abre a pasta do arquivo (Windows/macOS: com o arquivo selecionado).
    void RevealInFileExplorer(const std::filesystem::path& path);

    // Abre 'path' com o programa padrao do sistema (ex: uma .sln abre no Visual Studio).
    // false se o caminho tem caracteres que nao da para passar ao shell com seguranca.
    bool OpenWithDefaultApp(const std::filesystem::path& path);

    // Abre um terminal com 'directory' como pasta atual.
    void OpenTerminalAt(const std::filesystem::path& directory);

}
