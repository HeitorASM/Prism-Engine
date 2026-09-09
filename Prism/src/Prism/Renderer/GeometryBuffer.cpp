#include <glad/gl.h>
#include "GeometryBuffer.h"
#include "../Core/Log.h"

namespace Prism {

    GeometryBuffer::GeometryBuffer(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height) {
        Invalidate();
    }

    GeometryBuffer::~GeometryBuffer() {
        glDeleteFramebuffers(1, &m_FBO);
        glDeleteTextures(1, &m_NormalAttachment);
        glDeleteTextures(1, &m_DepthAttachment);
    }

    void GeometryBuffer::Invalidate() {
        if (m_FBO) {
            glDeleteFramebuffers(1, &m_FBO);
            glDeleteTextures(1, &m_NormalAttachment);
            glDeleteTextures(1, &m_DepthAttachment);
        }

        glCreateFramebuffers(1, &m_FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

        // Anexo de normal - RGB16F (nao RGBA8): normais em view-space tem
        // componentes em [-1, 1], nao [0, 1] - um formato unsigned normalizado
        // (RGBA8) exigiria remapear (normal * 0.5 + 0.5) na escrita e desfazer
        // isso na leitura, alem de perder precisao. RGB16F guarda o valor
        // float diretamente, sem remapeamento e com precisao de sobra para
        // este uso (SSAO nao precisa da precisao completa de RGB32F).
        glCreateTextures(GL_TEXTURE_2D, 1, &m_NormalAttachment);
        glBindTexture(GL_TEXTURE_2D, m_NormalAttachment);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, (GLsizei)m_Width, (GLsizei)m_Height,
                     0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_NormalAttachment, 0);

        // Anexo de profundidade - textura (nao renderbuffer, ao contrario de
        // Framebuffer::m_DepthAttachment que e combinado com stencil): o
        // shader de SSAO precisa AMOSTRAR isto (sampler2D), o que exige uma
        // textura de verdade. Sem stencil aqui - este G-buffer nao usa
        // stencil test para nada.
        glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthAttachment);
        glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, (GLsizei)m_Width, (GLsizei)m_Height,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);

        // Um unico color attachment (normal) - glDrawBuffers com 1 elemento
        // e redundante com o padrao (GL_COLOR_ATTACHMENT0 ja e o default),
        // mas deixado explicito aqui por clareza e para deixar obvio, se
        // este G-buffer ganhar mais anexos no futuro (ex: albedo, se a
        // engine migrar para deferred shading de verdade), onde adicionar
        // a entrada correspondente.
        GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, drawBuffers);

        PRISM_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                     "GeometryBuffer: framebuffer incompleto!");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void GeometryBuffer::BindForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void GeometryBuffer::Unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void GeometryBuffer::BindNormalForReading(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_NormalAttachment);
    }

    void GeometryBuffer::BindDepthForReading(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
    }

    void GeometryBuffer::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || width > 8192 || height > 8192) {
            PRISM_CORE_WARN("GeometryBuffer::Resize ignorado com tamanho invalido: ", width, "x", height);
            return;
        }
        if (width == m_Width && height == m_Height)
            return;

        m_Width = width;
        m_Height = height;
        Invalidate();
    }

}
