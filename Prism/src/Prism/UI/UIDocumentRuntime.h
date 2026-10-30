#pragma once

// ============================================================================
// UIDocumentRuntime.h
// Estado de EXECUCAO de um UIDocumentComponent: o Ui::Document ja lido do
// .prismui, o estado do mouse (hover/apertado) e o controle de hot reload.
// NAO e salvo na cena nem copiado (ResetOnCopy - ver UIDocumentComponent em
// Components.h): cada copia (Play, desfazer) recarrega o arquivo.
// So std (usa UIMarkup/UIInput, que tambem sao so std).
// ============================================================================

#include "UIDocument.h"
#include "UIInput.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace Prism {

    struct UIDocumentRuntime {
        Ui::Document Doc;
        Ui::Interaction Input;

        std::string LoadedPath;                       // caminho (do componente) que gerou 'Doc'
        std::filesystem::file_time_type WriteTime{};  // data do arquivo na carga (hot reload)
        bool HasDocument = false;                     // 'Doc' valido (a ultima leitura deu certo)
        bool FailedOnce = false;                      // ja logou erro para este caminho/data (evita spam)
        std::chrono::steady_clock::time_point NextCheck{}; // proxima checagem de data do arquivo
    };

}
