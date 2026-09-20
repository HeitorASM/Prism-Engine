# Prism Engine

Engine 3D em C++20 com editor integrado. OpenGL 4.5, GLFW, Dear ImGui (docking), EnTT, Lua (sol2) e Jolt Physics.

> Ainda em fase incial de desenvolvimento

## Recursos

- Editor com viewport, hierarquia em árvore, propriedades, console, content browser e editor de scripts
- Gizmo de mover/rotacionar/escalar (ImGuizmo) e seleção por clique na viewport
- Undo/redo para as operações principais
- ECS com EnTT: transform, mesh, material, câmera, luz, collider, rigid body, script e raycast
- Iluminação PBR multi-luz (Point/Spot/Directional) com roughness/metallic, normal map, sombras direcionais e SSAO
- Física com Jolt (corpos estáticos, cinemáticos e dinâmicos)
- Scripting em Lua com acesso a transform, física, input e raycast
- Modo Play em janela separada, rodando uma cópia da cena
- Mapas (`.prismmap`), prefabs (`.prismprefab`) e materiais (`.prismmat`)

> Muitos recursos ainda estão em fase incial de desenvolvimento e são sucetiveis a erros/poblemas
> E ainda estão sendo ativamente melhorados/corigidos

## Compilando

Requisitos: CMake 3.20+ e um compilador C++20 (MSVC, Clang ou GCC). Na primeira configuração é necessária conexão com a internet, pois o CMake baixa as dependências.

**Visual Studio:** abra a pasta do projeto (Arquivo → Abrir → Pasta), selecione `PrismEditor.exe` como item de inicialização compile e rode .

**Linha de comando:**

```bash
cmake -B build -S .
cmake --build build --config Debug
```

O executável fica em `build/PrismEditor/Debug/PrismEditor.exe` (ou `Release/`).

> Erro `OpenGL header already included`? Veja a regra de ordem de includes em [docs/arquitetura.md](docs/arquitetura.md#ordem-de-includes-do-opengl).

## Estrutura

```
Prism/            engine (biblioteca estática)
  src/Prism/
    Core/         Application, janela, eventos, input, log, undo/redo
    Layer/        Layer e LayerStack
    Renderer/     Renderer, shaders, meshes, framebuffers, sombras, SSAO
    Scene/        Scene, Entity, components, serializadores
    Scripting/    ScriptEngine (Lua)
    Physics/      PhysicsEngine (Jolt)
    ImGui/        integração do ImGui
    Project/      Project e ProjectSerializer (.prismproj)
PrismEditor/      executável do editor
vendor/           dependências (glad e stb locais; o resto via FetchContent)
docs/             documentação detalhada
```

## Documentação De desenvolvimento de desiçoes

- [Arquitetura](docs/arquitetura.md)
- [Editor](docs/editor.md)
- [Scripting Lua](docs/scripting-lua.md)
- [Física](docs/fisica.md)
- [Renderização](docs/renderizacao.md)
- [Formatos de arquivo](docs/formatos-de-arquivo.md)

## Licença

MIT. Veja o arquivo [LICENSE](LICENSE).

Licenças das dependências em [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Roadmap

- [ ] Importação de modelos (FBX/OBJ/glTF), áudio e texturas pelo editor
- [ ] BSP/CSG e ferramentas de construção de mapas
- [x] Modelo de iluminação com especular/PBR (Cook-Torrance GGX; ainda sem reflexão de ambiente/IBL)
- [ ] Callbacks de colisão em Lua (`OnCollisionEnter/Exit`)
- [ ] Sombras para luzes Point/Spot e Cascaded Shadow Maps
- [ ] Editor de código com autocomplete e debugger
- [ ] Vínculo vivo entre instâncias e seus prefabs/materiais
- [ ] Aviso de alterações não salvas (dirty flag)
- [ ] Modo Runtime (jogo exportado, sem editor)
