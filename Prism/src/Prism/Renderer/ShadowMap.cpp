// Lembrete da regra de ouro do projeto: glad SEMPRE antes de qualquer coisa
// que toque OpenGL/GLFW (ver comentario completo em OpenGLContext.cpp).
#include <glad/gl.h>
#include "ShadowMap.h"
#include "../Core/Log.h"

namespace Prism {

    ShadowMap::ShadowMap(uint32_t resolution)
        : m_Resolution(resolution) {

        glCreateFramebuffers(1, &m_FBO);

        glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthTexture);
        glBindTexture(GL_TEXTURE_2D, m_DepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, (GLsizei)m_Resolution, (GLsizei)m_Resolution,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

        // Borda branca (profundidade = 1.0, o valor mais "longe" possivel)
        // - qualquer fragmento cuja posicao projetada no espaco da luz cai
        // FORA do frustum ortho (ver Renderer::RenderShadowPass) amostra
        // esta borda ao inves de repetir/espelhar a textura (o que criaria
        // sombra falsa nas bordas do frustum da luz). Com profundidade=1.0
        // na borda, a comparacao em CalculateShadow() (fragmento mais
        // "perto" que a borda) sempre da "sem sombra" fora do frustum -
        // exatamente o comportamento desejado (nada fora do alcance do
        // shadow map deveria ficar sombreado).
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTexture, 0);

        // Sem anexo de cor: diz explicitamente ao OpenGL que nenhum
        // draw/read buffer de cor sera usado - sem isso, alguns drivers
        // marcam o FBO como incompleto (GL_FRAMEBUFFER_UNSUPPORTED) por
        // faltar GL_COLOR_ATTACHMENT0, mesmo o proposito aqui sendo so
        // profundidade.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        PRISM_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                     "ShadowMap: framebuffer de profundidade incompleto!");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ShadowMap::~ShadowMap() {
        glDeleteFramebuffers(1, &m_FBO);
        glDeleteTextures(1, &m_DepthTexture);
    }

    void ShadowMap::BindForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glViewport(0, 0, (GLsizei)m_Resolution, (GLsizei)m_Resolution);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void ShadowMap::Unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ShadowMap::BindForReading(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_DepthTexture);
    }

}
