#pragma once

// ============================================================================
// FileDialog.h
// Dialogos nativos do sistema operacional (abrir arquivo, escolher pasta) via
// nativefiledialog-extended, mais dois helpers de conversao UTF-8 <-> path.
//
// Os dialogos sao MODAIS e bloqueiam a thread principal ate o usuario
// responder (o ImGui simplesmente nao desenha nesse intervalo). Cancelar
// devolve um path vazio.
//
// Por que os helpers UTF-8: os campos de texto do ImGui guardam UTF-8, mas
// std::filesystem::path(std::string) no Windows interpreta o texto na code
// page ANSI - um caminho com acento ("C:\Users\Jose\Jogos Ação") viraria
// lixo. Sempre converta com FromUtf8/ToUtf8 nas bordas da interface.
// ============================================================================

#include <filesystem>
#include <string>

namespace PrismEditor::FileDialog {

    // Escolher uma pasta. 'startIn' (se existir) e onde o dialogo abre.
    std::filesystem::path PickFolder(const std::filesystem::path& startIn = {});

    // Escolher um arquivo existente. 'filterName' e o texto mostrado ("Projeto Prism")
    // e 'filterSpec' as extensoes SEM ponto, separadas por virgula ("prismproj").
    std::filesystem::path OpenFile(const char* filterName, const char* filterSpec,
                                   const std::filesystem::path& startIn = {});

    std::string ToUtf8(const std::filesystem::path& path);
    std::filesystem::path FromUtf8(const std::string& text);

}
