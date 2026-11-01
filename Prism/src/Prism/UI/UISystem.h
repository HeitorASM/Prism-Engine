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

#include "../Renderer/Renderer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Prism {

    class Scene;
    class Entity;
    namespace Ui { struct Node; }

    class UISystem {
    public:
        // Desenha a UI de 'scene' no framebuffer atual. 'layoutWidth/Height' sao
        // o tamanho (em pixels) do espaco de layout = area onde o mouse e medido.
        static void Render(Scene& scene, float layoutWidth, float layoutHeight, bool interactive);

        // UI em ESPACO DE MUNDO (UIDocumentComponent::WorldSpace): desenha os paineis no mundo com a
        // view/projection da camera. Chamado pelo Renderer::DrawScene depois das particulas.
        static void RenderWorld(Scene& scene, const float* view, const float* projection, const float* cameraWorldPos);

        // Entrada dos paineis de mundo (so no Play): lanca o raio do mouse (ou do centro da tela com
        // o cursor travado) contra os canvas, converte o acerto em pixels do canvas e alimenta a mesma
        // logica de hover/clique da UI de tela (botoes, OnClick Lua/C++). O painel mais proximo vence e
        // um objeto na frente (Scene::VisualRaycast) o esconde. 'pointerAllowed' = false quando a UI de
        // tela ja esta sob o mouse. Chamar DEPOIS de Render(interactive=true) no mesmo quadro.
        // 'layoutWidth/Height' = o mesmo espaco do mouse usado em Render.
        static void UpdateWorldInteraction(Scene& scene, const float* view, const float* projection,
                                           float layoutWidth, float layoutHeight, bool pointerAllowed);

        // Texto da fonte da UI para quem desenha por conta propria (DrawContext): gera os quads (modo
        // glifo) de 'utf8' com o canto superior esquerdo em (x, y); 'align' 0 esq, 1 centro, 2 dir dentro
        // de uma caixa de largura 'boxWidth'. Vazio se a fonte nao carregou.
        static void BuildTextQuads(std::vector<Renderer::UIQuad>& out, const std::string& utf8, float x, float y,
                                   float fontSize, int align, float boxWidth, const glm::vec4& color);
        static float MeasureText(const std::string& utf8, float fontSize);
        // Textura (R8) do atlas da fonte, carregando-a se preciso. 0 se indisponivel.
        static uint32_t GetFontTexture();

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
