# Prism Engine

Engine 3D em C++20 com editor integrado (WYSIWYG), construída para projetos
reais e portfólio. Renderização via OpenGL 4.5, janela via GLFW, UI via Dear
ImGui (docking), matemática via glm.

## Estado atual (fundação)

Esta é a **fundação** do projeto: arquitetura de camadas (`Layer`/`LayerStack`),
`Application` central, sistema de eventos, `Project`/`ProjectManager` e o
`EditorLayer` com dockspace e painéis (Viewport, Hierarquia, Propriedades,
Console). A viewport 3D agora **renderiza de verdade**: um `Framebuffer`
offscreen recebe o desenho de um cubo de teste (via `Renderer` + `Shader`
básicos) e o resultado é mostrado dentro do painel Viewport com
`ImGui::Image`, com uma câmera de órbita simples (botão direito + arrastar,
scroll para zoom) e resize automático do framebuffer conforme o painel muda
de tamanho.

Ainda **não** implementados (por design, para não travar o projeto tentando
fazer tudo de uma vez): Scene/Entity real, scripting Lua, física Box3D,
BSP/CSG, importação de assets (FBX/OBJ/glTF/áudio), sistema de luzes.

## Arquitetura em uma frase

`Application` roda um loop e possui uma `LayerStack`. O Editor não é um caso
especial dentro da engine — ele é só mais uma `Layer` empilhada. Isso significa
que, quando o modo "Runtime" (jogo exportado, sem editor) existir, ele também
será só outra `Layer`/executável, sem exigir mudanças no core.

```
Prism/          -> a engine, biblioteca estática (Prism.lib)
  src/Prism/
    Core/       -> Application, Window, eventos, Log
    Layer/      -> Layer, LayerStack
    Renderer/   -> GraphicsContext (abstrai OpenGL/futuro Vulkan),
                   Framebuffer (offscreen render target para a viewport),
                   Shader, Renderer (API minima de desenho)
    ImGui/      -> ImGuiLayer (integra Dear ImGui ao ciclo de eventos)
    Project/    -> Project, ProjectSerializer (.prismproj)

PrismEditor/    -> o executável do editor
  src/
    EditorApp.cpp             -> define CreateApplication()
    PrismEditor/Layers/
      ProjectManagerLayer.*   -> tela inicial (criar/abrir projeto)
      EditorLayer.*           -> dockspace + painéis do editor

vendor/         -> dependências de terceiros
  glad/         -> vendorizado localmente (gerado, OpenGL 4.5 Core)
  CMakeLists.txt -> baixa GLFW, glm e Dear ImGui via FetchContent
```

## Como compilar

### Pré-requisitos
- CMake 3.20+
- Visual Studio 2022 (com "Desktop development with C++") **ou** qualquer
  compilador C++20 (MSVC, Clang, GCC)
- Conexão com a internet na primeira configuração (CMake baixa GLFW/glm/ImGui)

### Opção A — Visual Studio 2022 (recomendado)

1. Abra o Visual Studio 2022.
2. **Arquivo → Abrir → Pasta...** e selecione a pasta raiz deste projeto
   (a que contém este `README.md` e o `CMakeLists.txt` principal).
3. O Visual Studio detecta o `CMakeLists.txt` automaticamente e configura o
   projeto sozinho (pode levar um tempo na primeira vez, baixando as
   dependências).
4. No seletor de "Startup Item" (topo da janela), escolha `PrismEditor.exe`.
5. Compile e rode com **Ctrl+F5** ou o botão verde de Play.

Não é necessário criar `.vcxproj` manualmente, adicionar arquivos um por um,
nem configurar Include/Library Directories — o CMake cuida de tudo isso.

### Opção B — linha de comando

```bash
cmake -B build -S .
cmake --build build --config Debug
```

O executável final fica em `build/PrismEditor/Debug/PrismEditor.exe`
(ou `Release/` conforme a configuração escolhida).

## Convenção crítica: ordem de includes do OpenGL

`glad` **sempre** precisa ser incluído antes de qualquer coisa que toque em
GLFW ou OpenGL, em **todo** arquivo `.cpp`. Veja o comentário completo em
`Prism/src/Prism/Renderer/OpenGLContext.cpp`. Ignorar isso é a causa mais
comum de erro de build nesta engine.

## Próximos passos

1. ~~Framebuffer + renderização real da cena na Viewport panel.~~ ✅ feito
   (cubo de teste; falta trocar por uma `Scene` real no passo 2).
2. Sistema de `Scene`/`Entity` (provavelmente ECS simples) + Hierarchy panel real.
3. Undo/Redo command stack (essencial, conforme definido).
4. Embutir Lua (ex: via `sol2` ou `LuaBridge`) + primeiro script rodando.
5. Integrar Box3D, corpos rígidos básicos.
6. BSP/CSG (brushes como um tipo de Entity no editor).

## Nota sobre a Viewport (estado atual do renderer)

O `Renderer` hoje é propositalmente mínimo: um shader único com iluminação
direcional simples, desenhando um cubo hardcoded (`Renderer::DrawTestCube`).
Isso existe só para provar o caminho completo
`Framebuffer → Shader/Renderer → ImGui::Image` funcionando de ponta a ponta.
Quando o sistema de `Scene`/`Entity` (passo 2 acima) existir, o lugar certo
para evoluir é trocar essa chamada fixa por um loop que percorre as entidades
da cena ativa e desenha cada uma com seu próprio mesh/material — o
`Framebuffer` e o resto da integração com o painel Viewport não precisam
mudar.
