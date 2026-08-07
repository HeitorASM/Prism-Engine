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
    Core/       -> Application, Window, eventos, Log, LogBuffer,
                   Command/CommandHistory (undo-redo)
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
    PrismEditor/Panels/
      ConsolePanel.*          -> painel de log (lê Prism::LogBuffer)
      ContentBrowserPanel.*   -> navega os arquivos do projeto (Assets/Maps/Scripts/Cache)
    PrismEditor/Commands/
      EditorCommands.h        -> Commands concretos (Transform, criar/excluir entidade, etc)

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
4. ~~Undo/Redo command stack.~~ ✅ feito
   (Ctrl+Z/Ctrl+Y globais, cobrindo mover/rotacionar/escalar entidade, mudar
   cor, criar e excluir entidade - ver `Prism::CommandHistory` e
   `PrismEditor/Commands/EditorCommands.h`).
5. ~~Console de verdade.~~ ✅ feito
   (le de `Prism::LogBuffer`, cor por nivel, filtro de texto, toggles de
   verbosidade, auto-scroll - ver `PrismEditor/Panels/ConsolePanel.h`).
6. ~~Content Browser (painel de arquivos do projeto).~~ ✅ feito
   (`PrismEditor/Panels/ContentBrowserPanel.h/.cpp` - navega Assets/Maps/
   Scripts/Cache a partir da raiz do projeto; duplo-clique num `.prismmap`
   já carrega aquele mapa na Scene ativa).
7. ~~Sistema de salvamento melhor.~~ ✅ feito
   ("Salvar Como" com popup de nome, aviso de sobrescrita, múltiplos mapas
   por projeto, "Novo Mapa" funcional, atalhos Ctrl+S/Ctrl+Shift+S - ver
   nota abaixo).
8. ~~Entidades mais robustas.~~ ✅ feito (parcialmente - ver nota abaixo)
   (`LightComponent`, `ColliderComponent`, `RigidBodyComponent`,
   `ScriptComponent` novos; Properties panel virou "Add/Remove Component";
   presets no menu Entidade: Luz, Character, Entidade Vazia).
9. Embutir Lua (ex: via `sol2` ou `LuaBridge`) + primeiro script rodando.
10. Integrar Box3D, corpos rígidos básicos.
11. BSP/CSG (brushes como um tipo de Entity no editor).

## Nota sobre o Console (estado atual)

`Prism::LogBuffer` (`Prism/src/Prism/Core/LogBuffer.h/.cpp`) se conecta como
um *sink* opcional do `Log` (`Log::SetSink`, instalado em
`Application::Application` antes de qualquer log relevante acontecer) e
guarda as últimas 2000 mensagens num buffer circular. Nenhuma mudança na API
de log existente (`PRISM_INFO`, `PRISM_CORE_ERROR`, etc.) — quem chama essas
macros não sabe nem precisa saber que existe um Console; a engine standalone
(fora do editor) continua funcionando exatamente como antes.

`PrismEditor::ConsolePanel` (`PrismEditor/src/PrismEditor/Panels/ConsolePanel.h`)
lê esse buffer a cada frame e desenha: cor por nível (cinza/branco/amarelo/
vermelho), filtro de texto case-insensitive, toggles para esconder
Trace/Info/Warn/Error (Critical fica sempre junto de Error), auto-scroll
inteligente (só desce sozinho se você já estava perto do fim — rolar pra
cima pra ler algo antigo não é interrompido por mensagens novas), e um botão
Limpar.

## Nota sobre Undo/Redo (estado atual)

`Prism::Command`/`Prism::CommandHistory` (`Prism/src/Prism/Core/`) ficam no
CORE da engine de propósito — qualquer ferramenta futura que edite estado
(editor de brushes/BSP, editor de materiais) vai precisar do mesmo
mecanismo. Os comandos concretos (`TransformCommand`, `MeshColorCommand`,
`CreateEntityCommand`, `DeleteEntityCommand`) ficam do lado do Editor
(`PrismEditor/src/PrismEditor/Commands/EditorCommands.h`), porque conhecem
`Entity`/`Scene` do fluxo de edição específico.

Para edições contínuas via `DragFloat3`/`ColorEdit3` (arrastar posição,
escolher cor), o editor captura o estado "antes" quando o gesto **começa**
(`ImGui::IsItemActivated()`) e só empurra **um** comando no histórico quando
o gesto **termina** (`ImGui::IsItemDeactivatedAfterEdit()`) — arrastar a
posição não gera um comando por frame, só um comando por gesto completo.

Trocar de cena (carregar um mapa do disco) limpa o histórico
(`CommandHistory::Clear()`), já que um `Undo()` não tem como desfazer ações
sobre entidades de uma cena que não está mais em memória.

Limitação conhecida: não há ainda um atalho Ctrl+S para salvar (fica para
quando existir um sistema de atalhos mais genérico); Ctrl+Z/Ctrl+Y já
funcionam globalmente, exceto enquanto o ImGui está capturando texto (ex:
editando o campo "Nome"), onde o undo nativo do campo de texto tem
prioridade.

## Nota sobre o Content Browser (estado atual)

`PrismEditor::ContentBrowserPanel` navega a partir de
`Project::GetProjectDirectory()` (a raiz inteira do projeto, não só
`Assets/`) — assim dá pra ver `Maps/` e `Scripts/` também. Duplo-clique numa
pasta entra nela; botão "< Voltar" sobe um nível (desabilitado na raiz);
duplo-clique num `.prismmap` chama `EditorLayer::LoadScene()`, que substitui
a Scene ativa pela do arquivo clicado (sem perguntar "salvar antes?" ainda —
ver limitação abaixo).

