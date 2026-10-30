#pragma once

// ============================================================================
// SkyTexture.h
// Panorama equirretangular para o skybox. Diferente de Texture2D (8 bits):
// guarda float de 16 bits LINEAR (GL_RGB16F), para .hdr manter a faixa
// dinamica. Imagens comuns (png/jpg...) tambem entram, convertidas para linear
// pelo stb. Nao inverte verticalmente: a linha 0 da imagem e o topo do ceu.
// ============================================================================

#include <cstdint>
#include <string>

namespace Prism {

    class SkyTexture {
    public:
        // Nunca lanca: falha (arquivo ausente/corrompido) deixa IsValid() == false e loga.
        explicit SkyTexture(const std::string& absolutePath);
        ~SkyTexture();

        SkyTexture(const SkyTexture&) = delete;
        SkyTexture& operator=(const SkyTexture&) = delete;

        void Bind(uint32_t slot) const;
        bool IsValid() const { return m_RendererID != 0; }
        bool IsHDR() const { return m_IsHDR; }

    private:
        uint32_t m_RendererID = 0;
        bool m_IsHDR = false;
    };

}
