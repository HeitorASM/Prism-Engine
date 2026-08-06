#pragma once

// ============================================================================
// EditorLayer.h
// O editor de verdade: dockspace com viewport 3D, painel de hierarquia,
// painel de propriedades e console. So existe depois que um Project esta
// ativo (ver ProjectManagerLayer).
//
// Agora possui uma Scene real (Prism::Scene) com Entities de verdade -
// a Hierarchy panel lista as entidades da cena, clicar seleciona uma, e a
// Properties panel edita o TransformComponent da entidade selecionada. A
// Viewport desenha todas as entidades com MeshRendererComponent, cada uma
// com seu proprio TransformComponent, num Framebuffer offscreen mostrado
// via ImGui::Image.
//
// A Scene agora tambem persiste em disco (Prism::SceneSerializer, formato
// binario .prismmap dentro de Project::GetMapDirectory()) - ver
// LoadOrCreateScene() e SaveActiveScene().
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

        // Salva a Scene ativa em Project::GetMapDirectory()/<StartMap>,
        // criando o caminho no ProjectConfig se ainda nao existir (primeiro
        // save de um projeto novo). Chamado pelo menu Arquivo > Salvar Mapa
        // e por Ctrl+S (ver OnEvent... por ora so o menu, atalho fica para
        // quando o sistema de Input/atalhos existir).
        void SaveActiveScene();

        // Tenta carregar Project::GetConfig().StartMap; se nao existir
        // ainda (projeto novo, primeira vez abrindo o editor), cria uma
        // cena de exemplo em memoria em vez de falhar.
        void LoadOrCreateScene();

        // Desenha todas as entidades da Scene com MeshRendererComponent
        // dentro do m_ViewportFramebuffer. Chamado de OnUpdate, antes do
        // ImGui - o resultado (uma textura de cor) e que aparece dentro do
        // painel Viewport neste mesmo frame.
        void RenderScene(float deltaTime);

    private:
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        float m_ViewportSize[2] = { 0.0f, 0.0f };

        // Framebuffer offscreen onde a cena 3D e desenhada. O color
        // attachment dele e o que vira ImGui::Image() dentro do painel
        // Viewport - ver RenderViewportPanel().
        Prism::Scope<Prism::Framebuffer> m_ViewportFramebuffer;

        // A cena ativa do editor. Por ora criada em memoria com uma entidade
        // de exemplo em OnAttach() - salvar/carregar cenas do disco (dentro
        // de Project::GetMapDirectory(), ver Project.h) e o proximo passo
        // natural depois deste (ver README, secao "Proximos passos").
        Prism::Ref<Prism::Scene> m_ActiveScene;

        // Entidade atualmente selecionada na Hierarchy panel. Invalida
        // (Entity{}) quando nada esta selecionado.
        Prism::Entity m_SelectedEntity;

        // Camera de orbita minima para a viewport do editor (nao e a camera
        // FPS/TPS de jogo mencionada no guia - essa vira quando existir um
        // modo "jogar dentro do editor"). Controle: botao direito do mouse
        // segurado sobre a viewport + arrastar orbita; scroll aproxima/afasta.
        float m_CameraYaw = -35.0f;   // graus
        float m_CameraPitch = 25.0f;  // graus
        float m_CameraDistance = 6.0f;
    };

}
