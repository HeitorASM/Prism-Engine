# Arquitetura

## Visão geral

`Application` roda o loop principal e mantém uma `LayerStack`. O editor não é um caso especial: é uma `Layer` empilhada, como qualquer outra. Um futuro modo Runtime (jogo exportado, sem editor) seria apenas outra `Layer` ou outro executável, sem mudanças no core.

O projeto tem dois alvos CMake:

- **Prism**: biblioteca estática com a engine. Não define `main()`; ele vem de `Core/EntryPoint.h`, incluído pelo executável que consome a biblioteca.
- **PrismEditor**: executável do editor. Implementa `Prism::CreateApplication()` em `EditorApp.cpp`.

## Módulos da engine

| Pasta | Conteúdo |
|---|---|
| `Core/` | `Application`, `Window`/`GlfwWindow`, eventos, `Input`, `Log`/`LogBuffer`, `Command`/`CommandHistory`, `CrashHandler` |
| `Layer/` | `Layer`, `LayerStack` |
| `Renderer/` | `Renderer`, `Shader`, `Mesh`, `PrimitiveMeshFactory`, `Framebuffer`, `Texture`, `ShadowMap`, `SSAO`, `GeometryBuffer`, `GraphicsContext` |
| `Scene/` | `Scene`, `Entity`, `Components`, `ComponentRegistry`, serializadores |
| `Scripting/` | `ScriptEngine` (Lua via sol2) |
| `Physics/` | `PhysicsEngine` (Jolt) |
| `ImGui/` | `ImGuiLayer` |
| `Project/` | `Project`, `ProjectSerializer` |

## Cena e ECS

`Scene` e `Entity` usam [EnTT](https://github.com/skypjack/entt). `Entity` é só um par (ID, `Scene*`); todo dado vive em components (`Scene/Components.h`), que são structs sem lógica.

Uma entidade não tem "tipo": ela é a soma dos components que possui. Um personagem, por exemplo, é uma entidade com `MeshRenderer` + `Collider` + `RigidBody` + `Script`. Os presets do menu Entidade (Criar Luz, Criar Character etc.) são atalhos que adicionam combinações comuns.

Components disponíveis: `Tag`, `Transform`, `MeshRenderer`, `Material`, `Camera`, `Light`, `Collider`, `RigidBody`, `Relationship`, `Script`, `Raycast`.

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
3. Incremente `kSceneFormatVersion` em `SceneSerializer.cpp`.
4. Escreva a UI dele na Properties panel (`EditorLayer.cpp`). A UI é manual, pois cada component tem lógica própria demais para generalizar.

`RelationshipComponent` fica fora do registro: usa índice posicional do pai na serialização e nunca aparece no menu.

## Undo/redo

`Command` e `CommandHistory` ficam no core (`Core/`), pois qualquer ferramenta que edite estado precisa do mecanismo. Os comandos concretos (`TransformCommand`, `CreateEntityCommand`, `SetParentCommand` etc.) ficam em `PrismEditor/Commands/EditorCommands.h`.

Em edições contínuas (arrastar um campo, usar o gizmo), o estado "antes" é capturado no início do gesto e **um** comando é empurrado ao final, não um por frame. Carregar outro mapa limpa o histórico.

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

As versões são fixadas em `vendor/CMakeLists.txt`.

## Plataformas

O desenvolvimento é focado em Windows. O `CrashHandler` só tem implementação no Windows (`PRISM_PLATFORM_WINDOWS`); nas outras plataformas ele é vazio.
