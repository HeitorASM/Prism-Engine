# Arquitetura

## Visão geral

`Application` roda o loop principal e mantém uma `LayerStack`. O editor não é um caso especial: é uma `Layer` empilhada, como qualquer outra. O modo Runtime (jogo sem editor) é só outro executável com a sua própria `Layer`, sem mudanças no core.

O projeto tem três alvos CMake:

- **Prism**: biblioteca estática com a engine. Não define `main()`; ele vem de `Core/EntryPoint.h`, incluído pelo executável que consome a biblioteca.
- **PrismEditor**: executável do editor. Implementa `Prism::CreateApplication()` em `EditorApp.cpp`.
- **PrismRuntime**: executável que roda um projeto sem o editor (`PrismRuntime <projeto>`). Implementa `CreateApplication()` em `RuntimeApp.cpp`; a `RuntimeLayer` carrega o projeto e o mapa inicial e desenha com `GameView`. Os argumentos de linha de comando chegam por `GetLaunchArguments()` (`Core/CommandLine.h`).

## Módulos da engine

| Pasta | Conteúdo |
|---|---|
| `Core/` | `Application`, `Window`/`GlfwWindow`, eventos, `Input`, `CommandLine`, `Log`/`LogBuffer`, `Command`/`CommandHistory`, `CrashHandler` |
| `Layer/` | `Layer`, `LayerStack` |
| `Renderer/` | `Renderer`, `Shader`, `Mesh`, `PrimitiveMeshFactory`, `Framebuffer`, `GameView`, `Texture`, `ShadowMap`, `SSAO`, `GeometryBuffer`, `GraphicsContext` |
| `Scene/` | `Scene`, `Entity`, `Components`, `ComponentRegistry`, serializadores |
| `Scripting/` | `ScriptEngine` (Lua via sol2), `NativeScripting` + `NativeScript.h` (scripts C++ em módulo DLL/.so) |
| `Physics/` | `PhysicsEngine` (Jolt) |
| `Audio/` | `AudioEngine` (miniaudio) |
| `ImGui/` | `ImGuiLayer` |
| `Project/` | `Project`, `ProjectSerializer`, `GameExporter` (Arquivo → Exportar Jogo) |
| `Assets/` | `AssetID`, `AssetMeta`, `AssetRegistry`, `ModelLoader` |

## Cena e ECS

