#pragma once

// ============================================================================
// SSAO.h
// Screen-Space Ambient Occlusion - escurece reentrancias/cantos da cena
// (onde geometria proxima bloqueia luz ambiente) usando so os dados de
// tela (profundidade + normal), sem precisar de raytracing nem geometria
// extra - a tecnica classica popularizada pela Crysis (2007).
//
// Este arquivo encapsula APENAS o armazenamento (2 framebuffers R8 -
// resultado bruto + resultado apos blur - mais o kernel de amostras e a
// textura de ruido usados pelo shader). O CALCULO em si (os shaders GLSL
// de SSAO e blur) fica em Renderer.cpp junto dos outros shaders da engine
// (RenderSSAOPass/RenderSSAOBlurPass), seguindo o mesmo padrao ja usado
// para shadow mapping (ShadowMap.h/.cpp guarda so o framebuffer,
// Renderer.cpp tem a logica de RenderShadowPass).
//
// PIPELINE COMPLETO (ver Renderer::DrawScene para a orquestracao):
//   1) RenderGeometryPrePass preenche um GeometryBuffer (normal view-space
//      + depth) do ponto de vista da CAMERA PRINCIPAL (nao da luz, ao
//      contrario do ShadowMap).
//   2) RenderSSAOPass (usa esta classe): le o GeometryBuffer, calcula
//      oclusao por pixel usando um kernel de amostras hemisfericas +
//      textura de ruido, escreve um escalar (0=sem oclusao, 1=totalmente
//      ocluido) no primeiro framebuffer desta classe.
//   3) RenderSSAOBlurPass: box blur simples sobre o resultado de (2),
//      escreve no segundo framebuffer desta classe - necessario porque a
//      textura de ruido em (2) introduz um padrao granulado visivel sem
//      isto.
//   4) O pass de cor final amostra o resultado de (3) (u_AOMap no shader
//      principal) e multiplica no termo de luz ambiente.
// ============================================================================

#include "../Core/Base.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <array>

namespace Prism {

    class SSAO {
    public:
        // Numero de amostras do kernel hemisferico - 16 e um meio-termo
        // comum entre qualidade (mais amostras = oclusao mais suave, menos
        // ruido residual apos o blur) e custo (cada amostra e uma leitura
        // de textura de profundidade + calculos trigonometricos dentro do
        // shader de SSAO, que ja roda em resolucao de tela inteira).
        static constexpr int KernelSize = 16;

        SSAO(uint32_t width, uint32_t height);
        ~SSAO();

        SSAO(const SSAO&) = delete;
        SSAO& operator=(const SSAO&) = delete;

        // Bind do framebuffer de resultado BRUTO (antes do blur) como alvo
        // de escrita - usado por Renderer::RenderSSAOPass.
        void BindRawForWriting() const;

        // Bind do framebuffer de resultado APOS o blur como alvo de
        // escrita - usado por Renderer::RenderSSAOBlurPass.
        void BindBlurredForWriting() const;

        void Unbind() const;

        // Bind da textura BRUTA (pre-blur) como sampler2D - usada pelo
        // proprio RenderSSAOBlurPass como ENTRADA (le o resultado bruto,
        // escreve o resultado suavizado no outro framebuffer).
        void BindRawForReading(uint32_t slot) const;

        // Bind da textura JA SUAVIZADA (pos-blur) como sampler2D - esta e
        // a que o pass de cor final (u_AOMap) efetivamente usa.
        void BindBlurredForReading(uint32_t slot) const;

        // Bind da textura de ruido 4x4 (ver comentario em m_NoiseTexture)
        // como sampler2D - usada so por RenderSSAOPass.
        void BindNoiseForReading(uint32_t slot) const;

        const std::array<glm::vec3, KernelSize>& GetKernel() const { return m_Kernel; }

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

        // Mesma logica de Framebuffer::Resize/GeometryBuffer::Resize -
        // ambos os framebuffers (bruto e suavizado) acompanham o tamanho
        // da viewport atual.
        void Resize(uint32_t width, uint32_t height);

    private:
        void Invalidate();
        void GenerateKernelAndNoise();

        uint32_t m_RawFBO = 0, m_RawTexture = 0;
        uint32_t m_BlurredFBO = 0, m_BlurredTexture = 0;
        uint32_t m_NoiseTexture = 0;
        uint32_t m_Width, m_Height;

        // Kernel de vetores hemisfericos em TANGENT SPACE (Z sempre >= 0,
        // "para cima" saindo da superficie) - ver GenerateKernelAndNoise em
        // SSAO.cpp para como sao gerados (distribuicao mais densa perto da
        // origem, tecnica padrao de SSAO desde o tutorial classico da
        // Crytek/LearnOpenGL). Calculado uma vez em Init(), nunca muda
        // depois - nao ha necessidade de regerar por frame.
        std::array<glm::vec3, KernelSize> m_Kernel{};
    };

}
