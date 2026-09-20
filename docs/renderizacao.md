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
- **Ambiente**: gradiente analítico de 3 cores (ver a seção "Ambiente" abaixo), multiplicado por albedo e SSAO. Metais refletem o gradiente em vez de ficarem pretos onde nenhuma luz os atinge. Metais muito rugosos ainda perdem energia (limitação conhecida do Smith de espalhamento único).
- Faces de costas para a câmera são descartadas no fragment shader (`discard`) em vez de usar `glCullFace`, porque as primitivas (`PrimitiveMeshFactory`) não têm winding order consistente entre si. Normalizar o winding e migrar para `glCullFace` é uma otimização possível.

## Materiais

`MeshRendererComponent::Color` é a cor de fallback. Um `MaterialComponent` opcional adiciona:

| Campo | Efeito hoje |
|---|---|
| `AlbedoPath` | textura de cor (sRGB), multiplicada por `AlbedoTint` |
| `NormalPath` | normal map em tangent-space (linear) |
| `RoughnessMetallicPath`, `RoughnessFactor`, `MetallicFactor` | mapa em convenção glTF (G = roughness, B = metallic, linear); os fatores multiplicam o mapa ou valem sozinhos sem ele |

Caminhos são relativos a `Assets/`; vazio significa "sem textura". Texturas são carregadas com stb_image e mantidas em cache por (caminho, sRGB).

## Ambiente

O ambiente é um **gradiente vertical analítico** de 3 cores (zênite, horizonte, chão), sem textura nem cubemap. Como ele depende só da altura `y` da direção, tudo é calculado em forma fechada no shader (`EnvironmentColor`, `EnvironmentIrradiance`, `EnvironmentBRDF`, `EnvironmentSpecular`):

- **Difuso**: `albedo * irradiância(normal.y)`. A irradiância é linear nas 3 cores (`zênite*Wz + horizonte*Wh + chão*Wg`, com 3 pesos escalares de grau 2 em `normal.y`), então **qualquer paleta funciona sem reajustar constantes**.
- **Especular**: `F * mix(env(R), irradiância(R.y), roughness^1.25)`. É o que impede o metal preto: sem luz direta, um metal reflete o ambiente.
- **Termo BRDF** `(A, B)`: aproximação de Karis (Unreal 4) mais uma correção polinomial própria, restrita fisicamente (`A` em [0,1], `A+B <= 1`).

API: `Renderer::SetEnvironmentColors(zenith, horizon, ground)` (sRGB, o que o color picker mostra; o shader converte para linear) e `Renderer::SetAmbient` (a **intensidade**). O padrão é 0.10, e `u_Ambient` vale o brilho **médio** do ambiente com qualquer paleta: a normalização é calculada na CPU a partir das cores atuais (`u_EnvScale`).

**Precisão medida** (contra integração numérica): irradiância, erro máximo 0.009 (0.008 com uma paleta de pôr do sol, sem reajuste); especular, erro médio 1.5 níveis de 255; termo BRDF, `A` com erro médio 0.018.

**Limites conhecidos:**

- É uma **aproximação**, não IBL: não há reflexo de objetos da cena nem de imagem HDR. As funções `Environment*` do shader são o ponto de troca quando existir IBL de verdade.
- O ambiente agora tem **cor**. Com o gradiente padrão (céu azul), um material quente (o laranja padrão) recebe cerca de **7% menos** brilho de ambiente que com o cinza uniforme antigo, e um azul puro cerca de 49% mais. Materiais neutros (cinza, branco) ficam idênticos. Para reproduzir o visual antigo, use as 3 cores iguais.
- Superfícies foscas ganham pouca variação topo/fundo (cerca de 1.7x); o contraste aparece nos metais.
- O ajuste do termo BRDF vale para `0.2 <= roughness <= 1`. Abaixo disso ele extrapola e o resultado é apenas limitado fisicamente (sem valores impossíveis, mas menos preciso).
- Com as 3 cores em preto, o ambiente é zero (a normalização devolve 0 em vez de dividir por zero).

## Matriz normal

A normal é transformada por `transpose(inverse(mat3(model)))` (`u_NormalMatrix`, calculada uma vez por objeto na CPU em `Renderer::ComputeNormalMatrix`), e não por `mat3(model)`. Com escala **não uniforme** a segunda distorce a normal (ela deixa de ser perpendicular à superfície: cerca de 50° de erro com escala 3×1×1) e o brilho especular sai torto. Com escala uniforme as duas dão a mesma direção. O prepass do SSAO faz o mesmo em view-space (`u_ViewNormalMatrix`, a matriz normal de `view * model`).

A **tangente** continua usando `mat3(model)` de propósito: ela é um vetor ao longo da superfície, e usar a matriz normal nela quebraria a base TBN do normal map.

Se o determinante da matriz é zero (um eixo de escala em 0), a inversa não existe e `ComputeNormalMatrix` devolve `mat3(model)` para não gerar NaN. Escala negativa (espelho) funciona normalmente.

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
