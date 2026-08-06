#pragma once
#include "Window.h"

struct GLFWwindow;

namespace Prism {

    // Implementacao concreta de Window usando GLFW + contexto OpenGL 4.5.
    class GlfwWindow : public Window {
    public:
        GlfwWindow(const WindowProps& props);
        virtual ~GlfwWindow();

        void OnUpdate() override;

        uint32_t GetWidth() const override { return m_Data.Width; }
        uint32_t GetHeight() const override { return m_Data.Height; }

        void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
        void SetVSync(bool enabled) override;
        bool IsVSync() const override { return m_Data.VSync; }

        void* GetNativeWindow() const override { return m_Window; }

    private:
        void Init(const WindowProps& props);
        void Shutdown();

    private:
        GLFWwindow* m_Window = nullptr;

        // Dados que precisam sobreviver dentro dos callbacks estaticos do GLFW.
        // GLFW so aceita ponteiros de funcao livres (nao lambdas com captura),
        // entao guardamos esse struct via glfwSetWindowUserPointer e o
        // recuperamos dentro de cada callback estatico.
        struct WindowData {
            std::string Title;
            uint32_t Width, Height;
            bool VSync = true;
            EventCallbackFn EventCallback;
        };

        WindowData m_Data;
    };

}
