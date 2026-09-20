#include <glad/gl.h>
#include "GlfwWindow.h"

#include "Log.h"
#include "ApplicationEvent.h"
#include "KeyEvent.h"
#include "MouseEvent.h"
#include "../Renderer/GraphicsContext.h"

#include <GLFW/glfw3.h>

namespace Prism {

    static uint8_t s_GLFWWindowCount = 0;

    static void GLFWErrorCallback(int error, const char* description) {
        PRISM_CORE_ERROR("GLFW Error (", error, "): ", description);
    }

    Scope<Window> Window::Create(const WindowProps& props) {
        return CreateScope<GlfwWindow>(props);
    }

    GlfwWindow::GlfwWindow(const WindowProps& props) {
        Init(props);
    }

    GlfwWindow::~GlfwWindow() {
        Shutdown();
    }

    void GlfwWindow::Init(const WindowProps& props) {
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        PRISM_CORE_INFO("Criando janela \"", props.Title, "\" (", props.Width, "x", props.Height, ")");

        if (s_GLFWWindowCount == 0) {
            int success = glfwInit();
            PRISM_ASSERT(success, "Nao foi possivel inicializar o GLFW!");
            glfwSetErrorCallback(GLFWErrorCallback);
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef PRISM_DEBUG
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

        m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
        ++s_GLFWWindowCount;

        // O contexto grafico e um objeto separado da janela de proposito:
        // no futuro, quando existir suporte a Vulkan, a Window nao precisa
        // saber nada sobre qual API grafica esta sendo usada por baixo.
        GraphicsContext::Create(m_Window)->Init();

        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(true);

        // --- Callbacks GLFW -> eventos Prism ---

        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.Width = width;
            data.Height = height;

            WindowResizeEvent event(width, height);
            data.EventCallback(event);
        });

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            WindowCloseEvent event;
            data.EventCallback(event);
        });

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            switch (action) {
                case GLFW_PRESS: {
                    KeyPressedEvent event(key, false);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE: {
                    KeyReleasedEvent event(key);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_REPEAT: {
                    KeyPressedEvent event(key, true);
                    data.EventCallback(event);
                    break;
                }
            }
        });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            switch (action) {
                case GLFW_PRESS: {
                    MouseButtonPressedEvent event(button);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE: {
                    MouseButtonReleasedEvent event(button);
                    data.EventCallback(event);
                    break;
                }
            }
        });

        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseScrolledEvent event((float)xOffset, (float)yOffset);
            data.EventCallback(event);
        });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseMovedEvent event((float)x, (float)y);
            data.EventCallback(event);
        });
    }

    void GlfwWindow::Shutdown() {
        glfwDestroyWindow(m_Window);
        --s_GLFWWindowCount;

        if (s_GLFWWindowCount == 0) {
            glfwTerminate();
        }
    }

    void GlfwWindow::OnUpdate() {
        glfwPollEvents();
        // O SwapBuffers real acontece via GraphicsContext, chamado pela Application
        // depois que a renderizacao do frame (incluindo ImGui) termina.
        glfwSwapBuffers(m_Window);
    }

    void GlfwWindow::SetVSync(bool enabled) {
        glfwSwapInterval(enabled ? 1 : 0);
        m_Data.VSync = enabled;
    }

}
