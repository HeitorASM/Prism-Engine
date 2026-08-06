#pragma once

// ============================================================================
// EditorLayer.h
// O editor de verdade: dockspace com viewport 3D, painel de hierarquia,
// painel de propriedades e console. So existe depois que um Project esta
// ativo (ver ProjectManagerLayer).
//
// A viewport agora renderiza uma cena de verdade (um cubo de teste) num
// Framebuffer offscreen e desenha esse resultado dentro do painel ImGui via
// ImGui::Image (ver RenderViewportPanel()). Isso prova o caminho completo
// Framebuffer -> Renderer -> ImGui::Image que o resto da engine (Scene real,
// gizmos, picking por raycast, etc) vai construir em cima daqui pra frente.
// ============================================================================

#include <Prism.h>
#include <glm/glm.hpp>

namespace PrismEditor {

    class EditorLayer : public Prism::Layer {
    public:
        EditorLayer();

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnImGuiRender() override;
        void OnEvent(Prism::Event& event) override;

    private:
        void RenderDockspace();
        void RenderMenuBar();
        void RenderViewportPanel();
        void RenderHierarchyPanel();
        void RenderPropertiesPanel();
        void RenderConsolePanel();

        // Desenha a cena (por ora, so o cubo de teste) dentro do
        // m_ViewportFramebuffer. Chamado de OnUpdate, antes do ImGui
        // desenhar - o resultado (uma textura de cor) e que aparece dentro
        // do painel Viewport neste mesmo frame.
        void RenderScene(float deltaTime);

    private:
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        float m_ViewportSize[2] = { 0.0f, 0.0f };

        // Framebuffer offscreen onde a cena 3D e desenhada. O color
        // attachment dele e o que vira ImGui::Image() dentro do painel
        // Viewport - ver RenderViewportPanel().
        Prism::Scope<Prism::Framebuffer> m_ViewportFramebuffer;

        // Camera de orbita minima para a viewport do editor (nao e a camera
        // FPS/TPS de jogo mencionada no guia - essa vira quando existir um
        // modo "jogar dentro do editor"). Controle: botao direito do mouse
        // segurado sobre a viewport + arrastar orbita; scroll aproxima/afasta.
        float m_CameraYaw = -35.0f;   // graus
        float m_CameraPitch = 25.0f;  // graus
        float m_CameraDistance = 4.0f;
        bool m_OrbitingCamera = false;
        float m_LastMouseX = 0.0f;
        float m_LastMouseY = 0.0f;

        // Rotacao do cubo de teste - so para deixar visivel na tela que a
        // cena esta realmente rodando (nao e uma imagem estatica).
        float m_CubeRotation = 0.0f;

        // Placeholders de estado de UI - viram sistemas reais nas proximas fases
        // (Scene/Entity para hierarquia real; Undo/Redo command stack, etc).
        int m_SelectedEntityIndex = -1;
    };

}
