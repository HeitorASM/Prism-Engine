#pragma once

// ============================================================================
// ViewportPanel.h
// Painel Viewport: renderiza a cena para um framebuffer (camera livre do
// editor), navegacao (modo voar), selecao por clique, drop de prefab e o
// gizmo de transform (ImGuizmo). Antes: ~700 linhas e 8 membros de
// EditorLayer. Os gizmos de linha estao em EditorGizmos.h.
// ============================================================================

#include "../Core/EditorContext.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>

namespace PrismEditor {

    class ViewportPanel {
    public:
        explicit ViewportPanel(EditorContext& context) : m_Ctx(context) {}

        // Cria o framebuffer (precisa de contexto OpenGL: chamar em OnAttach).
        void Init();

        // Redimensiona o framebuffer se o painel mudou de tamanho desde o
        // ultimo frame. Chamar no inicio de OnUpdate.
        void ResizeIfNeeded();

        // Desenha todas as entidades da Scene com MeshRendererComponent
        // dentro do m_ViewportFramebuffer, usando a camera LIVRE do
        // editor (EditorContext::Camera.Position + EditorContext::Camera.Yaw/EditorContext::Camera.Pitch - ver
        // ComputeEditorViewMatrix) - NAO usa nenhuma CameraComponent::Primary
        // aqui, mesmo que exista uma na cena (ver nota em
        // PropertiesPanel::RenderCameraPreview() abaixo sobre o motivo). Chamado de
        // OnUpdate, antes do ImGui - o resultado (uma textura de cor) e
        // que aparece dentro do painel Viewport neste mesmo frame.
        void RenderScene(float deltaTime);

        void OnImGuiRender();


    private:
        // Desenha o gizmo de manipulacao (ImGuizmo) sobre a entidade
        // atualmente SELECIONADA (EditorContext::SelectedEntity) - as setas/planos de
        // Translate, os aneis de Rotate ou as caixinhas de Scale,
        // dependendo de m_GizmoOperation. Diferente dos outros gizmos
        // acima (RenderCameraGizmos etc, que sao desenhados DENTRO do
        // framebuffer da viewport via Renderer::DrawLines), este e
        // desenhado por CIMA da imagem ja renderizada, usando a API de
        // overlay 2D do ImGuizmo (ImGui::GetWindowDrawList() da propria
        // janela "Viewport") - por isso e chamado de dentro de
        // OnImGuiRender(), depois do ImGui::Image(), nao de dentro
        // de RenderScene(). Escreve direto em
        // EditorContext::ActiveScene->GetWorldTransform-equivalente local (via
        // TransformComponent, respeitando um pai se houver - ver
        // comentario no .cpp) e empurra UM TransformCommand no
        // EditorContext::History quando o arraste termina (mesmo padrao de
        // "um comando por gesto" que os DragFloat3 da Properties panel ja
        // usam - ver ImGui::IsItemActivated()/IsItemDeactivatedAfterEdit
        // la, e o equivalente ImGuizmo::IsUsing() aqui). Nao faz nada se
        // nada estiver selecionado.
        // 'imageScreenPos' e a posicao de tela (GetItemRectMin(), NAO
        // GetWindowPos() - a janela inclui a barra de titulo, o que
        // desalinhava a area de clique/hover do gizmo da imagem por conta
        // dessa altura extra, um bug ja corrigido) de onde a imagem da
        // viewport foi desenhada neste frame - ver OnImGuiRender().
        void RenderTransformGizmo(const glm::mat4& view, const glm::mat4& projection, const ImVec2& imageScreenPos);

        EditorContext& m_Ctx;

        bool m_ViewportFocused = false;

        bool m_ViewportHovered = false;

        float m_ViewportSize[2] = { 0.0f, 0.0f };

        // Framebuffer offscreen onde a cena 3D e desenhada. O color
        // attachment dele e o que vira ImGui::Image() dentro do painel
        // Viewport - ver OnImGuiRender().
        Prism::Scope<Prism::Framebuffer> m_ViewportFramebuffer;

        // Estado do gizmo de manipulacao (ImGuizmo) - ver RenderTransformGizmo().
        // m_GizmoOperation troca com as teclas W (Translate) / E (Rotate) /
        // R (Scale), mesma convencao de atalho que Unity/Unreal/Godot usam
        // - checada em OnUpdate() so quando a viewport esta em foco, para
        // nao roubar W/E/R de um campo de texto sendo editado em outro
        // painel. m_GizmoMode alterna Local/World (tecla nao mapeada
        // ainda - so o botao na toolbar da viewport, ver
        // OnImGuiRender()).
        //
        // Usar os tipos de verdade do ImGuizmo (em vez de int com um
        // comentario "isto e TRANSLATE") evita depender do VALOR NUMERICO
        // por tras de cada enumeracao - era exatamente esse descompasso
        // (int = 0 "achando" que era TRANSLATE) que fazia o gizmo nao
        // aparecer por padrao ao selecionar uma entidade na viewport.
        // Sem o tipo certo, ImGuizmo::Manipulate() recebia uma operacao
        // invalida e simplesmente nao desenhava nada.
        ImGuizmo::OPERATION m_GizmoOperation = ImGuizmo::TRANSLATE;

        ImGuizmo::MODE m_GizmoMode = ImGuizmo::WORLD;

        // Mesmo padrao de m_TransformBeforeEdit (ver acima), so que para o
        // gesto de arrastar o gizmo: capturado no frame em que
        // ImGuizmo::IsUsing() vira true, usado para montar UM
        // TransformCommand quando IsUsing() volta a false (arraste
        // terminou) - ver RenderTransformGizmo() no .cpp.
        Prism::TransformComponent m_GizmoTransformBeforeEdit;

        bool m_GizmoWasUsingLastFrame = false;
    };

}
