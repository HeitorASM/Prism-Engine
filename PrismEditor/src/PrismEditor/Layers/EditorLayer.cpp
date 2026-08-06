#include "EditorLayer.h"
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>

namespace PrismEditor {

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    void EditorLayer::OnAttach() {
        PRISM_INFO("EditorLayer anexada. Projeto ativo: ",
            Prism::Project::GetActive()->GetConfig().Name);

        // Tamanho inicial arbitrario - sera ajustado no primeiro frame em
        // que RenderViewportPanel() souber o tamanho real do painel ImGui.
        Prism::FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_ViewportFramebuffer = Prism::Framebuffer::Create(fbSpec);
    }

    void EditorLayer::OnDetach() {}

    void EditorLayer::OnUpdate(float deltaTime) {
        // Redimensiona o framebuffer se o painel Viewport mudou de tamanho
        // desde o ultimo frame (arrastar a janela, dockar/desdockar, etc).
        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        if (m_ViewportSize[0] > 0.0f && m_ViewportSize[1] > 0.0f &&
            (spec.Width != (uint32_t)m_ViewportSize[0] || spec.Height != (uint32_t)m_ViewportSize[1])) {
            m_ViewportFramebuffer->Resize((uint32_t)m_ViewportSize[0], (uint32_t)m_ViewportSize[1]);
        }

        RenderScene(deltaTime);
    }

    void EditorLayer::RenderScene(float deltaTime) {
        m_ViewportFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        float aspect = spec.Height > 0 ? (float)spec.Width / (float)spec.Height : 1.0f;

        // Camera de orbita: posicao calculada a partir de yaw/pitch/distancia
        // ao redor da origem, olhando sempre para o centro da cena.
        float yawRad = glm::radians(m_CameraYaw);
        float pitchRad = glm::radians(m_CameraPitch);
        glm::vec3 cameraPos;
        cameraPos.x = m_CameraDistance * cosf(pitchRad) * cosf(yawRad);
        cameraPos.y = m_CameraDistance * sinf(pitchRad);
        cameraPos.z = m_CameraDistance * cosf(pitchRad) * sinf(yawRad);

        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        glm::mat4 viewProjection = projection * view;

        // Cubo de teste rotacionando lentamente - prova visual de que a
        // cena esta sendo simulada e redesenhada frame a frame, nao e uma
        // imagem estatica dentro do painel.
        m_CubeRotation += deltaTime * 30.0f; // graus por segundo
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(m_CubeRotation), glm::vec3(0.3f, 1.0f, 0.1f));

        Prism::Renderer::DrawTestCube(glm::value_ptr(viewProjection), glm::value_ptr(model));

