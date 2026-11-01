# Desenho imediato em C++ (`DrawContext`)

API para desenhar **linhas, caixas, esferas, setas, eixos, textos e retângulos** direto do código, sem criar entidades: ideal para debug (raios, áreas, alvos), HUD de desenvolvimento e marcadores no mundo. Funciona na viewport do editor, na janela de Play e no jogo exportado.

```cpp
#include <Prism/Renderer/DrawContext.h>   // dentro da engine

DrawContext::Line({0, 0, 0}, {0, 2, 0}, {1, 0, 0, 1});            // dura 1 quadro: chame todo quadro
DrawContext::Sphere(hit.Point, 0.1f, {1, 1, 0, 1}, 2.0f);         // fica 2 segundos
DrawContext::BoxAABB(min, max, {0, 1, 0, 1});
DrawContext::Text3D(pos + glm::vec3(0, 1, 0), "Inimigo", {1, 1, 1, 1});
DrawContext::Text2D("FPS: 60", 8, 8, {1, 1, 1, 1});               // tela, em pixels
```

## Em scripts C++ do projeto

Os mesmos desenhos estão em `ScriptableEntity` (API nativa v13):

```cpp
void OnUpdate(float dt) override {
    auto hit = RaycastMasked(origin, dir, 20.0f, ~0u);
    DrawLine(origin, hit.Hit ? hit.Point : origin + dir * 20.0f, {1, 0, 0, 1});
    if (hit.Hit) DrawSphere(hit.Point, 0.1f, {1, 1, 0, 1});
    DrawText2D("Distancia: " + std::to_string(hit.Distance), 8, 8, {1, 1, 1, 1});
}
```

Funções: `DrawLine`, `DrawBox(model)`, `DrawBoxAABB`, `DrawSphere`, `DrawArrow`, `DrawAxes(model, size)`, `DrawText3D`, `DrawLine2D`, `DrawRect2D`, `DrawText2D`. Cores são rgba "de tela" (como a UI e as partículas).

> Subir para a API v13 exige **recompilar** os scripts C++ dos projetos existentes (módulos de outra versão são recusados ao carregar).

## Parâmetros comuns

| Parâmetro | Significado |
|---|---|
| `duration` | `0` = um quadro (o padrão); `> 0` = fica esse tempo em segundos (máx. 1 h) |
| `depthTest` | `true` = objetos na frente escondem o desenho; `false` = sempre por cima |
| `thickness` | espessura em **pixels** (1 a 32), constante com a distância |

Mundo: `Line`, `Box`, `BoxAABB`, `Sphere`, `Arrow`, `Axes`, `Text3D` (texto sempre de frente para a câmera; `heightMeters` = altura de uma linha). Tela (pixels, origem no canto superior esquerdo): `Line2D`, `Rect2D` (cheio ou só a borda), `Text2D`.

## Como funciona

- As funções só **acumulam comandos** (`DrawCommandBuffer`). O `Application` chama `NewFrame(dt)` no início de cada quadro, que remove os de 1 quadro do quadro anterior e conta o tempo dos temporizados.
- O `Renderer::DrawScene` desenha os de mundo depois das partículas (blend, sem escrever profundidade); o `GameView` desenha os de tela por cima da UI. Valem para toda câmera que desenha o quadro.
- Linhas são quads expandidos no vertex shader (`draw_line.vert`) a partir de um SSBO: espessura em pixels real (o `glLineWidth` não é confiável) e funcionam em qualquer contexto GL, inclusive a janela de Play.
- Dados inválidos (NaN/inf, raio ≤ 0, texto vazio) são descartados; há um teto de 100 000 comandos por quadro/vida (o excedente é ignorado). `DrawContext::SetEnabled(false)` desliga tudo (as chamadas viram no-ops).

## Limitações

- Só no thread principal; os desenhos não aparecem em capturas de shadow maps.
- Textos usam a fonte da UI (Latin-1), sem negrito/estilos.
- Cores passam pelo pós-processamento como as partículas (bloom/exposição podem alterá-las no mundo).
