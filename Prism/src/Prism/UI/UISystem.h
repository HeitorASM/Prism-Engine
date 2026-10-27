#pragma once

// ============================================================================
// UISystem.h
// UI in-game: desenha os UIDocumentComponent (.prismui) da cena por cima da imagem 3D e
// trata hover/clique dos botoes.
//
// - Layout e escala por resolucao: ver UI/UILayout.h e docs/ui-prismui.md.
// - Render(..., interactive=true) tambem le o mouse (Input) e dispara OnClick
//   (Lua/C++) quando o botao e solto sobre o mesmo botao em que foi apertado.
//   O editor desenha com interactive=false (so visualizacao).
// - Fonte: Prism/fonts/DejaVuSans.ttf, lida do disco e rasterizada com
//   stb_truetype num atlas (Latin-1).
// ============================================================================

#include <string>

namespace Prism {

    class Scene;
    class Entity;
    namespace Ui { struct Node; }

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

        // Elemento (por id) do UIDocumentComponent de 'entity', ou nullptr (sem componente,
        // documento invalido ou id inexistente). Carrega o arquivo se ainda nao foi lido.
        // O ponteiro vale ate o proximo Render/recarga: use na hora, nao guarde.
        static Ui::Node* FindDocumentNode(Entity entity, const std::string& id);

        // Libera a textura da fonte. Chamar antes de Renderer::Shutdown.
        static void Shutdown();

    private:
        static bool s_PointerOverUI;
    };

}
