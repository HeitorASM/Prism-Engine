#pragma once

// ============================================================================
// Application.h
// O nucleo da engine em runtime. Existe exatamente UMA Application por
// processo. Ela possui a Window, a LayerStack e roda o loop principal.
//
// Tanto o Editor (PrismEditor.exe) quanto, futuramente, jogos exportados em
// modo Runtime vao derivar desta classe ou instancia-la e empilhar suas
// proprias Layers - o core nunca precisa saber qual dos dois esta rodando.
// ============================================================================

#include "Base.h"
#include "Window.h"
#include "Event.h"
#include "ApplicationEvent.h"
#include "../Layer/LayerStack.h"
#include "../ImGui/ImGuiLayer.h"

#include <functional>
#include <vector>

namespace Prism {

    struct ApplicationSpecification {
        std::string Name = "Prism Application";
        uint32_t WindowWidth = 1600;
        uint32_t WindowHeight = 900;
    };

    class Application {
    public:
        Application(const ApplicationSpecification& spec);
        virtual ~Application();

        void Run();
        void Close();
        void OnEvent(Event& e);

        // IMPORTANTE: estas funcoes sao seguras de chamar de DENTRO de
        // OnUpdate()/OnImGuiRender()/OnEvent() de qualquer Layer (inclusive a
        // que esta chamando "se destruir", como o ProjectManagerLayer faz ao
        // trocar para o EditorLayer). Elas NAO mexem na LayerStack na hora:
        // apenas enfileiram a operacao, que e aplicada com seguranca em
        // Run(), depois que o loop atual de iteracao sobre a LayerStack
        // termina. Mexer no vector da LayerStack no meio de um "for (Layer*
        // l : m_LayerStack)" invalida o iterador do range-for e corrompe a
        // memoria (era a causa dos crashes "invalidated vector iterator" /
        // "parametro invalido" que este comentario documenta para o futuro).
        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);
        void PopLayer(Layer* layer);
        void PopOverlay(Layer* overlay);

        Window& GetWindow() { return *m_Window; }
        ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }

        static Application& Get() { return *s_Instance; }

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);

        // Aplica todas as operacoes de push/pop enfileiradas durante o frame.
        // So pode ser chamada de Run(), FORA de qualquer iteracao ativa sobre
        // m_LayerStack.
        void ProcessPendingLayerOps();

    private:
        ApplicationSpecification m_Specification;
        Scope<Window> m_Window;
        ImGuiLayer* m_ImGuiLayer;
        bool m_Running = true;
        bool m_Minimized = false;
        LayerStack m_LayerStack;
        float m_LastFrameTime = 0.0f;

        std::vector<std::function<void()>> m_PendingLayerOps;

        static Application* s_Instance;
    };

    // Definida pelo lado da aplicacao concreta (o Editor, por exemplo).
    // O core da engine nao sabe o que essa funcao retorna - so a chama.
    Application* CreateApplication();

}
