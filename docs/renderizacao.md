# Renderização

OpenGL 4.5 Core, com glad como carregador. O `Renderer` é uma API estática enxuta; a viewport do editor, o preview de câmera e a janela de Play usam o mesmo `Renderer::DrawScene`.

## Pipeline de `DrawScene`

1. **Sombras** (se alguma luz Directional projeta sombra): desenha a cena do ponto de vista da luz num `ShadowMap`.
2. **Pré-passe de geometria**: normais em view-space e profundidade, do ponto de vista da câmera (`GeometryBuffer`).
3. **SSAO**: calcula a oclusão a partir do pré-passe, seguida de blur.
4. **Passe de cor**: desenha cada entidade com `Transform` + `MeshRenderer`, usando a transform de mundo (`Scene::GetWorldTransform`).

Nenhum chamador (`EditorLayer`, `PlayWindow`) precisa conhecer esses passes.

## Iluminação

- Até **16 luzes** por frame (`MAX_LIGHTS`): Point, Spot e Directional.
- Modelo **PBR metallic/roughness** (Cook-Torrance): distribuição GGX, geometria Smith (Schlick-GGX) e Fresnel-Schlick, com difuso Lambert e conservação de energia (`kD = (1 - F) * (1 - metallic)`). Sem `MaterialComponent`, a entidade usa roughness 1 e metallic 0 (fosca, visual equivalente ao Lambert antigo).
- **Ambiente** fixo (`Renderer::SetAmbient`, padrão 0.10), multiplicado por albedo e SSAO. Não há reflexão de ambiente/IBL: **metais ficam escuros** onde nenhuma luz os atinge, e metais muito rugosos perdem energia (limitação conhecida do Smith de espalhamento único).
- Faces de costas para a câmera são descartadas no fragment shader (`discard`) em vez de usar `glCullFace`, porque as primitivas (`PrimitiveMeshFactory`) não têm winding order consistente entre si. Normalizar o winding e migrar para `glCullFace` é uma otimização possível.

## Materiais

`MeshRendererComponent::Color` é a cor de fallback. Um `MaterialComponent` opcional adiciona:

| Campo | Efeito hoje |
|---|---|
| `AlbedoPath` | textura de cor (sRGB), multiplicada por `AlbedoTint` |
| `NormalPath` | normal map em tangent-space (linear) |
| `RoughnessMetallicPath`, `RoughnessFactor`, `MetallicFactor` | mapa em convenção glTF (G = roughness, B = metallic, linear); os fatores multiplicam o mapa ou valem sozinhos sem ele |

Caminhos são relativos a `Assets/`; vazio significa "sem textura". Texturas são carregadas com stb_image e mantidas em cache por (caminho, sRGB).

## Pipeline de cor

A iluminação roda em espaço **linear**. A cor sólida (`AlbedoTint`/`Color`, escolhida em sRGB no color picker) é convertida para linear na entrada; texturas de albedo já chegam lineares (`GL_SRGB8_ALPHA8`). Na saída: exposição (`Renderer::SetExposure`, padrão 1.0) → tone mapping ACES → linear para sRGB. A conversão é feita no shader de cena, e não num passe final, porque gizmos de linha e o clear color são desenhados depois no mesmo framebuffer, já em espaço de tela.

## Sombras

- Somente luz **Directional**, e só **uma** por frame: a primeira com `CastShadows` marcado. Uma segunda é ignorada em silêncio.
- Point e Spot com `CastShadows` marcado não produzem sombra.
- Um único frustum ortográfico, dimensionado pelo bounding box das posições de todas as entidades com `MeshRenderer` e recalculado a cada frame. Cenas muito grandes podem mostrar sombras com baixa resolução (Cascaded Shadow Maps seria a evolução).
- Shadow map de 2048×2048, filtragem PCF 3×3.
- Com sombra ativa, a cena é desenhada duas vezes por frame.

## SSAO

16 amostras hemisféricas, textura de ruído e blur por box filter. O resultado multiplica o termo de luz ambiente.

## Meshes embutidas

Cube, Sphere, Capsule, Cylinder e Plane. A geometria é gerada uma vez na CPU (`PrimitiveMeshFactory`, sem chamadas OpenGL) e enviada à GPU em `Renderer::Init()`, com uma malha por primitiva reaproveitada por todas as entidades. Não há importação de modelos externos ainda.

## Contexto compartilhado (Play)

A `PlayWindow` cria sua janela GLFW compartilhando o contexto OpenGL do editor, então VAOs, VBOs e shaders valem nas duas. Estado global de contexto (bind atual, viewport, depth test) **não** é compartilhado e é reconfigurado a cada troca de contexto.

## Linhas

`Renderer::DrawLines` usa um shader dedicado, sem iluminação, para os gizmos de câmera, collider, luz e raycast.
