# Prism Engine

Engine 3D em C++20 com editor integrado. OpenGL 4.5, GLFW, Dear ImGui (docking), EnTT, Lua (sol2) e Jolt Physics.

> Ainda em fase incial de desenvolvimento

## Recursos

- Editor com viewport, hierarquia em árvore, propriedades, console, content browser, editor de código (vários arquivos) e documentação embutida (F1)
- Ambiente e skybox por cena (component Environment) e opções de render por câmera
- Gizmo de mover/rotacionar/escalar (ImGuizmo) e seleção por clique na viewport
- Undo/redo para as operações principais e aviso de alterações não salvas ao fechar o editor ou trocar de mapa
- ECS com EnTT: transform, mesh, material, câmera, luz, collider, rigid body, script e raycast
- Iluminação PBR multi-luz (Point/Spot/Directional) com roughness/metallic, normal map, sombras direcionais e SSAO
- Física com Jolt (corpos estáticos, cinemáticos e dinâmicos)
- Scripting em Lua com acesso a transform, física, input, raycast e áudio
- Áudio (miniaudio): Audio Source/Listener com som 3D
- Modo Play na aba Game (ou em janela separada), rodando uma cópia da cena, e o executável PrismRuntime para rodar um projeto sem o editor
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
    Assets/       AssetID, AssetMeta, AssetRegistry (identidade dos assets, .meta)
  shaders/        shaders GLSL (.vert/.frag), com #include e hot reload
PrismEditor/      executável do editor (ver "Estrutura do editor" em docs/arquitetura.md)
PrismRuntime/     executável que roda um projeto sem o editor
tests/            testes automatizados, opt-in (ver docs/arquitetura.md)
vendor/           dependências (glad e stb locais; o resto via FetchContent)
docs/             documentação técnica (GitHub)
  inengine/       guias para quem usa a engine (abrem no editor com F1)
```

## Documentação De desenvolvimento de desiçoes

- [Arquitetura](docs/arquitetura.md)
- [Editor](docs/editor.md)
- [Scripting Lua](docs/scripting-lua.md)
- [Scripting C++](docs/scripting-cpp.md)
- [Física](docs/fisica.md)
- [Renderização](docs/renderizacao.md)
- [Áudio](docs/audio.md)
- [Animação](docs/animacao.md)
- [Formatos de arquivo](docs/formatos-de-arquivo.md)

## Licença

MIT. Veja o arquivo [LICENSE](LICENSE).

Licenças das dependências em [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Roadmap

- [x] Importação de modelos 3D (OBJ/FBX/glTF/GLB) pelo editor (via Assimp), com um material por submesh e texturas embutidas extraídas
- [x] Áudio: Audio Source/Listener, som 3D, API Lua (miniaudio; ver [docs/audio.md](docs/audio.md))
- [x] API de scripting Lua mais completa (colisão/trigger, `Properties`, `Time`, `Scene`)
- [x] Scripts em C++ (módulo nativo DLL/.so; ver [docs/scripting-cpp.md](docs/scripting-cpp.md)); falta hot reload durante o Play
- [ ] BSP/CSG e ferramentas de construção de mapas
- [x] Modelo de iluminação com especular/PBR (Cook-Torrance GGX)
- [x] Ambiente com gradiente analítico de 3 cores (difuso e reflexo nos metais)
- [x] Skybox visível (cor, gradiente ou panorama HDR)

- [x] UI in-game (painéis, texto, imagens e botões com `OnClick`; ver `docs/ui.md`)
- [x] Reflexão de ambiente com IBL de verdade (irradiance/prefiltered maps a partir do panorama)
- [x] Callbacks de colisão em Lua (`OnCollisionEnter/Exit`)
- [x] Sombras para luzes Point/Spot e Cascaded Shadow Maps
- [ ] Editor de código com autocomplete e debugger
- [x] Vínculo vivo entre instâncias e seus prefabs/materiais
- [x] Aviso de alterações não salvas (dirty flag)
- [x] PrismRuntime: executável que roda um projeto sem o editor
- [ ] Exportar/empacotar o jogo (pasta distribuível com o Runtime + projeto)
