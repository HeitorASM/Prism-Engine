#pragma once

// ============================================================================
// Texture.h
// Carrega um arquivo de imagem (PNG/JPG/BMP/TGA - qualquer formato que
// stb_image suporte, ver Texture.cpp) do disco e sobe para a GPU como uma
// GL_TEXTURE_2D comum, pronta para ser amostrada (sampler2D) por qualquer
// shader - usado pelo sistema de Material (MaterialComponent, Components.h)
// para as texturas de albedo/normal/roughness-metallic.
//
// SEM CACHE nesta classe (de proposito): cada Texture2D e uma textura de
// GPU independente, mesmo que duas instancias carreguem o MESMO arquivo -
// deduplicar carregamentos repetidos do mesmo path e responsabilidade de
// quem CONSTROI Texture2D (ver Renderer::GetOrLoadTexture, Renderer.cpp),
// nao desta classe - Texture2D em si e deliberadamente "burra": um
// wrapper fino de RAII sobre um unico glGenTextures/glDeleteTextures, sem
// saber nada sobre outras texturas que possam existir na engine.
// ============================================================================

#include "../Core/Base.h"
#include <string>
#include <cstdint>

namespace Prism {

    class Texture2D {
    public:
        // Carrega 'path' do disco - se o arquivo nao existir ou nao puder
        // ser decodificado (formato nao suportado, arquivo corrompido),
        // NAO lanca excecao nem crasha: a textura fica marcada invalida
        // (ver IsValid()) e um PRISM_CORE_ERROR e logado (ver Texture.cpp).
        // Isso e proposital - um Material com um path de textura quebrado
        // (ex: arquivo movido/deletado depois de configurado) nao deveria
        // derrubar a engine inteira, so desenhar sem aquela textura
        // especifica (ver Renderer::DrawMesh, que trata IsValid()==false
        // como "sem esta textura", igual a nao ter nenhum path configurado).
        //
        // 'isSRGB' controla o formato interno da textura na GPU
        // (GL_SRGB8_ALPHA8 vs GL_RGBA8) - IMPORTANTE para PBR correto:
        // texturas de ALBEDO/COR sao tipicamente pintadas/fotografadas em
        // espaco sRGB (o mesmo espaco de cor que monitores/telas usam) e
        // precisam desse flag =true para a GPU converter para linear
        // automaticamente antes da iluminacao ser calculada (ver
        // s_FragmentSrc, Renderer.cpp) - sem isso, os tons ficam
        // "lavados"/incorretos porque o shader faria matematica de
        // iluminacao LINEAR sobre valores ainda em sRGB. Mapas de
        // NORMAL/ROUGHNESS/METALLIC sao dados tecnicos (nao cor
        // percebida) e devem ficar com isSRGB=false (dado linear puro).
        explicit Texture2D(const std::string& path, bool isSRGB);
        ~Texture2D();

        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;

        void Bind(uint32_t slot) const;

        bool IsValid() const { return m_RendererID != 0; }

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        const std::string& GetPath() const { return m_Path; }

        // ID nativo da textura OpenGL - exposto so para o editor poder
        // desenhar um preview/miniatura via ImGui::Image (que espera um
        // ImTextureID, tipicamente o RendererID convertido - ver
        // EditorLayer::RenderPropertiesPanel, mesmo padrao usado pelo
        // color attachment do Framebuffer na viewport principal). Nao
        // usado por Renderer/DrawMesh (que ja usa Bind() para isso).
        uint32_t GetRendererID() const { return m_RendererID; }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_Width = 0, m_Height = 0;
        std::string m_Path;
    };

}
