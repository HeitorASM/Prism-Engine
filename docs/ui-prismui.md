# UI em texto: `.prismui`

Uma UI inteira vira **um arquivo de texto** (XML simples) e **uma entidade** com o component **UI Document**. Isso substitui montar a interface com várias entidades `UI Element` (que continuam funcionando, mas são o formato antigo — veja [ui.md](ui.md)).

- Menu **Entidade → Criar UI → Documento de UI (.prismui)** cria `Assets/UI/NovaUI.prismui` com um modelo e a entidade que aponta para ele. Ou *+ Add Component → UI Document* e arraste um `.prismui` do Conteúdo do Projeto.
- **Hot reload**: salve o arquivo (duplo clique abre no editor de código) e a UI se atualiza sozinha, a cada ~0,5 s. Se o XML estiver quebrado, o documento anterior continua na tela e o erro aparece no Console como `arquivo.prismui:linha:coluna: erro: ...`.
- Alterações feitas por script (texto, cor...) valem até o arquivo ser recarregado.

## Exemplo

```xml
<UI scale="height" reference="1920x1080">
  <Panel id="hud" anchor="top-right" margin="24" padding="16" layout="column" gap="12" color="#000000AA">
    <Text id="score" size="36" color="#FFCC00">Pontos: 0</Text>
    <Button id="pause" onClick="OnPause" width="220" color="#3F73D9">Pausar</Button>
  </Panel>
</UI>
```

## Elementos

| Elemento | Uso |
|----------|-----|
| `<UI>` | raiz do arquivo (`scale`, `reference`) |
| `<Panel>` | contêiner/retângulo; sem `color` nem `src` é transparente e **não** barra o mouse |
| `<Row>` / `<Column>` | `Panel` com `layout="row"` / `"column"` |
| `<Text>` | texto (o conteúdo da tag é o texto; quebra de linha automática com `wrap="true"`) |
| `<Image>` | textura (`src`, relativo ao projeto) com tint `color` |
| `<Button>` | retângulo clicável com rótulo centralizado; cor muda com hover/pressionado |
| `<Spacer>` | espaço vazio (use `width`/`height`, ou `fill` para empurrar o resto) |

A ordem no arquivo é a ordem de **desenho**: o que vem depois fica por cima.

## Atributos

| Atributo | Valores |
|----------|---------|
| `id` | nome para achar o elemento por script |
| `visible` | `true`/`false` (esconde também os filhos) |
| `anchor` | `top-left` `top` `top-right` `left` `center` `right` `bottom-left` `bottom` `bottom-right`, ou `stretch` (preenche o pai). Só vale em `layout="stack"` (padrão) |
| `width` `height` | `auto`, `120` / `120px`, `50%` (da área interna do pai), `fill`, `2fr` (fatia proporcional do espaço que sobra em row/column) |
| `margin` `padding` | 1, 2 ou 4 números, na ordem do CSS: `8` · `8 16` (vertical horizontal) · `1 2 3 4` (cima direita baixo esquerda) |
| `layout` | `stack` (filhos se ancoram, um sobre o outro), `row`, `column` |
| `gap` | espaço entre filhos de row/column |
| `align` | eixo transversal em row/column: `start` `center` `end` `stretch` |
| `justify` | eixo principal em row/column: `start` `center` `end` `space-between` |
| `color` | fundo (Panel/Button), tint (Image) ou cor do texto (Text). `#RGB` `#RGBA` `#RRGGBB` `#RRGGBBAA` ou nome |
| `textColor` | cor do rótulo do Button |
| `hoverColor` `pressedColor` | cor **absoluta** do fundo do Button em hover/pressionado (padrão: `color` escurecida) |
| `src` | textura do Panel/Image/Button |
| `text` | alternativa ao conteúdo da tag |
| `size` | tamanho da fonte (px de projeto) |
| `textAlign` | `left` `center` `right` |
| `wrap` | quebra de linha automática pela largura do elemento |
| `interactable` | `false` desativa o Button (cinza, sem clique) |
| `blocksPointer` | barra o mouse para o jogo (`UI.IsPointerOverUI`) |
| `onClick` | nome da função de script chamada no clique |

`<UI scale="height|width|min|none" reference="1920x1080">`: com `scale`, todos os px (tamanhos, margens, fonte) são multiplicados por `tela / reference`, então a UI mantém a proporção em qualquer resolução. Sem `scale` os px são px de tela.

Atributo ou elemento desconhecido vira **aviso** (com sugestão do nome certo quando parece um erro de digitação) e o resto carrega normalmente. Limites: 64 níveis de profundidade, 10 000 elementos.

## Scripts

O clique chama a função de nome `onClick` no script (Lua ou C++) **da mesma entidade** que tem o UI Document, recebendo o `id` do botão. Sem `onClick`, chama `OnClick()` como na UI antiga.

### Lua

```lua
function OnCreate()
    ui = entity:GetUIDocument()
    score = ui:Find("score")      -- nil se o id não existe
end

function OnUpdate(dt)
    score.Text = "Pontos: " .. tostring(points)
    score:SetColor(1, 0.8, 0, 1)
end

function OnPause(id)              -- id == "pause"
    ui:Find("hud").Visible = false
end
```

Elemento: `Text`, `Visible`, `Interactable`, `FontSize`, `Id`, `Exists()`, `SetColor(r,g,b,a)`, `SetTextColor(r,g,b,a)`, `SetTexture(caminho)`. Documento: `Visible`, `Layer`, `Path`, `Find(id)`. As referências são por id e continuam válidas depois de um hot reload.

### C++ (API nativa v8)

```cpp
void OnCreate() override { UISetText("score", "Pontos: 0"); }
void OnUIClick(const char* handler, const char* nodeId) override {
    if (std::string(handler) == "OnPause") UISetVisible("hud", false);
}
```

`UISetText`, `UIGetText`, `UISetVisible`, `UISetInteractable`, `UISetColor`, `UISetTextColor`. Módulos compilados com a API v7 ou anterior precisam ser **recompilados**.

## Limitações

- Imagens não preservam a proporção (use `width`/`height` coerentes).
- Sem rolagem, sem campos de texto e sem animações de UI.
- `Text` não tem padding próprio (use um `Panel` em volta).
- O component `UI Document` é salvo em cenas/prefabs no formato em blocos (v21+); cenas mais antigas não são afetadas.

## Testes

`PrismUITests` (`tests/UITests.cpp`) compila só o núcleo (`UIMarkup`, `UILayout`, `UIInput` — C++ puro, sem OpenGL) e verifica parser, layout e cliques.
