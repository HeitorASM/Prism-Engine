#include <glad/gl.h> // sempre primeiro - ver OpenGLContext.cpp
#include "Application.h"

#include "Log.h"
#include "LogBuffer.h"
#include "../Renderer/Renderer.h"
#include <GLFW/glfw3.h>

namespace Prism {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec) {
        PRISM_ASSERT(!s_Instance, "So pode existir uma Application por processo!");
        s_Instance = this;

        // Instalado antes de qualquer log relevante acontecer, para que o
        // Console do editor (ou qualquer outro consumidor futuro) capture
        // o historico completo da sessao, nao so o que foi logado depois
        // que o EditorLayer/ConsolePanel foi anexado.
        LogBuffer::Install();

        WindowProps props(spec.Name, spec.WindowWidth, spec.WindowHeight);
        m_Window = Window::Create(props);
        m_Window->SetEventCallback(PRISM_BIND_EVENT_FN(OnEvent));

        // O contexto OpenGL ja esta valido neste ponto (Window::Create chama
        // GraphicsContext::Init() internamente) - e seguro inicializar a
        // parte de renderizacao da engine (shaders/geometria da GPU) agora.
        Renderer::Init();

        m_ImGuiLayer = new ImGuiLayer();
        PushOverlay(m_ImGuiLayer);
    }

    Application::~Application() {
        Renderer::Shutdown();
        s_Instance = nullptr;
    }

    // Nenhuma destas quatro funcoes toca m_LayerStack diretamente - elas so
    // enfileiram a operacao. Isso e o que torna seguro chama-las de dentro
    // de um OnUpdate/OnImGuiRender/OnEvent de uma Layer (ver comentario em
    // Application.h). A aplicacao de fato acontece em ProcessPendingLayerOps().
    void Application::PushLayer(Layer* layer) {
        m_PendingLayerOps.emplace_back([this, layer]() {
            m_LayerStack.PushLayer(layer);
        });
    }

    void Application::PushOverlay(Layer* overlay) {
        m_PendingLayerOps.emplace_back([this, overlay]() {
            m_LayerStack.PushOverlay(overlay);
        });
    }

    void Application::PopLayer(Layer* layer) {
        m_PendingLayerOps.emplace_back([this, layer]() {
            m_LayerStack.PopLayer(layer);
        });
    }

    void Application::PopOverlay(Layer* overlay) {
        m_PendingLayerOps.emplace_back([this, overlay]() {
            m_LayerStack.PopOverlay(overlay);
        });
    }

    void Application::ProcessPendingLayerOps() {
        if (m_PendingLayerOps.empty())
            return;

        // Copia e limpa antes de executar: se uma das operacoes enfileiradas
        // (ex: OnAttach() de uma Layer recem empilhada) enfileirar OUTRA
        // operacao, ela deve esperar o proximo frame, nao ser perdida nem
        // reentrar em m_PendingLayerOps enquanto o estamos percorrendo.
        std::vector<std::function<void()>> ops;
        ops.swap(m_PendingLayerOps);

        for (auto& op : ops)
            op();
    }

    void Application::Close() {
        m_Running = false;
    }

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(PRISM_BIND_EVENT_FN(OnWindowClose));
        dispatcher.Dispatch<WindowResizeEvent>(PRISM_BIND_EVENT_FN(OnWindowResize));

        // Propaga do topo da pilha (overlays/UI) para baixo. Se uma layer
        // marcar o evento como Handled (ex: ImGui capturou o clique do
        // mouse sobre um painel), as layers abaixo dela nao o recebem mais.
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it) {
            if (e.Handled)
                break;
            (*it)->OnEvent(e);
        }
    }

    bool Application::OnWindowClose(WindowCloseEvent& e) {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e) {
        if (e.GetWidth() == 0 || e.GetHeight() == 0) {
            m_Minimized = true;
            return false;
        }
        m_Minimized = false;
        glViewport(0, 0, e.GetWidth(), e.GetHeight());
        return false; // nao "consome": Layers tambem podem querer reagir ao resize
    }

    void Application::Run() {
        PRISM_CORE_INFO("Prism Engine - loop principal iniciado.");

        while (m_Running) {
            float time = (float)glfwGetTime();
            float deltaTime = time - m_LastFrameTime;
            m_LastFrameTime = time;

            // Aplica pushes/pops de Layer pedidos no frame ANTERIOR antes de
            // comecar um novo frame. Isso precisa acontecer aqui, no topo do
            // loop - e nao so no fim dele - porque o proprio construtor da
            // Application enfileira PushOverlay(m_ImGuiLayer) (ver acima), e
            // essa operacao so e executada num ProcessPendingLayerOps().
            // m_ImGuiLayer->Begin() logo abaixo chama
            // ImGui_ImplOpenGL3_NewFrame(), que exige que
            // ImGui_ImplOpenGL3_Init() (chamado em ImGuiLayer::OnAttach())
            // ja tenha rodado - senao o backend dispara
            // "Context or backend not initialized!". Processar aqui garante
            // que toda Layer enfileirada ate o frame anterior ja teve
            // OnAttach() chamado antes de qualquer OnUpdate/OnImGuiRender
            // deste frame usar o ImGui.
            ProcessPendingLayerOps();

            if (!m_Minimized) {
                for (Layer* layer : m_LayerStack)
                    layer->OnUpdate(deltaTime);

                m_ImGuiLayer->Begin();
                for (Layer* layer : m_LayerStack)
                    layer->OnImGuiRender();
                m_ImGuiLayer->End();
            }

            m_Window->OnUpdate();
        }

        PRISM_CORE_INFO("Prism Engine - loop principal encerrado.");
    }

}
