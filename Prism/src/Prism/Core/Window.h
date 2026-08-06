#pragma once

// ============================================================================
// Window.h
// Abstracao de janela. Hoje a unica implementacao e GLFW, mas a interface
// nao vaza nenhum detalhe do GLFW - isso significa que se um dia trocarmos
// para SDL (como sua lista de requisitos menciona GLFW/SDL como opcoes),
// so precisamos escrever uma nova classe WindowsWindow/SDLWindow, sem tocar
// em Application, Layers ou no Editor.
// ============================================================================

#include "Base.h"
#include "Event.h"
#include <string>
#include <cstdint>

struct GLFWwindow;

namespace Prism {

    struct WindowProps {
        std::string Title;
        uint32_t Width;
        uint32_t Height;

        WindowProps(const std::string& title = "Prism Engine",
                    uint32_t width = 1600,
                    uint32_t height = 900)
            : Title(title), Width(width), Height(height) {}
    };

    // Interface de janela. Representa uma janela desktop com contexto grafico.
    class Window {
    public:
        virtual ~Window() = default;

        virtual void OnUpdate() = 0;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSync() const = 0;

        virtual void* GetNativeWindow() const = 0;

        static Scope<Window> Create(const WindowProps& props = WindowProps());
    };

}
