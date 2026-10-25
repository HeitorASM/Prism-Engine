# UI in-game

A interface do **jogo** (placar, menus, barra de vida) é feita de entidades com o component **UI Element**. Ela é desenhada por cima da cena, na janela de Play, e aparece (só para visualizar) na viewport do editor.

> Formato novo em texto: [ui-prismui.md](../ui-prismui.md).

Crie pelo menu **Entidade → Criar UI**: **Panel**, **Texto**, **Botão** ou **Imagem**.

## Tipos

| Tipo | Uso |
|---|---|
| **Panel** | Retângulo de cor (ou com textura) |
| **Texto** | Texto; `\n` quebra a linha (não há quebra automática) |
| **Imagem** | Uma textura (arraste do Conteúdo do Projeto), com uma cor de tint |
| **Botão** | Retângulo clicável com rótulo; muda de cor com o mouse em cima e ao apertar |

## Posicionamento

Tudo em **pixels** (sem escala automática por resolução).

- **Âncora**: onde o elemento se prende no pai ou na tela: 9 posições (cantos, bordas, centro) ou **Preencher o pai**.
- **Deslocamento**: a margem para **dentro**, a partir das bordas ancoradas. `Topo esquerda (16, 16)` fica a 16 px do topo e da esquerda. Em eixos centralizados é o deslocamento a partir do centro.
- **Tamanho**: largura e altura (ignorado em Preencher o pai, que usa a margem).
- **Hierarquia**: um elemento **filho** de outro elemento de UI se ancora dentro do retângulo do pai e some junto com ele. Arraste na Hierarquia para aninhar (ex.: botões dentro de um Panel).
- **Camada**: entre elementos raiz, maior fica por cima.

O Transform da entidade é ignorado pela UI.

## Botões e cliques

O clique dispara quando o botão do mouse é **solto sobre o mesmo botão** em que foi apertado. Para reagir, ponha um script na entidade do botão:

```lua
function OnClick()
    log("clicou!")
end
```

Em C++, sobrescreva `OnClick()`. Com o cursor travado (`CursorMode.Locked`) a UI não recebe o mouse.

**Barra o mouse**: Panels, Imagens e Botões "seguram" o mouse; Textos não. Use `UI.IsPointerOverUI()` para não atirar ou mirar quando o mouse está sobre a interface:

```lua
if Input.IsMouseButtonDown(Mouse.Left) and not UI.IsPointerOverUI() then
    -- ação do jogo
end
```

## Mudando a UI por script

```lua
local ui = Scene.Find("Placar"):GetUI()
ui.Text = "Moedas: 3"
ui:SetColor(1, 0.8, 0, 1)        -- r, g, b, a (0 a 1)
ui.Visible = false               -- esconde (e aos filhos)
```

Campos: `Visible`, `Text`, `FontSize`, `Interactable`, `Layer`, `TexturePath`; funções `SetOffset(x, y)`, `SetSize(w, h)`, `SetColor(r,g,b,a)`, `SetTextColor(r,g,b,a)`.

## Fonte

A fonte é a DejaVu Sans (cobre letras latinas com acentos). Fica em `fonts/DejaVuSans.ttf`, ao lado do executável do editor.