        m_ViewportFramebuffer->Unbind();
    }

    void EditorLayer::OnEvent(Prism::Event& event) {
        // O controle de camera propriamente dito (arrastar com botao
        // direito) e tratado em RenderViewportPanel() usando o estado de
        // mouse do proprio ImGui, porque so faz sentido reagir quando o
        // mouse esta sobre o painel Viewport - o que o ImGui ja sabe dizer
        // (IsWindowHovered) sem precisarmos de um sistema de Input por
        // polling na engine ainda (isso fica para quando o modo "jogar
        // dentro do editor" precisar de WASD de verdade).
    }

    void EditorLayer::OnImGuiRender() {
        RenderDockspace();
    }

    void EditorLayer::RenderDockspace() {
        static bool dockspaceOpen = true;
        static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("PrismEditorDockspace", &dockspaceOpen, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspaceId = ImGui::GetID("PrismDockspace");
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
        }

        RenderMenuBar();

        ImGui::End();

        // Paineis - cada um e sua propria janela ImGui, e o dockspace acima
        // permite que o usuario os arraste/organize livremente.
        RenderViewportPanel();
        RenderHierarchyPanel();
        RenderPropertiesPanel();
        RenderConsolePanel();
    }

    void EditorLayer::RenderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Arquivo")) {
                if (ImGui::MenuItem("Novo Mapa")) { /* TODO */ }
                if (ImGui::MenuItem("Salvar Mapa", "Ctrl+S")) { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Fechar Projeto")) {
                    Prism::Application::Get().Close(); // TODO: voltar ao ProjectManagerLayer em vez de fechar
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Editar")) {
                if (ImGui::MenuItem("Desfazer", "Ctrl+Z")) { /* TODO: Undo/Redo stack */ }
                if (ImGui::MenuItem("Refazer", "Ctrl+Y")) { /* TODO */ }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Janela")) {
                ImGui::MenuItem("Viewport", nullptr, true, false);
                ImGui::MenuItem("Hierarquia", nullptr, true, false);
                ImGui::MenuItem("Propriedades", nullptr, true, false);
                ImGui::MenuItem("Console", nullptr, true, false);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

    void EditorLayer::RenderViewportPanel() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        ImVec2 size = ImGui::GetContentRegionAvail();
        // Nunca deixamos o tamanho chegar a zero - um framebuffer 0x0 e
        // invalido em OpenGL (ver Framebuffer::Resize, que ja ignora isso
        // tambem por seguranca).
        m_ViewportSize[0] = std::max(size.x, 1.0f);
        m_ViewportSize[1] = std::max(size.y, 1.0f);

        // A cena ja foi desenhada no framebuffer em OnUpdate() deste mesmo
        // frame - aqui so pegamos o color attachment (uma textura OpenGL
        // comum) e desenhamos como uma imagem dentro do painel ImGui. E
        // exatamente assim que Unity/Unreal/Godot/Hazel mostram a viewport
        // 3D dentro de uma janela de UI dockavel.
        uint32_t textureID = m_ViewportFramebuffer->GetColorAttachmentID();
        ImGui::Image((ImTextureID)(uintptr_t)textureID, ImVec2(m_ViewportSize[0], m_ViewportSize[1]),
                     ImVec2(0, 1), ImVec2(1, 0)); // UV invertido no Y: origem do framebuffer OpenGL e embaixo a esquerda.

        // Controle de camera minimo: segurar botao direito do mouse sobre a
        // viewport e arrastar orbita a camera; scroll (com o mouse sobre a
        // viewport) aproxima/afasta. E deliberadamente simples - vira a
        // camera de editor "de verdade" (com pan, foco em objeto, etc)
        // quando o resto do editor (selecao, gizmos) existir.
        if (m_ViewportHovered) {
            ImGuiIO& io = ImGui::GetIO();

            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                ImVec2 delta = io.MouseDelta;
                m_CameraYaw += delta.x * 0.4f;
                m_CameraPitch = std::clamp(m_CameraPitch - delta.y * 0.4f, -89.0f, 89.0f);
            }

            if (io.MouseWheel != 0.0f) {
                m_CameraDistance = std::clamp(m_CameraDistance - io.MouseWheel * 0.5f, 1.0f, 25.0f);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::RenderHierarchyPanel() {
        ImGui::Begin("Hierarquia");
        ImGui::TextDisabled("Nenhuma Scene carregada ainda.");
        ImGui::TextDisabled("(Sistema de Scene/Entity entra na proxima fase)");
        ImGui::Separator();
        ImGui::TextDisabled("Cena atual: 1 cubo de teste (hardcoded)");
        ImGui::End();
    }

    void EditorLayer::RenderPropertiesPanel() {
        ImGui::Begin("Propriedades");
        if (m_SelectedEntityIndex == -1) {
            ImGui::TextDisabled("Nada selecionado.");
        }
        ImGui::Separator();
        ImGui::TextDisabled("Camera do editor");
        ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_CameraYaw, m_CameraPitch);
        ImGui::Text("Distancia: %.2f", m_CameraDistance);
        ImGui::End();
    }

    void EditorLayer::RenderConsolePanel() {
        ImGui::Begin("Console");
        ImGui::TextDisabled("Saida de log aparecera aqui.");
        ImGui::TextDisabled("(Hoje o Log.h escreve no stdout/stderr do processo)");
        ImGui::End();
    }

}
