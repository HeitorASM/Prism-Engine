#pragma once

// ============================================================================
// ShadowMap.h
// Framebuffer especializado, SO DE PROFUNDIDADE (sem anexo de cor), usado
// para renderizar a cena do ponto de vista de uma luz - a tecnica classica
// de "shadow mapping" (ver Renderer::RenderShadowPass em Renderer.cpp para
// o pipeline completo de 2 passes: 1) desenhar profundidade aqui, 2) no
// pass de cor normal, comparar a profundidade de cada fragmento contra o
// que esta gravado aqui para decidir se esta na sombra).
//
// Deliberadamente SEPARADO de Framebuffer (Framebuffer.h) em vez de
// reaproveitado: Framebuffer sempre cria um anexo de cor (RGBA8) + um de
// profundidade combinado com stencil (GL_DEPTH24_STENCIL8), pensado para
// ser lido como IMAGEM (ImGui::Image). ShadowMap so tem profundidade pura
// (GL_DEPTH_COMPONENT, sem stencil, sem canal de cor) e e lido como
// TEXTURA DE COMPARACAO dentro do shader (sampler2DShadow-like via
// GL_TEXTURE_COMPARE_MODE) - misturar os dois casos dentro da mesma classe
// exigiria flags condicionais espalhados pelo Framebuffer existente sem
// necessidade real.
//
// Esta e a implementacao mais simples que funciona: UM shadow map, UMA
// luz Directional projetando sombra por vez (ver comentario grande em
// Renderer::RenderShadowPass sobre o motivo de nao suportar Point/Spot
// nem multiplas luzes com sombra ainda). Cascaded Shadow Maps (CSM -
// varios ShadowMap encadeados por distancia da camera, usados por engines
// AAA para cenas grandes) fica como evolucao natural se/quando cenas desta
// engine precisarem: a API publica desta classe (Bind/GetLightSpaceMatrix)
// ja e compativel com "um ShadowMap por cascata" no futuro, sem quebrar
// nada.
// ============================================================================

#include "../Core/Base.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace Prism {

    class ShadowMap {
    public:
        // 'resolution' e a largura E altura (quadrada) da textura de
        // profundidade em texels. 2048 e um meio-termo razoavel entre
        // qualidade de borda de sombra e memoria de GPU (2048x2048 x 32
        // bits ~= 16MB) - generoso o bastante para nao serrilhar
        // visivelmente nas cenas de portfolio que esta engine desenha
        // hoje, sem ir para os 4096+ que engines AAA usam para cenas
        // muito maiores.
        explicit ShadowMap(uint32_t resolution = 2048);
        ~ShadowMap();

        ShadowMap(const ShadowMap&) = delete;
        ShadowMap& operator=(const ShadowMap&) = delete;

        // Bind como alvo de escrita (glViewport para o tamanho do shadow
        // map + bind do FBO) - chamar antes de desenhar a cena com o
        // shader de profundidade (ver Renderer::RenderShadowPass).
        void BindForWriting() const;

        // Restaura o framebuffer padrao (tela/FBO 0) - chamar depois do
        // pass de profundidade, antes de continuar com o pass de cor
        // normal (que tem seu proprio glViewport/Framebuffer::Bind, ver
        // Renderer::DrawScene).
        void Unbind() const;

        // Bind da textura de profundidade como sampler2D no slot de
        // textura dado (glActiveTexture(GL_TEXTURE0 + slot)) - chamado
        // pelo pass de cor antes de desenhar cada mesh, para o shader
        // poder amostrar u_ShadowMap (ver s_FragmentSrc, CalculateShadow).
        void BindForReading(uint32_t slot) const;

        uint32_t GetResolution() const { return m_Resolution; }

        // Matriz combinada Projection * View da LUZ (nao da camera) -
        // calculada uma vez por frame em Renderer::RenderShadowPass (ver
        // la o motivo de ser ortho, nao perspective) e reusada tanto para
        // desenhar a profundidade aqui quanto, depois, dentro do pass de
        // cor (uniform u_LightSpaceMatrix), para transformar a posicao de
        // mundo de cada fragmento para o espaco da luz e comparar com o
        // valor gravado neste shadow map.
        const glm::mat4& GetLightSpaceMatrix() const { return m_LightSpaceMatrix; }
        void SetLightSpaceMatrix(const glm::mat4& matrix) { m_LightSpaceMatrix = matrix; }

    private:
        uint32_t m_FBO = 0;
        uint32_t m_DepthTexture = 0;
        uint32_t m_Resolution;
        glm::mat4 m_LightSpaceMatrix{ 1.0f };
    };

}
