# Visibilidade e sombras por objeto

## Esconder / mostrar (estilo Godot)
- Painel **Hierarquia**: o ícone de olho à direita de cada linha esconde/mostra a entidade. Os **filhos herdam**: esconder um pai esconde toda a subárvore (o olho dos filhos aparece cortado e o nome fica apagado). Ctrl+Z desfaz.
- Escondido = não desenha malhas, luzes, partículas e documentos de UI, não projeta sombra e não é clicável no viewport. **Física, colisões e scripts continuam funcionando.**
- Salvo na cena como o componente `Hidden` (só existe em entidades escondidas; cenas antigas abrem normalmente).

### Lua
```lua
entity:SetVisible(false)
if not entity:IsVisible() then log("escondido") end   -- IsVisible considera os ancestrais
```

### C++ (API nativa v14 — recompile os módulos de script existentes)
```cpp
SetVisible(false);
bool v = IsVisible();
```

## Projetar sombras (Mesh Renderer)
O campo **Projetar sombras** do `Mesh Renderer` (padrão: ligado) tira o objeto do mapa de sombras quando desligado. Não é o mesmo que esconder: o objeto continua visível. "Receber sombras" por objeto ainda não existe.
