# Shaders GLSL

Todos os shaders da engine ficam aqui, em arquivos (antes eram strings dentro de `Renderer.cpp`). São lidos em tempo de execução por `Shader::CreateFromFiles` (`Renderer/Shader.h`) e preparados por `ShaderSource` (`Renderer/ShaderSource.h`).

## Mapa de arquivos

| Programa (`Renderer::s_*Shader`) | Vertex | Fragment | Função |
|---|---|---|---|
| `BasicLit` | `basic.vert` | `basic.frag` | Shader principal: PBR Cook-Torrance, multi-luz, sombras, SSAO, ambiente, tone mapping |
| `Line` | `line.vert` | `line.frag` | Linhas de gizmo (cor sólida) |
| `ShadowDepth` | `shadow_depth.vert` | `shadow_depth.frag` | Passe de profundidade do ponto de vista da luz |
| `GeometryPrePass` | `geometry_prepass.vert` | `geometry_prepass.frag` | SSAO, etapa 1: normais em view-space |
| `SSAO` | `fullscreen_quad.vert` | `ssao.frag` | SSAO, etapa 2: oclusão bruta |
| `SSAOBlur` | `fullscreen_quad.vert` | `ssao_blur.frag` | SSAO, etapa 3: blur |

Pipeline do SSAO completo (geometria → SSAO bruto → blur → passe de cor final): ver `Renderer/SSAO.h`. O comentário de design de cada shader está no topo do próprio arquivo.

## Hot reload

Com o editor aberto, edite e salve qualquer arquivo desta pasta: em até ~0,5 s o shader é recompilado e a viewport atualiza, sem reiniciar.

- Se a nova versão **não compilar**, a anterior continua em uso e o erro aparece no Console do editor. Corrigir e salvar recompila.
- Em desenvolvimento a engine lê esta pasta diretamente (caminho embutido pelo CMake em `PRISM_SHADER_SOURCE_DIR`). Fora da árvore do projeto, usa a cópia `shaders/` ao lado do executável (feita no POST_BUILD).
- `Renderer::SetShaderHotReload(false)` desliga a verificação.

## `#include`

```glsl
#include "common.glsl"
```

- Resolvido primeiro ao lado do arquivo que inclui e depois nesta pasta.
- Cada arquivo entra **no máximo uma vez** por shader (`#pragma once` implícito); inclusão circular é erro.
- Textual: ignora `#if`/`#ifdef`.
- Erros do driver (`1:23`): o número é o índice do arquivo, listado na linha `arquivos do shader: 0=..., 1=...` do log.

## Defines vindos do C++

O Renderer injeta `#define`s logo depois do `#version`. Hoje: `MAX_LIGHTS` no `basic.frag` (valor de `Prism::MAX_LIGHTS`). Sem ele, o `basic.frag` falha de propósito com um `#error`.

## Validar fora da engine

```bash
glslangValidator -l -DMAX_LIGHTS=16 basic.frag
```

O teste `PrismShaderTests` (`tests/`) expande todos os shaders desta pasta, então um include quebrado ou arquivo renomeado falha no CI.
