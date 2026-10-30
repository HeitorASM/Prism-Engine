#pragma once

// ============================================================================
// GamePanel.h
// Aba "Game": roda o jogo DENTRO do editor, ao lado da Viewport (como a aba
// Game da Unity). Faz o mesmo que a PlayWindow (Play/PlayWindow.h) - roda uma
// COPIA da cena (Scene::Clone), nunca a de edicao - mas desenha num
// framebuffer mostrado como imagem no painel, em vez de abrir uma janela do SO.
//
// Entrada (teclado/mouse): o jogo le Input da janela do editor. So "captura"
// depois que o usuario CLICA na imagem; enquanto nao captura, Input devolve
// "nada apertado" e os atalhos do editor (Delete, Ctrl+Z...) seguem valendo.
// Esc devolve o input ao editor. O mouse chega ao jogo relativo ao canto da
// imagem (Input::SetMouseOrigin), entao a UI in-game e os cliques batem.
//
// Limite conhecido: a aba precisa ficar DOCADA na janela principal do editor
// (se for arrastada para uma janela propria do SO, o mouse deixa de bater com
// a imagem; teclado continua funcionando).
// ============================================================================

#include "../Core/EditorContext.h"
#include <Prism.h>
#include <cstdint>

namespace PrismEditor {

    class GamePanel {
    public:
        explicit GamePanel(EditorContext& context) : m_Ctx(context) {}
        ~GamePanel();

        GamePanel(const GamePanel&) = delete;
        GamePanel& operator=(const GamePanel&) = delete;

        // Clona a cena de edicao e comeca a rodar (scripts + fisica). Cria o
        // framebuffer na primeira vez (precisa de contexto OpenGL). false se ja
        // estiver rodando ou o clone falhar.
        bool Open(Prism::Ref<Prism::Scene> editorScene);

        // Para scripts/fisica, descarta a copia e devolve o input ao editor.
        // Idempotente.
        void Close();

        bool IsOpen() const { return m_PlayScene != nullptr; }

        // true enquanto o jogo esta com o teclado/mouse (depois do clique na imagem).
        bool IsCapturingInput() const { return m_Capturing; }

        // Simula um frame e desenha no framebuffer. Chamar de OnUpdate do
        // EditorLayer, ANTES do ImGui (mesmo ponto da ViewportPanel::RenderScene).
        void OnUpdate(float deltaTime);

        // A aba em si. Sempre desenhada (mostra uma dica quando nao ha Play).
        void OnImGuiRender();

        // Pede o foco da aba no proximo frame (ao apertar Play).
        void RequestFocus() { m_FocusRequested = true; }

    private:
        EditorContext& m_Ctx;

        Prism::Ref<Prism::Scene> m_PlayScene;
        Prism::Ref<Prism::Scene> m_PristineScene; // copia intacta da cena de edicao (Scene.Reload)
        Prism::Scope<Prism::Framebuffer> m_Framebuffer;

        // Tamanho/posicao da imagem no ultimo frame de ImGui (a simulacao usa
        // o do frame anterior - mesma defasagem de 1 frame da Viewport).
        float m_ImageSize[2] = { 0.0f, 0.0f };
        float m_ImageOrigin[2] = { 0.0f, 0.0f };

        bool m_Capturing = false;
        bool m_FocusRequested = false;
        bool m_LoggedNoCameraWarning = false;
    };

}
