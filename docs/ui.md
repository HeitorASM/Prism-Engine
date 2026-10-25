# UI in-game

> **Novo:** prefira o formato em texto `.prismui` (um arquivo + uma entidade, layout flexível, escala por resolução, hot reload) — veja [ui-prismui.md](ui-prismui.md). O component `UI Element` descrito aqui continua funcionando, mas é o formato antigo.

A UI in-game é feita de entidades com o component **UI Element** (menu *Entidade → Criar UI*, ou *+ Add Component*). Ela é desenhada por cima da cena, na janela de Play e (só para visualização) no viewport do editor.

## Tipos

| Tipo | Uso |
|------|-----|
| Panel | retângulo de cor (ou com textura) |
| Texto | texto UTF-8 (Latin-1 coberto), `\n` quebra linha, sem quebra automática |
| Imagem | textura (arraste do Conteúdo do Projeto) com tint `Cor` |
| Botão | retângulo clicável com rótulo; cor muda com mouse em cima/apertado |

## Layout

Tudo em **pixels** (sem escala por resolução): no Play, o tamanho da janela; no editor, o do viewport.

- **Âncora**: 9 posições (cantos, bordas, centro) ou *Preencher o pai*.
- **Deslocamento**: margem para dentro a partir das bordas ancoradas (`TopLeft (16,16)` fica a 16 px do topo e da esquerda; `BottomRight (16,16)` a 16 px do fundo e da direita). Em eixos centralizados é o deslocamento a partir do centro. Em *Preencher o pai* é a margem de cada lado.
- **Hierarquia**: um elemento filho de outro elemento de UI se ancora dentro do retângulo do pai e some junto com ele (`Visível` do pai). Arraste na hierarquia para aninhar.
- **Camada**: ordem entre elementos raiz (maior = por cima). Dentro de uma árvore, a ordem é a da hierarquia.

A posição/escala do `Transform` da entidade é ignorada pela UI.

## Interação

- Botões reagem ao mouse esquerdo; o clique dispara quando o botão é **solto sobre o mesmo botão** em que foi apertado. O elemento mais acima que barra o mouse vence.
- Com o cursor travado (`Input.SetCursorMode(CursorMode.Locked)`) a UI não recebe mouse.
- `Barra o mouse`: Panels/Imagens/Botões barram o ponteiro; Texto não.

### Lua

```lua
function OnClick()                       -- no script da entidade do botão
    print("clicou!")
end

function OnUpdate(dt)
    if Input.IsMouseButtonDown(Mouse.Left) and not UI.IsPointerOverUI() then
        -- tiro, seleção etc. só quando o mouse NÃO está sobre a UI
    end
end

local ui = Scene.Find("Placar"):GetUI()
ui.Text = "Moedas: 3"
ui:SetColor(1, 0.8, 0, 1)
```

`GetUI()` expõe `Visible`, `Text`, `FontSize`, `Interactable`, `Layer`, `TexturePath`, `SetOffset(x, y)`, `SetSize(w, h)`, `SetColor(r,g,b,a)`, `SetTextColor(r,g,b,a)`.

### C++

`ScriptableEntity::OnClick()` (API nativa **v3**: recompile módulos antigos).

## Fonte

DejaVu Sans, lida direto de `Prism/fonts/DejaVuSans.ttf` (o build copia a pasta `fonts/` para junto do executável) e rasterizada na primeira vez que a UI é desenhada, com `stb_truetype` (Latin-1). Para usar outra fonte, troque esse `.ttf` (mantendo o nome) ou mude o nome em `UISystem.cpp`.

## Formato

Component `UI Element`: cena v19 / prefab v7 (ver [formatos](formatos-de-arquivo.md)). Arquivos antigos não são migrados.
