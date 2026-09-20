#pragma once

// ============================================================================
// GeometryBuffer.h
// Framebuffer com DUAS saidas (MRT - Multiple Render Targets), usado
// exclusivamente pelo pre-pass de geometria do SSAO (ver
// Renderer::RenderGeometryPrePass em Renderer.cpp):
//   - Anexo 0: normal da superficie em VIEW-SPACE (espaco da camera
//     principal, nao world-space) - por que view-space e nao world-space:
//     o calculo de SSAO (Renderer::RenderSSAOPass) reconstroi a posicao de
//     cada pixel a partir da profundidade fazendo unproject com a matriz
//     de PROJECAO da camera, o que naturalmente da a posicao em
//     view-space, nao world-space - manter a normal no mesmo espaco evita
//     ter que multiplicar por matrizes extras (view/inversa) dentro do
//     shader de SSAO, que roda por-pixel e ja tem custo suficiente com o
//     kernel de amostras.
//   - Anexo 1 (profundidade): igual ao Framebuffer normal (Framebuffer.h),
//     usado pelo shader de SSAO para reconstruir a posicao 3D de cada
//     pixel.
//
// DELIBERADAMENTE SEPARADO de Framebuffer (Framebuffer.h) por dois
// motivos: (1) Framebuffer sempre cria exatamente 1 anexo de cor (RGBA8,
// pensado para ser mostrado via ImGui::Image) + depth/stencil combinado -
// misturar o caso de "2 anexos de cor, um deles float, sem stencil" via
// flags condicionais tornaria a classe mais dificil de entender para o
// caso comum (que e a maioria dos usos); (2) este e um recurso PRIVADO do
// Renderer (ninguem de fora do motor de renderizacao chama
// GetNormalAttachmentID/GetDepthAttachmentID diretamente), enquanto
// Framebuffer e usado por qualquer Layer/janela do editor.
//
// Assim como o ShadowMap (ver ShadowMap.h), este recurso e alocado sob
// demanda (so quando alguma cena efetivamente usa SSAO) e redimensionado
// via Resize() seguindo o mesmo padrao ja usado por Framebuffer::Resize -
// a resolucao acompanha a da viewport ATUAL sendo desenhada (ao contrario
// do ShadowMap, que tem resolucao fixa independente da camera).
// ============================================================================

#include "../Core/Base.h"
#include <cstdint>

namespace Prism {

    class GeometryBuffer {
    public:
        GeometryBuffer(uint32_t width, uint32_t height);
        ~GeometryBuffer();

        GeometryBuffer(const GeometryBuffer&) = delete;
        GeometryBuffer& operator=(const GeometryBuffer&) = delete;

        // Bind como alvo de escrita (habilita os 2 anexos de cor via
        // glDrawBuffers + glViewport para o tamanho deste buffer) - chamar
        // antes do pre-pass de geometria (ver Renderer::RenderGeometryPrePass).
        void BindForWriting() const;

        // Restaura o framebuffer padrao (tela/FBO 0). Assim como
        // ShadowMap::Unbind, NAO restaura o glViewport sozinho - quem
        // chama (Renderer::DrawScene) e responsavel por isso, ja que so
        // o chamador sabe qual era o viewport antes.
        void Unbind() const;

        // Bind da textura de normal (view-space) como sampler2D no slot dado.
        void BindNormalForReading(uint32_t slot) const;

        // Bind da textura de profundidade como sampler2D no slot dado.
        void BindDepthForReading(uint32_t slot) const;

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

        // Recria as texturas internas com o novo tamanho - identico em
        // espirito a Framebuffer::Resize (mesma logica de "ignorar
        // tamanhos invalidos" e "no-op se o tamanho nao mudou").
        void Resize(uint32_t width, uint32_t height);

    private:
        void Invalidate();

        uint32_t m_FBO = 0;
        uint32_t m_NormalAttachment = 0; // RGB16F, view-space
        uint32_t m_DepthAttachment = 0;  // GL_DEPTH_COMPONENT24 (textura, nao renderbuffer - precisa ser amostravel)
        uint32_t m_Width, m_Height;
    };

}
