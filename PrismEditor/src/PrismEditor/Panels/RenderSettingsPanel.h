#pragma once

// ============================================================================
// RenderSettingsPanel.h
// Menu "Renderizacao" (exposicao e ambiente) e a gravacao no .prismproj.
// Antes: 2 metodos e 2 membros de EditorLayer. Nao depende do EditorContext:
// os ajustes valem para o PROJETO (Project::GetRenderSettings), nao para o
// mapa. Ver docs/editor.md, "Menu Renderizacao".
// ============================================================================

#include <Prism.h>

namespace PrismEditor {

    class RenderSettingsPanel {
    public:
        // Quanto tempo (s) sem editar o menu antes de gravar o .prismproj -
        // cobre arrastar slider, edicao pelo teclado (repeticao de tecla muda
        // o valor varios frames seguidos) e o picker de cor.
        static constexpr double kSaveDelay = 0.35;

        // --- Menu "Renderizacao" (barra de menus) --------------------------
        //
        // Exposicao, intensidade do ambiente e as 3 cores do gradiente. Os
        // valores vivem no Project ativo (RenderSettings, gravados no
        // .prismproj) - NAO na cena: mudar o visual nao marca o mapa como
        // "com alteracoes nao salvas" e nao entra no undo/redo.
        // Cada edicao e aplicada ao Renderer na hora (a viewport mostra ao
        // vivo); o .prismproj e gravado por FlushRenderSettingsSave.
        void OnImGuiRenderMenu();

        // Grava o .prismproj se algum ajuste do menu mudou e o usuario
        // parou de mexer (mouse solto ha uns instantes) - assim arrastar um
        // slider nao reescreve o arquivo a cada frame. Chamado todo frame
        // por RenderMenuBar.
        void FlushSave();

        // Grava AGORA um ajuste ainda pendente (a espera do debounce nao
        // acabou) - usado em OnDetach, quando nao vem mais nenhum frame. Sem
        // log e sem Application::Get(): seguro durante a destruicao.
        void FlushOnShutdown();

    private:
        // Menu "Renderizacao": ha ajustes ainda nao gravados no .prismproj, e
        // quando foi a ultima edicao (ImGui::GetTime) - ver
        // FlushRenderSettingsSave. Tambem no FIM da classe, pelo mesmo motivo
        // do bloco acima.
        bool m_NeedSave = false;

        double m_LastEdit = 0.0;
    };

}
