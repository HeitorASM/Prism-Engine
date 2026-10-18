#pragma once

// ============================================================================
// UISystem.h
// UI in-game: desenha os UIElementComponent da cena por cima da imagem 3D e
// trata hover/clique dos botoes.
//
// - Layout em pixels (sem escala por resolucao). Raizes: ancoram na tela
//   inteira, ordenadas por Layer; filhos (hierarquia) ancoram no retangulo do
//   pai e herdam a visibilidade dele. A ordem de desenho e em profundidade.
// - Render(..., interactive=true) tambem le o mouse (Input) e dispara OnClick
//   (Lua/C++) quando o botao e solto sobre o mesmo botao em que foi apertado.
//   O editor desenha com interactive=false (so visualizacao).
// - Fonte: Prism/fonts/DejaVuSans.ttf, lida do disco e rasterizada com
//   stb_truetype num atlas (Latin-1).
// ============================================================================

namespace Prism {

    class Scene;

    class UISystem {
    public:
        // Desenha a UI de 'scene' no framebuffer atual. 'layoutWidth/Height' sao
        // o tamanho (em pixels) do espaco de layout = area onde o mouse e medido.
        static void Render(Scene& scene, float layoutWidth, float layoutHeight, bool interactive);

        // O mouse esta sobre um elemento que barra o ponteiro? (resultado do
        // ultimo Render interativo).
        static bool IsPointerOverUI() { return s_PointerOverUI; }

        // Esquece botao apertado/hover (ao iniciar ou parar o Play).
        static void ResetInput();

        // Libera a textura da fonte. Chamar antes de Renderer::Shutdown.
        static void Shutdown();

    private:
        static bool s_PointerOverUI;
    };

}