Limitações conhecidas, deixadas de propósito para não expandir escopo agora:
sem criar/renomear/excluir/arrastar arquivos pelo painel (isso se conecta
naturalmente ao fluxo de importação de assets do guia do protótipo, que
ainda não existe); sem confirmação de "salvar antes de trocar de mapa" —
carregar outro mapa (seja pelo Content Browser, seja por "Novo Mapa") perde
qualquer alteração não salva na cena atual, sem aviso — fica para quando
existir rastreamento de "alterações não salvas" (dirty flag).

## Nota sobre Entidades mais robustas / Add Component (estado atual)

**Decisão de arquitetura**: em vez de "tipos de entidade" fixos (Prop,
Character, Light como um enum rígido), a engine continua 100% ECS livre —
uma entidade não "é" um tipo, ela é só a soma dos components que tem. Um
Character é uma entidade com `MeshRendererComponent` + `ColliderComponent` +
`RigidBodyComponent` + `ScriptComponent`; uma Light estática é só
`LightComponent`; uma tocha (luz + prop físico) é as duas coisas juntas sem
nenhum caso especial. É o mesmo modelo que Unity/Unreal usam por baixo. Os
"presets" do menu Entidade (Criar Luz, Criar Character, Criar Entidade
Vazia) são só atalhos de conveniência que já adicionam a combinação comum de
components — não travam nada; qualquer component pode ser adicionado ou
removido depois pela Properties panel.

**Novos components** (`Prism/src/Prism/Scene/Components.h`):
- `LightComponent` — tipo (Point/Spot/Directional), cor, intensidade,
  alcance, ângulo do cone.
- `ColliderComponent` — forma (Box/Sphere/Capsule), tamanho, flag de
  trigger.
- `RigidBodyComponent` — tipo de corpo (Static/Kinematic/Dynamic), massa,
  gravidade, CCD — nomes escolhidos já pensando no Box3D (próximo item do
  roadmap).
- `ScriptComponent` — só um caminho relativo para um arquivo `.lua` dentro
  de `Scripts/`. Ainda **não executa nada** — é o slot de dado que a UI e o
  formato de arquivo já suportam, para quando Lua for embutido não precisar
  de outra rodada de migração de `.prismmap`s salvos.

**Properties panel** virou "Add/Remove Component": cada component vira uma
seção colapsável com um "X" para remover (exceto Transform, que toda
entidade tem por definição); um botão "+ Add Component" no final abre um
popup só com os components que a entidade ainda não tem. Tudo passa pelo
`CommandHistory` (Ctrl+Z desfaz adicionar/remover um component inteiro).

**Limitações conhecidas, deixadas de propósito**: os campos numéricos de
Light/Collider/RigidBody (drag floats, combos, checkboxes) ainda **não**
geram comandos individuais de undo por edição — diferente de
Transform/Cor, que capturam um `TransformCommand`/`MeshColorCommand` por
gesto de arraste. Adicionar isso para mais ~15 campos infestaria bastante
esta etapa; fica para uma passada futura se se mostrar necessário na
prática. `LightComponent` ainda não afeta a renderização de verdade (o
Renderer só tem uma luz direcional fixa hardcoded no shader) —
`ColliderComponent`/`RigidBodyComponent` ainda não alimentam nenhuma
simulação física — ambos aguardam os próximos itens do roadmap (iluminação
de verdade / Box3D). Sem parenting/hierarquia real ainda (a Hierarchy panel
continua sendo uma lista plana, não uma árvore).

## Nota sobre Salvar/Salvar Como/Novo Mapa (estado atual)

`EditorLayer` agora rastreia `m_CurrentMapPath` — o arquivo `.prismmap`
associado à cena ativa, vazio quando a cena ainda não foi salva em lugar
nenhum (cena nova, ou a cena de exemplo do primeiro `OnAttach`).

- **Salvar Mapa** (Ctrl+S): grava em `m_CurrentMapPath` se ele já existe;
  caso contrário, se comporta como Salvar Como (não há "onde" sobrescrever
  ainda).
- **Salvar Como...** (Ctrl+Shift+S): sempre abre um popup pedindo um nome,
  mostra um preview do caminho final, avisa em amarelo se um mapa com esse
  nome já existe (seria sobrescrito), e salva num arquivo **novo** dentro de
  `Project::GetMapDirectory()` — nunca sobrescreve outro mapa sem avisar.
  Depois de salvar, esse novo mapa também vira o `StartMap` do projeto (é
  reaberto automaticamente da próxima vez).
- **Novo Mapa**: cria uma `Scene` vazia e limpa `m_CurrentMapPath` — a
  próxima vez que "Salvar Mapa" for usado, pede um nome (mesma lógica do
  primeiro save).

Múltiplos mapas por projeto já funcionam na prática: cada "Salvar Como" com
um nome diferente cria um arquivo `.prismmap` separado dentro de `Maps/`,
todos navegáveis pelo Content Browser — só falta uma UI dedicada para listar
"todos os mapas do projeto" fora do Content Browser genérico, se isso vier a
fazer falta.

## Nota sobre persistência de Scene (estado atual)

`Prism::SceneSerializer` (`Prism/src/Prism/Scene/SceneSerializer.h/.cpp`)
salva/carrega uma `Scene` inteira em um único arquivo binário `.prismmap`,
com um cabeçalho `magic + versão` (`kSceneFormatVersion`) para detectar
arquivos corrompidos ou de um formato futuro incompatível — hoje qualquer
versão diferente da atual é recusada (sem migração automática ainda).
**Formato atual: v2** (v1 não é mais lido — projetos criados antes dos novos
components de Light/Collider/RigidBody/Script, ver seção acima, precisam ser
resalvos uma vez).

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
