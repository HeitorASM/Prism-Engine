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
    Scene/      -> Scene, Entity, Components (ECS via EnTT), SceneSerializer
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

## Próximos passos sugeridos (nesta ordem)

1. ~~Framebuffer + renderização real da cena na Viewport panel.~~ ✅ feito
2. ~~Sistema de `Scene`/`Entity` (ECS via EnTT) + Hierarchy panel real.~~ ✅ feito
3. ~~Salvar/carregar `Scene` em disco.~~ ✅ feito
   (formato binário `.prismmap` dentro de `Project::GetMapDirectory()`;
   menu "Arquivo > Salvar Mapa" já funciona; a cena salva mais recentemente
   é reaberta automaticamente na próxima vez que o editor abre, via
   `ProjectConfig::StartMap`).
4. Undo/Redo command stack (essencial, conforme definido).
5. Embutir Lua (ex: via `sol2` ou `LuaBridge`) + primeiro script rodando.
6. Integrar Box3D, corpos rígidos básicos.
7. BSP/CSG (brushes como um tipo de Entity no editor).

## Nota sobre persistência de Scene (estado atual)

`Prism::SceneSerializer` (`Prism/src/Prism/Scene/SceneSerializer.h/.cpp`)
salva/carrega uma `Scene` inteira em um único arquivo binário `.prismmap`,
com um cabeçalho `magic + versão` (`kSceneFormatVersion`) para detectar
arquivos corrompidos ou de um formato futuro incompatível — hoje qualquer
versão diferente da atual é recusada (sem migração automática ainda).

Fluxo no editor: `EditorLayer::LoadOrCreateScene()` tenta carregar
`ProjectConfig::StartMap`; se não existir (projeto novo), cria a cena de
exemplo em memória de sempre. `EditorLayer::SaveActiveScene()` (menu
Arquivo > Salvar Mapa) grava o arquivo e, no primeiro save, também chama
`Project::SetStartMap()` para lembrar qual mapa reabrir da próxima vez.

Limitações conhecidas, deixadas de propósito para não expandir escopo agora:
não há atalho de teclado Ctrl+S funcional ainda (precisa de um sistema de
Input por polling, que ainda não existe — ver nota em `EditorLayer::OnEvent`);
não há suporte a múltiplos mapas por projeto nem uma janela "Salvar como"
(o nome do arquivo vem do nome da cena, automaticamente, no primeiro save).

## Nota sobre a Scene/ECS (estado atual)

`Scene`/`Entity` usam [EnTT](https://github.com/skypjack/entt) por baixo
(header-only, baixado via `FetchContent` — não precisa instalar nada à
parte). `Entity` é só um par (ID, `Scene*`); todo dado real vive em
*components* (`Prism/src/Prism/Scene/Components.h`): `TagComponent`,
`TransformComponent`, `MeshRendererComponent`, e um `CameraComponent` ainda
não usado (reservado para quando existir um modo "Play" com câmera de jogo,
distinta da câmera de órbita do editor).

Toda entidade nasce com `TagComponent` + `TransformComponent` (ver
`Scene::CreateEntity`). O "sistema" que desenha a cena é só uma função que
itera `registry.view<TransformComponent, MeshRendererComponent>()` — ver
`EditorLayer::RenderScene()`. Adicionar um novo tipo de mesh primitivo
(esfera, plano) é: adicionar ao `enum PrimitiveMesh`, ensinar `Renderer` a
desenhar essa geometria, e adicionar um `case` no `switch` de
`RenderScene()`.

## Nota sobre a Viewport (estado atual do renderer)

O `Renderer` hoje é propositalmente mínimo: um shader único com iluminação
direcional simples, sabendo desenhar só um tipo de primitiva (cubo unitário,
`Renderer::DrawTestCube`). Isso existe para provar o caminho completo
`Framebuffer → Scene/ECS → Shader/Renderer → ImGui::Image` funcionando de
ponta a ponta. O "Renderizador principal" definitivo (forward vs deferred,
batching, materiais de verdade, importação de meshes) ainda está em aberto,
conforme o guia do protótipo.
