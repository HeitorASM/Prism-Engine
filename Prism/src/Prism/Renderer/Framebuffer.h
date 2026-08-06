#pragma once

// ============================================================================
// Framebuffer.h
// Framebuffer OpenGL usado para renderizar a cena fora da tela (offscreen),
// para depois desenhar o resultado (uma textura de cor) dentro do painel
// "Viewport" do editor via ImGui::Image. E assim que praticamente todo
// editor de engine (Unity, Unreal, Godot, Hazel...) mostra a cena 3D dentro
// de uma janela de UI que pode ser docada/redimensionada livremente.
//
// Uso tipico dentro de uma Layer:
//   m_Framebuffer = Framebuffer::Create({ 1280, 720 });
//   ...
//   m_Framebuffer->Bind();
//   Renderer::Clear();
//   // ... desenhar a cena aqui ...
//   m_Framebuffer->Unbind();
//   ImGui::Image((ImTextureID)(uintptr_t)m_Framebuffer->GetColorAttachmentID(), size);
// ============================================================================

#include "../Core/Base.h"
#include <cstdint>

namespace Prism {

    struct FramebufferSpecification {
        uint32_t Width = 1280;
        uint32_t Height = 720;
    };

    class Framebuffer {
    public:
        Framebuffer(const FramebufferSpecification& spec);
        ~Framebuffer();

        // Nao copiavel: possui recursos de GPU (IDs de textura/framebuffer).
        Framebuffer(const Framebuffer&) = delete;
        Framebuffer& operator=(const Framebuffer&) = delete;

        void Bind();
        void Unbind();

        // Recria as texturas internas com o novo tamanho. Chamado sempre que
        // o painel Viewport do editor muda de tamanho. Ignora tamanhos 0 ou
        // negativos (pode acontecer por um frame quando a janela e minimizada).
        void Resize(uint32_t width, uint32_t height);

        uint32_t GetColorAttachmentID() const { return m_ColorAttachment; }
        const FramebufferSpecification& GetSpecification() const { return m_Specification; }

        static Scope<Framebuffer> Create(const FramebufferSpecification& spec);

    private:
        void Invalidate();

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_ColorAttachment = 0;
        uint32_t m_DepthAttachment = 0;
        FramebufferSpecification m_Specification;
    };

}
