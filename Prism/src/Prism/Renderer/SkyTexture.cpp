#include <glad/gl.h>
#include "SkyTexture.h"
#include "../Core/Log.h"
#include <stb_image.h> // so as declaracoes: STB_IMAGE_IMPLEMENTATION vive em Texture.cpp

namespace Prism {

    SkyTexture::SkyTexture(const std::string& absolutePath) {
        m_IsHDR = stbi_is_hdr(absolutePath.c_str()) != 0;

        // O flip e estado global do stb e Texture2D o liga antes de cada carga;
        // aqui precisa estar desligado (linha 0 = topo). Texture2D religa o seu.
        stbi_set_flip_vertically_on_load(false);

        int width = 0, height = 0, channels = 0;
        float* pixels = stbi_loadf(absolutePath.c_str(), &width, &height, &channels, 3);
        if (!pixels) {
            PRISM_CORE_ERROR("SkyTexture: falha ao carregar '", absolutePath, "': ", stbi_failure_reason());
            return;
        }

        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);        // costura do panorama
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // polos

        stbi_image_free(pixels);
    }

    SkyTexture::~SkyTexture() {
        if (m_RendererID)
            glDeleteTextures(1, &m_RendererID);
    }

    void SkyTexture::Bind(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
    }

}
