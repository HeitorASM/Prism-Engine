#include <glad/gl.h>
#include "SSAO.h"
#include "../Core/Log.h"
#include <random>

namespace Prism {

    SSAO::SSAO(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height) {
        GenerateKernelAndNoise();
        Invalidate();
    }

    SSAO::~SSAO() {
        glDeleteFramebuffers(1, &m_RawFBO);
        glDeleteFramebuffers(1, &m_BlurredFBO);
        glDeleteTextures(1, &m_RawTexture);
        glDeleteTextures(1, &m_BlurredTexture);
        glDeleteTextures(1, &m_NoiseTexture);
    }

    // Gera o kernel de amostras hemisfericas + a textura de ruido de
    // rotacao - chamado UMA VEZ no construtor (nao em Invalidate/Resize,
    // ja que nem o kernel nem o ruido dependem da resolucao da tela).
    void SSAO::GenerateKernelAndNoise() {
        // std::uniform_real_distribution em vez de rand()/RAND_MAX - evita
        // vies de distribuicao e da resultados independentes de plataforma
        // (RAND_MAX varia por implementacao da libc).
        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
        std::default_random_engine generator;

        for (int i = 0; i < KernelSize; i++) {
            // Vetor aleatorio numa hemisfera (Z sempre positivo - "para
            // cima" em tangent space, ver comentario em SSAO.h) - as
            // componentes X/Y vao de -1 a 1, Z de 0 a 1.
            glm::vec3 sample(
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator)
            );
            sample = glm::normalize(sample);
            sample *= randomFloats(generator);

            // Distribuicao nao-uniforme: concentra mais amostras PERTO da
            // origem (perto da superficie sendo sombreada) e menos longe
            // dela - interpolacao quadratica classica (LearnOpenGL/Crytek),
            // porque a oclusao proxima da superficie importa mais para o
            // resultado visual do que oclusao de geometria distante.
            float scale = (float)i / (float)KernelSize;
            scale = 0.1f + (scale * scale) * 0.9f; // lerp(0.1, 1.0, scale^2)
            sample *= scale;

            m_Kernel[i] = sample;
        }

        // Textura de ruido 4x4: vetores de rotacao aleatorios (so X/Y, Z=0
        // - a rotacao acontece no PLANO tangente) usados para girar o
        // kernel de amostras a cada pixel dentro do shader de SSAO,
        // trocando um padrao de banding visivel (se o mesmo kernel fosse
        // usado identico em todo pixel) por ruido de alta frequencia, que
        // o blur pass (RenderSSAOBlurPass) depois suaviza. 4x4 e o tamanho
        // classico desta tecnica - grande o suficiente para nao repetir
        // visivelmente dentro do raio de blur (tambem 4x4, ver
        // s_SSAOBlurFragmentSrc em Renderer.cpp), pequeno o suficiente para
        // nao gastar memoria/banda a toa (a textura e "tileada"/repetida
        // por toda a tela via GL_REPEAT).
        std::vector<glm::vec3> noise;
        noise.reserve(16);
        for (int i = 0; i < 16; i++) {
            noise.emplace_back(
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 2.0f - 1.0f,
                0.0f
            );
        }

        glCreateTextures(GL_TEXTURE_2D, 1, &m_NoiseTexture);
        glBindTexture(GL_TEXTURE_2D, m_NoiseTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // GL_REPEAT (nao CLAMP): a textura de ruido e deliberadamente
        // "ladrilhada" por toda a tela - ver comentario acima.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    void SSAO::Invalidate() {
        if (m_RawFBO) {
            glDeleteFramebuffers(1, &m_RawFBO);
            glDeleteFramebuffers(1, &m_BlurredFBO);
            glDeleteTextures(1, &m_RawTexture);
            glDeleteTextures(1, &m_BlurredTexture);
        }

        // AO e um valor ESCALAR (0..1) - GL_RED/GL_R8 usa 1 byte por pixel
        // em vez dos 4 de RGBA8, suficiente para o intervalo de valores e
        // mais barato em banda/memoria (este e mais um framebuffer inteiro
        // na resolucao da tela, desenhado 2x - bruto e blur - a cada
        // frame com SSAO ativo).
        auto createR8Framebuffer = [this](uint32_t& fbo, uint32_t& texture) {
            glCreateFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);

            glCreateTextures(GL_TEXTURE_2D, 1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)m_Width, (GLsizei)m_Height,
                         0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

            // Sem anexo de profundidade: estes framebuffers so desenham um
            // fullscreen quad sem depth test (ver Renderer::RenderSSAOPass/
            // RenderSSAOBlurPass) - nao ha nada para o depth buffer testar.
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_NONE);

            PRISM_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                         "SSAO: framebuffer incompleto!");
        };

        createR8Framebuffer(m_RawFBO, m_RawTexture);
        createR8Framebuffer(m_BlurredFBO, m_BlurredTexture);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void SSAO::BindRawForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, m_RawFBO);
        glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
    }

    void SSAO::BindBlurredForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, m_BlurredFBO);
        glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
    }

    void SSAO::Unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void SSAO::BindRawForReading(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RawTexture);
    }

    void SSAO::BindBlurredForReading(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_BlurredTexture);
    }

    void SSAO::BindNoiseForReading(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_NoiseTexture);
    }

    void SSAO::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || width > 8192 || height > 8192) {
            PRISM_CORE_WARN("SSAO::Resize ignorado com tamanho invalido: ", width, "x", height);
            return;
        }
        if (width == m_Width && height == m_Height)
            return;

        m_Width = width;
        m_Height = height;
        Invalidate(); // note: NAO regera o kernel/ruido - ver comentario em GenerateKernelAndNoise
    }

}
