// Lembrete da regra de ouro do projeto: glad SEMPRE antes de qualquer coisa
// que toque OpenGL/GLFW (ver comentario completo em OpenGLContext.cpp).
#include <glad/gl.h>
#include "Framebuffer.h"
#include "../Core/Log.h"

namespace Prism {

    Framebuffer::Framebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec) {
        Invalidate();
    }

    Framebuffer::~Framebuffer() {
        glDeleteFramebuffers(1, &m_RendererID);
        glDeleteTextures(1, &m_ColorAttachment);
        glDeleteTextures(1, &m_DepthAttachment);
    }

    Scope<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec) {
        return CreateScope<Framebuffer>(spec);
    }

    void Framebuffer::Invalidate() {
        // Se ja existe um framebuffer antigo (caso de Resize), destroi antes
        // de recriar - GPU nao tem "realloc", so create/destroy.
        if (m_RendererID) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(1, &m_ColorAttachment);
            glDeleteTextures(1, &m_DepthAttachment);
        }

        glCreateFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

        // Anexo de cor: o que efetivamente vira a imagem mostrada no painel
        // Viewport via ImGui::Image.
        glCreateTextures(GL_TEXTURE_2D, 1, &m_ColorAttachment);
        glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

        // Anexo de profundidade: necessario para que objetos 3D ocluam uns
        // aos outros corretamente dentro do framebuffer offscreen (sem isso,
        // o teste de profundidade feito em OpenGLContext::Init nao teria
        // onde escrever quando estivessemos desenhando para este FBO).
        glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthAttachment);
        glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);

        PRISM_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                     "Framebuffer incompleto!");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Framebuffer::Bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glViewport(0, 0, m_Specification.Width, m_Specification.Height);
    }

    void Framebuffer::Unbind() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Framebuffer::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || width > 8192 || height > 8192) {
            PRISM_CORE_WARN("Framebuffer::Resize ignorado com tamanho invalido: ", width, "x", height);
            return;
        }
        if (width == m_Specification.Width && height == m_Specification.Height)
            return;

        m_Specification.Width = width;
        m_Specification.Height = height;
        Invalidate();
    }

}