`Scene` e `Entity` usam [EnTT](https://github.com/skypjack/entt). `Entity` é só um par (ID, `Scene*`); todo dado vive em components (`Scene/Components.h`), que são structs sem lógica.

Uma entidade não tem "tipo": ela é a soma dos components que possui. Um personagem, por exemplo, é uma entidade com `MeshRenderer` + `Collider` + `RigidBody` + `Script`. Os presets do menu Entidade (Criar Luz, Criar Character etc.) são atalhos que adicionam combinações comuns.

Components disponíveis: `Tag`, `Transform`, `MeshRenderer`, `Material`, `Camera`, `Light`, `Collider`, `RigidBody`, `Relationship`, `Script`, `Raycast`, `AudioSource`, `AudioListener`.

### Hierarquia (parenting)

- `RelationshipComponent` guarda pai e filhos.
- `Scene::GetWorldTransform(entity)` combina o transform local com o de todos os ancestrais. É a função a usar sempre que algo precisar da posição no mundo (render, gizmos, física).
- `Scene::SetParent` recusa operações que criariam ciclo.
- `Scene::DestroyEntity` é recursivo: excluir um pai exclui toda a subárvore.
- `Scene::DuplicateEntity` copia a entidade e todos os descendentes.

### Registro de components

`ComponentRegistry` (`ComponentRegistration.cpp`) centraliza, para cada component opcional: checar presença, adicionar com valores padrão, remover, serializar/desserializar e copiar. Serialização, o menu "Add Component" e `Scene::Clone()` usam esse registro.

Para adicionar um component novo:

1. Declare a struct em `Components.h`.
2. Registre em `ComponentRegistration.cpp`.
3. Não precisa subir `kSceneFormatVersion`: o component é gravado num bloco com nome e versão. Para **adicionar campos depois**, escreva-os no fim do `Serialize`, suba `ComponentTypeInfo::Version` e leia-os só quando `ComponentRegistry::CurrentBlockVersion()` for suficiente (ver `Camera` em `ComponentRegistration.cpp`). O `DisplayName` é o nome no arquivo: não o renomeie.
4. Escreva a UI dele em `PrismEditor/Panels/PropertiesPanel_<Nome>.cpp` (função `Draw<Nome>UI()`, declarada em `PropertiesPanel.h` e chamada em `OnImGuiRender()`). A UI é manual, pois cada component tem lógica própria demais para generalizar.

`RelationshipComponent` fica fora do registro: usa índice posicional do pai na serialização e nunca aparece no menu.

## Identidade de assets

`Assets/` dá a cada arquivo de asset uma identidade estável, independente do caminho. É a base do "vínculo vivo" entre instâncias e seus prefabs/materiais: uma referência gravada como `AssetID` continua válida depois de mover ou renomear o arquivo, o que um caminho de texto não garante.

- `AssetID`: 64 bits aleatórios; o valor `0` significa "sem asset".
- `AssetMeta`/`AssetMetaFile`: lê e grava o `.meta` ao lado de cada asset (formato em [formatos-de-arquivo.md](formatos-de-arquivo.md#meta)).
- `AssetRegistry`: índice `AssetID ↔ caminho`. `Refresh()` varre a pasta e reconcilia com os `.meta`. Cada `Project` tem o seu (`Project::GetAssetRegistry()`), já varrido quando `Project::New`/`Load` retornam. O botão **Atualizar** do painel Conteúdo do Projeto chama `Refresh()`.

O módulo não depende de OpenGL, GLFW nem EnTT, de propósito: compila e é testado isoladamente (ver abaixo).

## Vínculo vivo de Material

`MaterialComponent::LinkedAsset` (um `AssetID`) marca um material como uma *view* de um `.prismmat`, em vez de uma cópia independente. Editar qualquer campo no painel Material grava no arquivo (com um pequeno atraso, `MaterialLinkSync::FlushSave`), e `MaterialLinkSync::Reconcile` releva o arquivo (comparando a hora de modificação) e propaga para **toda** entidade vinculada ao mesmo asset, todo frame. O painel mostra um selo **[Vinculado]** (ou **[Vínculo quebrado]**, se o asset sumiu) com um botão **Desvincular**.

`LoadMaterialAssetCommand` (arrastar um `.prismmat`, ou "Carregar de Asset") liga o vínculo, resolvendo o caminho via `AssetRegistry`. "Salvar como Asset" nunca liga vínculo — é sempre uma cópia pontual.

## Vínculo vivo de Prefab

Igual em espírito ao de Material, mas para uma **subárvore** de entidades em vez de um struct único — por isso usa dois components (`PrefabInstanceRootComponent` na raiz, `PrefabInstanceMemberComponent` em toda a subárvore) e uma classe dedicada, `Scene/PrefabSyncer.h`.

**Override por component inteiro** (não por campo, como Godot/Unity): `PrefabSyncer::Diff` relê o `.prismprefab` numa `Scene` temporária e compara, por entidade da instância, os bytes serializados de cada `Component` contra o correspondente no arquivo (casados por `PrefabInstanceMemberComponent::IndexInPrefab`). Divergem → *overridado* (nunca sobrescrito por um sync). Idênticos → pode ser atualizado.

- `PrefabSyncer::UpdateAll`: aplica o prefab atual em todo Component não overridado da instância.
- `PrefabSyncer::RevertComponent`: descarta o override de **um** Component específico.
- `PrefabSyncer::ApplyComponentToPrefab`: inverso — grava o valor atual da instância de volta no arquivo (não propaga para outras instâncias automaticamente; é uma ação explícita).

Os três comandos equivalentes (`SyncPrefabInstanceCommand`, `RevertPrefabComponentCommand`, `ApplyPrefabComponentCommand`, em `EditorCommands.h`) passam pelo `CommandHistory` (Undo/Redo), exceto a escrita do arquivo em si no último, que é irreversível por Ctrl+Z.

**Limitação conhecida:** `TransformComponent` (como `TagComponent`) nunca passa pelo `ComponentRegistry`, então nunca entra nesta comparação — mover/girar uma entidade da instância nunca conta como override, mas por isso um ajuste de posição feito no prefab também nunca sincroniza para instâncias já existentes.

`PrefabInstanceRootComponent`/`PrefabInstanceMemberComponent` nunca são registrados no `ComponentRegistry` (ver comentário em `ComponentRegistration.cpp`): um `.prismprefab` nunca contém esses dois components, mesmo salvo a partir de uma entidade que já é ela mesma uma instância (aninhamento). `SceneSerializer` os grava como bloco à parte no `.prismmap` (a partir da v12 de `kSceneFormatVersion`; a versão atual e o histórico estão em [formatos-de-arquivo.md](formatos-de-arquivo.md#prismmap)).

## Testes

Os testes são opt-in e não afetam o build normal:

```bash
cmake -B build -S . -DPRISM_BUILD_TESTS=ON
cmake --build build --target PrismAssetTests
ctest --test-dir build --output-on-failure
```

`PrismBuildAnalysisTests` compila só `Core/BuildOutputAnalysis.h` (reconhece erros de arquivo travado/símbolo não resolvido na compilação dos scripts C++). `PrismDrawBatchingTests` compila só `Renderer/DrawBatching.h` (agrupamento de desenhos para instancing). `PrismProfilerTests` compila só `Core/Profiler.h` (ver [desempenho.md](desempenho.md)). `PrismSceneTests` linka a biblioteca Prism inteira e cobre serialização de cena, `ComponentRegistry` (round-trip de todos os componentes, blocos truncados/antigos/desconhecidos) e `PrefabSyncer` (diff, revert, aplicar); só existe num build completo. `PrismPhysicsLayersTests` compila só `Physics/PhysicsLayers.h` (camadas de física e matriz de colisão, ver [fisica.md](fisica.md)). `PrismCullingTests` compila só `Renderer/Culling.h` (frustum e camadas, ver [renderizacao.md](renderizacao.md)). `PrismUITests` compila só o núcleo da UI em texto (`UI/UIMarkup`, `UILayout`, `UIInput`; ver [ui-prismui.md](ui-prismui.md)). `PrismExportTests` compila só `Project/GameExporter.cpp` (o exportador de jogos, ver [exportacao.md](exportacao.md)) e roda sobre pastas temporárias com arquivos de mentira.

`PrismAssetTests` compila só `Assets/` e `Project/`, sem GPU, GLFW ou Jolt. Cobre `AssetID`, o formato do `.meta`, todos os casos de reconciliação do `AssetRegistry` e um ciclo real `Project::New` → `Project::Load` com assets movidos entre as duas etapas.

## Undo/redo

`Command` e `CommandHistory` ficam no core (`Core/`), pois qualquer ferramenta que edite estado precisa do mecanismo. Os comandos concretos (`TransformCommand`, `CreateEntityCommand`, `SetParentCommand` etc.) ficam em `PrismEditor/Commands/EditorCommands.h`.

Em edições contínuas (arrastar um campo, usar o gizmo), o estado "antes" é capturado no início do gesto e **um** comando é empurrado ao final, não um por frame. Carregar outro mapa limpa o histórico.

## Estrutura do editor

`EditorLayer` é só o **orquestrador** (~400 linhas): monta o dockspace e a barra de menus, trata atalhos e liga o modo Play. Tudo o mais mora em arquivos próprios em `PrismEditor/src/PrismEditor/`:

| Arquivo | Responsabilidade |
|---|---|
| `Core/EditorContext.h` | Estado **compartilhado** entre painéis: cena ativa, entidade selecionada, histórico de undo, caminho do mapa, câmera do editor, vínculo de material |
| `Core/GameExportRunner` | Roda `Prism::ExportGame` numa thread de fundo e entrega as linhas à aba Saída de Build (Arquivo → Exportar Jogo) |
| `Core/SceneDocument` | Ciclo de vida do mapa: novo, carregar, salvar, salvar como; aviso de alterações não salvas |
| `Core/EntityOps` | Duplicar, excluir, instanciar prefab, câmera primária (funções livres sobre o `EditorContext`) |
| `Core/MaterialLinkSync` | Vínculo vivo de material no lado do editor (debounce, gravação, reconciliação) |
| `Core/EditorCamera` | Câmera livre da viewport |
| `Panels/ViewportPanel` | Viewport 3D, modo voar, seleção por clique, drop de prefab, gizmo de transform (ImGuizmo) |
| `Panels/EditorGizmos` | Gizmos de linha: câmera, collider, luz, raycast |
| `Panels/HierarchyPanel` | Árvore de entidades, menu de contexto, popup "Criar Prefab" |
| `Panels/PropertiesPanel` (+ `_Prefab.cpp` e um `_<Component>.cpp` por component) | Propriedades por component, seção Prefab, "+ Add Component", popups de material e script |
| `Panels/GamePanel` | Aba Game: roda a cópia da cena no editor (framebuffer, captura de input) |
| `Panels/PropertiesPanel_Environment` | UI do `EnvironmentComponent` (ambiente e skybox da cena) |
| `Panels/Console`, `ContentBrowser`, `ScriptEditor`, `EntityContextMenu` | Painéis já independentes |
| `Panels/BuildOutputPanel` | Aba "Saída de Build": saída da compilação dos scripts C++, com erros clicáveis |
| `Core/FileDialog` | Diálogos nativos (escolher pasta/arquivo, via nativefiledialog-extended) e conversão UTF-8 ↔ `path` |
| `Layers/ProjectManagerLayer` | Tela inicial: projetos recentes e criação de projeto (validação em tempo real). `EditorLayer::CloseProject` volta para ela |

**Regra para código novo:** estado que só um painel usa fica **no painel**; estado que dois ou mais painéis leem ou escrevem entra no `EditorContext`. Um painel novo recebe `EditorContext&` no construtor e **não** inclui `EditorLayer.h`. Para adicionar um painel: crie `Panels/MeuPainel.{h,cpp}` (o CMake pega arquivos novos sozinho), instancie-o como membro do `EditorLayer` (depois de `m_Ctx`) e chame o `OnImGuiRender()` dele em `RenderDockspace()`.

## Ordem de includes do OpenGL

`glad` deve ser incluído antes de qualquer header que toque em OpenGL ou GLFW, em todo `.cpp`. Se o GLFW for incluído primeiro, ele puxa o header nativo do sistema e colide com o glad, gerando `OpenGL header already included`. Em caso desse erro, confira primeiro a ordem dos includes do arquivo que falhou.

## Dependências

| Biblioteca | Uso | Origem |
|---|---|---|
| glad (OpenGL 4.5 Core) | carregador de funções OpenGL | `vendor/glad` |
| stb_image | leitura de texturas | `vendor/stb` |
| GLFW 3.4 | janela e input | FetchContent |
| glm 1.0.1 | matemática | FetchContent |
| EnTT v3.13.2 | ECS | FetchContent |
| Dear ImGui (branch `docking`) | interface | FetchContent |
| ImGuizmo | gizmo de transform | FetchContent |
| ImGuiColorTextEdit | editor de código | FetchContent |
| Lua 5.4.7 + sol2 3.3.0 | scripting | FetchContent |
| Jolt Physics 5.2.0 | física | FetchContent |
| miniaudio 0.11.22 | áudio | FetchContent (só o header) |
| nativefiledialog-extended | diálogos nativos de arquivo/pasta no editor | FetchContent |
| Assimp | importação de modelos (`.obj`/`.fbx`/`.gltf`/`.glb`), via `Assets/ModelLoader` | FetchContent |

As versões são fixadas em `vendor/CMakeLists.txt`.

## Plataformas

O desenvolvimento é focado em Windows. O `CrashHandler` só tem implementação no Windows (`PRISM_PLATFORM_WINDOWS`); nas outras plataformas ele é vazio.

## Versão da engine

`Version.py` (raiz) é a fonte única de nome, versão, estado e build:

```python
Name = "Prism"
Long_name = "Prism Engine"
Version = "0.1.0"
Version_state = "pre-alpha"
Editor_version = "0.1.0"
Build = ""
```

`cmake/PrismVersion.cmake` lê o arquivo ao configurar e gera `<build>/generated/PrismVersion.h` (a partir de `Prism/src/Prism/Core/Version.h.in`); o CMake reconfigura sozinho quando o `Version.py` muda. No código: `#include <PrismVersion.h>` e `Prism::Version::Full()` ("Prism Engine 0.1.0 pre-alpha"), `Version::EditorFull()` (título da janela do editor), `Version::Number`, `Version::State`, `Version::Build` e as macros `PRISM_VERSION` etc. Para um build de CI: `cmake -DPRISM_BUILD=1234 ...` sobrescreve o `Build`. A versão é registrada no Console ao iniciar o loop principal. Não confundir com `kSceneFormatVersion`/`kPrefabFormatVersion`, que versionam os formatos de arquivo.
