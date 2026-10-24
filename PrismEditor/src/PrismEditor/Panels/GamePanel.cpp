#include <glad/gl.h>
#include "GamePanel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>

namespace PrismEditor {

    GamePanel::~GamePanel() {
        // Sem Application::Get() aqui (pode ja ser nulo no shutdown): so zera o estado.
        if (m_PlayScene) {
            m_PlayScene->OnScriptsStop();
            m_PlayScene = nullptr;
        }
    }

    bool GamePanel::Open(Prism::Ref<Prism::Scene> editorScene) {
        if (m_PlayScene || !editorScene)
            return false;

        Prism::Ref<Prism::Scene> cloned = editorScene->Clone();
        if (!cloned) {
            PRISM_CORE_ERROR("GamePanel::Open: falha ao clonar a Scene de edicao.");
            return false;
        }

        if (!m_Framebuffer) {
            Prism::FramebufferSpecification spec;
            spec.Width = 1280;
            spec.Height = 720;
            m_Framebuffer = Prism::Framebuffer::Create(spec);
        }

        // Input ANTES de OnScriptsStart: scripts que chamam Input.SetCursorMode
        // no OnCreate precisam do contexto ja apontado (ver PlayWindow::Open).
        // Comeca SEM capturar: o usuario clica na imagem para jogar.
        GLFWwindow* editorWindow = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();
        Prism::Input::SetContext(editorWindow);
        Prism::Input::SetEnabled(false);
        Prism::UISystem::ResetInput();
        m_Capturing = false;
        m_LoggedNoCameraWarning = false;

        m_PristineScene = editorScene->Clone();
        m_PlayScene = cloned;
        m_PlayScene->OnScriptsStart();
        m_FocusRequested = true;

        PRISM_CORE_INFO("Game: jogo rodando na aba Game (Scene clonada).");
        return true;
    }

    void GamePanel::Close() {
        if (!m_PlayScene)
            return; // idempotente

        m_PlayScene->OnScriptsStop();

        // Devolve o input ao editor: SetContext(nullptr) tambem volta o cursor
        // ao normal (ver Input::SetContext) - um script pode ter travado o cursor.
        Prism::Input::SetContext(nullptr);
        Prism::UISystem::ResetInput();

        m_PlayScene = nullptr;
        m_PristineScene = nullptr;
        m_Capturing = false;
        PRISM_CORE_INFO("Game: parado.");
    }

    void GamePanel::OnUpdate(float deltaTime) {
        if (!m_PlayScene || !m_Framebuffer)
            return;

        // Tamanho do ultimo frame de ImGui (0 enquanto a aba nunca foi vista ou
        // esta oculta atras de outra aba: nesse caso simula, mas nao desenha).
        const uint32_t width = (uint32_t)std::max(m_ImageSize[0], 0.0f);
        const uint32_t height = (uint32_t)std::max(m_ImageSize[1], 0.0f);

        Prism::Input::SetEnabled(m_Capturing);
        Prism::Input::SetMouseOrigin(m_ImageOrigin[0], m_ImageOrigin[1]);

        m_PlayScene->OnUpdate(deltaTime);
        Prism::Input::EndFrame();

        // Scene.Load / Scene.Reload pedidos por scripts: aplica depois do frame.
        if (Prism::Scene::ApplyPendingLoad(m_PlayScene, m_PristineScene))
            m_LoggedNoCameraWarning = false;

        if (width == 0 || height == 0)
            return;

        const auto& spec = m_Framebuffer->GetSpecification();
        if (spec.Width != width || spec.Height != height)
            m_Framebuffer->Resize(width, height);

        m_Framebuffer->Bind();
        bool drewCamera = Prism::GameView::Render(*m_PlayScene, (float)width, (float)height, m_Capturing);
        m_Framebuffer->Unbind();

        if (!drewCamera && !m_LoggedNoCameraWarning) {
            PRISM_CORE_WARN("Game: nenhuma entidade com CameraComponent::Primary=true na cena - a aba Game ficara vazia ate existir uma.");
            m_LoggedNoCameraWarning = true;
        }
    }

    void GamePanel::OnImGuiRender() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (m_FocusRequested) {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        const bool visible = ImGui::Begin("Game");
        ImGui::PopStyleVar();

        if (!visible || !m_PlayScene) {
            m_ImageSize[0] = m_ImageSize[1] = 0.0f;
            if (visible) {
                ImGui::SetCursorPos(ImVec2(16.0f, 16.0f));
                ImGui::TextDisabled("Aperte Play para rodar o jogo aqui.");
            }
            ImGui::End();
            return;
        }

        ImVec2 size = ImGui::GetContentRegionAvail();
        m_ImageSize[0] = std::max(size.x, 1.0f);
        m_ImageSize[1] = std::max(size.y, 1.0f);

        ImGui::Image((ImTextureID)(uintptr_t)m_Framebuffer->GetColorAttachmentID(),
            ImVec2(m_ImageSize[0], m_ImageSize[1]), ImVec2(0, 1), ImVec2(1, 0));

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        // Coordenadas de tela do ImGui -> coordenadas da janela do editor (o
        // GLFW mede o mouse a partir do canto da janela principal).
        m_ImageOrigin[0] = imageMin.x - mainViewport->Pos.x;
        m_ImageOrigin[1] = imageMin.y - mainViewport->Pos.y;

        // Clique na imagem = captura. Esc = devolve ao editor.
        // (else-if: o foco da aba so muda no fim do frame do clique, entao a
        // checagem de foco nao pode rodar no mesmo frame em que capturou.)
        bool releaseInput = false;
        if (!m_Capturing) {
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                m_Capturing = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || !ImGui::IsWindowFocused()) {
            releaseInput = true;
        }
        if (releaseInput) {
            m_Capturing = false;
            // Um script pode ter travado o cursor (camera FPS): solta, senao o
            // usuario nao consegue voltar ao editor.
            if (Prism::Input::GetCursorMode() != Prism::CursorMode::Normal)
                Prism::Input::SetCursorMode(Prism::CursorMode::Normal);
        }

        // Dica no canto, por cima da imagem.
        ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
        if (m_Capturing)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 0.85f), "Jogo com o teclado/mouse - Esc para sair");
        else
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.6f), "Clique para controlar o jogo");

        ImGui::End();
    }

}
