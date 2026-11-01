#pragma once

// ============================================================================
// ExtensionTemplate.h
// Gera o esqueleto de uma extensao nova (extension.ini + CMakeLists.txt + <Nome>.cpp) para o
// botao "Nova extensao" da aba Extensoes do editor. So usa a std: testavel sem engine.
// ============================================================================

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Prism {

    // (caminho relativo, conteudo). Nao grava nada.
    std::vector<std::pair<std::string, std::string>> GenerateExtensionTemplate(const std::string& name);

    // Cria <parentDirectory>/<name>/ com os arquivos acima. Falha (com 'error') se o nome for invalido
    // ou a pasta ja existir - nunca sobrescreve nada.
    bool WriteExtensionTemplate(const std::filesystem::path& parentDirectory, const std::string& name,
                                std::filesystem::path& outDirectory, std::string& error);

    // Nome seguro como identificador C++ ("My-Ext" -> "My_Ext", "3d" -> "_3d").
    std::string ExtensionIdentifier(const std::string& name);

} // namespace Prism
