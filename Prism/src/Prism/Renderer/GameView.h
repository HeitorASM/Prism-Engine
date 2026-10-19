#pragma once

// ============================================================================
// GameView.h
// Desenha "o jogo" - a cena vista pela camera principal, com a UI in-game por
// cima - no framebuffer ATUALMENTE ligado. E o desenho compartilhado por
// todos os lugares que mostram o jogo rodando: a aba Game do editor, a janela
// de Play separada e o executavel PrismRuntime. Assim os tres sempre mostram
// exatamente a mesma imagem.
// ============================================================================

#include <cstdint>

namespace Prism {

    class Scene;

    class GameView {
    public:
        // Desenha 'scene' pela CameraComponent::Primary (opcoes de render da
        // propria camera - ver CameraRenderSettings) e a UI in-game por cima.
        //
        // O CHAMADOR liga o framebuffer de destino e define o viewport antes
        // (Framebuffer::Bind / Renderer::SetViewport). 'uiWidth'/'uiHeight' sao
        // o tamanho do layout da UI na MESMA unidade do mouse em Input (pixels
        // da janela; na aba Game, o tamanho da imagem). 'uiInteractive' liga
        // o clique em botoes (so quando o jogo captura o input).
        //
        // Sem camera principal limpa a tela e devolve false (a UI ainda e
        // desenhada); o chamador decide se avisa o usuario.
        static bool Render(Scene& scene, float uiWidth, float uiHeight, bool uiInteractive);
    };

}
