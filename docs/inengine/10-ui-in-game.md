# UI in-game

A interface do **jogo** (placar, menus, barra de vida) é um arquivo de texto `.prismui` (XML simples) ligado a uma entidade pelo component **UI Document**. Ela é desenhada por cima da cena, na janela de Play, e aparece (só para visualizar) na viewport do editor.

Crie pelo menu **Entidade → Criar UI → Documento de UI (.prismui)**: ele cria `Assets/UI/NovaUI.prismui` com um modelo e a entidade. Salve o arquivo e a UI se atualiza sozinha (hot reload).

Tudo sobre o formato (elementos, layout, escala por resolução, cores, imagens, botões) e sobre mexer na UI por script, em Lua e em C++, está em **[ui-prismui.md](../ui-prismui.md)**.

## O essencial

- Clique: o botão com `onClick="Nome"` chama a função `Nome(id)` do script da mesma entidade; sem `onClick`, chama `OnClick()`. Com o cursor travado (`CursorMode.Locked`) a UI não recebe o mouse.
- **Barra o mouse**: use `UI.IsPointerOverUI()` para não atirar nem mirar quando o mouse está sobre a interface:

```lua
if Input.IsMouseButtonDown(Mouse.Left) and not UI.IsPointerOverUI() then
    -- ação do jogo
end
```

- Por script (Lua): `ui = entity:GetUIDocument()`, depois `ui:Find("id")` com `Text`, `SetColor(...)` etc.
- Fonte: DejaVu Sans (letras latinas com acentos), em `fonts/DejaVuSans.ttf` ao lado do executável do editor.
