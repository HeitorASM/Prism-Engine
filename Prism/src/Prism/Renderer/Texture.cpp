// Lembrete da regra de ouro do projeto: glad SEMPRE antes de qualquer coisa
// que toque OpenGL/GLFW (ver comentario completo em OpenGLContext.cpp).
#include <glad/gl.h>
#include "Texture.h"
#include "../Core/Log.h"

// STB_IMAGE_IMPLEMENTATION deve aparecer em EXATAMENTE UM .cpp de todo o
// projeto (ver comentario grande em vendor/CMakeLists.txt, secao stb) -
// este e esse .cpp. Sem esta macro, stb_image.h so declara as funcoes
// (como um header comum); com ela, esta translation unit tambem GERA o
// corpo delas.
#define STB_IMAGE_IMPLEMENTATION
// stbi_set_flip_vertically_on_load(true) abaixo inverte a imagem no eixo Y
// ao carregar - arquivos PNG/JPG guardam a primeira linha de pixels como
// o TOPO da imagem, mas a convencao de coordenada de textura do OpenGL
// (V=0 na parte de baixo, ver comentario em MeshVertex::UV, Mesh.h) espera
// o oposto - sem inverter, toda textura apareceria de cabeca para baixo.
#include <stb_image.h>

namespace Prism {

    Texture2D::Texture2D(const std::string& path, bool isSRGB)
        : m_Path(path) {

        stbi_set_flip_vertically_on_load(true);

        int width = 0, height = 0, channelsInFile = 0;
        // Forcamos 4 canais (RGBA) sempre, mesmo que o arquivo original
        // tenha so 3 (RGB, sem alpha) - simplifica o resto desta funcao
        // (sempre GL_RGBA8/GL_SRGB8_ALPHA8, nunca precisa checar
        // channelsInFile para decidir o formato) ao custo de,
        // ocasionalmente, gastar um pouco mais de memoria de GPU do que o
        // estritamente necessario para uma textura RGB pura - aceitavel
        // para o volume de texturas que esta engine carrega hoje.
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channelsInFile, 4);

        if (!pixels) {
            PRISM_CORE_ERROR("Texture2D: falha ao carregar '", path, "': ", stbi_failure_reason());
            return; // m_RendererID fica 0 - ver IsValid()
        }

        m_Width = (uint32_t)width;
        m_Height = (uint32_t)height;

        // glCreateTextures is only available on newer GL (4.5+). Fallback
        // para glGenTextures/glBindTexture quando a funcao nao existir
        // (drivers/HW mais antigos). Usar diretamente glCreateTextures
        // sem checar pode levar a ponteiro nulo e crash.
        if (glCreateTextures) {
            glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
            glBindTexture(GL_TEXTURE_2D, m_RendererID);
        } else {
            glGenTextures(1, &m_RendererID);
            glBindTexture(GL_TEXTURE_2D, m_RendererID);
        }

        // Ver comentario grande em Texture.h (parametro 'isSRGB') sobre a
        // diferenca entre GL_SRGB8_ALPHA8 (albedo/cor) e GL_RGBA8 (dados
        // lineares - normal/roughness/metallic).
        GLenum internalFormat = isSRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D); // trilinear/anisotropico dependem de mipmaps existirem - gerados uma vez aqui, nunca recalculados depois

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        stbi_image_free(pixels);
    }

    Texture2D::~Texture2D() {
        if (m_RendererID)
            glDeleteTextures(1, &m_RendererID);
    }

    void Texture2D::Bind(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
    }

}
