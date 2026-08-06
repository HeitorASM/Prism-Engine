// ============================================================================
// REGRA DE OURO desta engine, valida para TODO arquivo .cpp do projeto:
//
//   #include <glad/gl.h>     SEMPRE a primeira linha de include de qualquer
//                            arquivo que toque em OpenGL ou GLFW, direta ou
//                            indiretamente.
//
// Motivo: o glad.h define os prototipos de TODAS as funcoes OpenGL. Se o
// header do sistema (gl.h/GL/glext.h) ou o GLFW forem incluidos antes dele,
// GLFW detecta que "OpenGL ja foi incluido" e o proprio GLFW inclui o header
// nativo do SO - o que colide com o glad e gera o erro:
//   "OpenGL header already included, remove this include, glad already
//    provides it."
//
// Esta regra e A causa mais comum de erro de build nesta engine. Se um dia
// aparecer esse erro de novo, o primeiro lugar a olhar e a ORDEM DE INCLUDES
// do arquivo que falhou - glad.h tem que vir antes de QUALQUER outra coisa.
// ============================================================================
#include <glad/gl.h>
#include "OpenGLContext.h"

#include "../Core/Log.h"
#include <GLFW/glfw3.h>

namespace Prism {

    Scope<GraphicsContext> GraphicsContext::Create(void* window) {
        return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
    }

    OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle) {
        PRISM_ASSERT(windowHandle, "Handle de janela e nulo!");
    }

    void OpenGLContext::Init() {
        glfwMakeContextCurrent(m_WindowHandle);
        int status = gladLoadGL((GLADloadfunc)glfwGetProcAddress);
        PRISM_ASSERT(status, "Falha ao inicializar o Glad!");

        PRISM_CORE_INFO("OpenGL Info:");
        PRISM_CORE_INFO("  Vendor:   ", (const char*)glGetString(GL_VENDOR));
        PRISM_CORE_INFO("  Renderer: ", (const char*)glGetString(GL_RENDERER));
        PRISM_CORE_INFO("  Version:  ", (const char*)glGetString(GL_VERSION));

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void OpenGLContext::SwapBuffers() {
        glfwSwapBuffers(m_WindowHandle);
    }

}
