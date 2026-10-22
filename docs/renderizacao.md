# Renderização

OpenGL 4.5 Core, com glad como carregador. O `Renderer` é uma API estática enxuta; a viewport do editor, o preview de câmera e a janela de Play usam o mesmo `Renderer::DrawScene`.

## Pipeline de `DrawScene`

1. **Sombras** (se alguma luz Directional projeta sombra): desenha a cena do ponto de vista da luz num `ShadowMap`.
2. **Pré-passe de geometria**: normais em view-space e profundidade, do ponto de vista da câmera (`GeometryBuffer`).
3. **SSAO**: calcula a oclusão a partir do pré-passe, seguida de blur.
4. **Fundo (skybox)** (`Renderer::DrawSky`): preenche a tela antes da geometria. Ver a seção "Skybox".
5. **Passe de cor**: desenha cada entidade com `Transform` + `MeshRenderer`, usando a transform de mundo (`Scene::GetWorldTransform`).
6. **Partículas** (`Renderer::DrawParticles`): billboards com blend, sem escrever profundidade (mas testando contra a geometria opaca). Ver a seção "Partículas".

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

Os valores vêm do **`EnvironmentComponent` da cena** (o primeiro encontrado; sem nenhum valem os padrões do component). Cores em sRGB, o que o color picker mostra; o shader converte para linear. `Ambient` é a **intensidade** (padrão 0.10) e `u_Ambient` vale o brilho **médio** do ambiente com qualquer paleta: a normalização é calculada na CPU a partir das cores atuais (`u_EnvScale`). O Renderer não guarda configuração do usuário: `Renderer::DrawScene` reescreve o estado do quadro (`ApplyFrameState`) a cada chamada e limita tudo (exposição 0.05–16, `Ambient` 0–2, cores 0–1, NaN/inf viram o padrão).

As opções da **câmera** chegam por `CameraRenderSettings::From(camera)`: `ClearMode` (skybox da cena ou cor chapada), `ClearColor`, `Exposure`, `SSAO` (desligar poupa dois passes de tela cheia) e as opções de pós-processamento (veja abaixo). A viewport livre do editor usa os padrões.

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

A iluminação roda em espaço **linear**. A cor sólida (`AlbedoTint`/`Color`, escolhida em sRGB no color picker) é convertida para linear na entrada; texturas de albedo já chegam lineares (`GL_SRGB8_ALPHA8`). Os shaders de cena (`basic`, `sky`, `particle`) escrevem **HDR linear** (`u_OutputMode = 1`) num buffer `RGBA16F`, e o passe final (`PostProcess`) converte para a tela. `Renderer::DrawMesh` fora do `DrawScene` continua escrevendo a imagem final (modo 0).

### Pós-processamento (`PostProcess`)

`Renderer::DrawScene` desenha a cena num FBO HDR (um por contexto GLFW, porque FBOs não são compartilhados entre o editor e a janela de Play) e, no fim, `PostProcess::End` faz:

1. **Bloom** (opcional): descida em até 6 níveis (filtro de 13 amostras, com limiar de joelho suave no primeiro) e subida aditiva (filtro tenda de 9 amostras). `BloomThreshold` é o brilho linear mínimo (1 = branco) e `BloomIntensity` quanto entra na imagem.
2. **Composição**: soma o bloom, aplica **exposição → ACES → sRGB** (o mesmo de antes), depois `Saturation` e `Contrast`. Pixels de **fundo** (profundidade 1.0) com céu LDR (cor chapada, gradiente, panorama comum) pulam exposição e ACES, então a cor do céu sai exatamente como foi escolhida; o panorama **HDR** passa pelo tone mapping.
3. **FXAA** (opcional): suaviza bordas sobre a imagem já em sRGB.
4. **Profundidade**: é copiada para o destino original, então gizmos e linhas desenhados depois continuam ocluídos.

As opções são **por câmera** (`CameraComponent`: `Bloom`, `BloomThreshold`, `BloomIntensity`, `FXAA`, `Saturation`, `Contrast`; chegam por `CameraRenderSettings::From`) e limitadas no Renderer. Com bloom, saturação e contraste neutros e FXAA desligado, a imagem é a de antes, exceto partículas sobre geometria, que agora são misturadas em espaço linear. Se os shaders `post_*.frag` não compilarem ou o FBO falhar, a cena é desenhada direto, sem pós-processamento.

## Sombras

- Somente luz **Directional**, e só **uma** por frame: a primeira com `CastShadows` marcado. Uma segunda é ignorada em silêncio.
- Point e Spot com `CastShadows` marcado não produzem sombra.
- Um único frustum ortográfico, dimensionado pelo bounding box das posições de todas as entidades com `MeshRenderer` e recalculado a cada frame. Cenas muito grandes podem mostrar sombras com baixa resolução (Cascaded Shadow Maps seria a evolução).
- Shadow map de 2048×2048, filtragem PCF 3×3.
- Com sombra ativa, a cena é desenhada duas vezes por frame.

## SSAO

16 amostras hemisféricas, textura de ruído e blur por box filter. O resultado multiplica o termo de luz ambiente.

## Skybox

`EnvironmentComponent::Sky` (gravado no mapa, editado no component **Environment**) escolhe o fundo visível; uma câmera com `ClearMode = SolidColor` ignora o skybox e usa a própria cor:

| Modo | Resultado |
|---|---|
| Cor chapada | `SkyColor` (sRGB, escrita direto) |
| Gradiente | O mesmo gradiente Céu/Horizonte/Chão do ambiente, visível ao fundo |
| Panorama | Imagem equirretangular (2:1) escolhida por `SkyTexture` (`AssetID`, arraste do Conteúdo do Projeto): `.hdr`, `.png`, `.jpg`, `.tga`, `.bmp` |

- O desenho é um triângulo de tela cheia no plano distante (`sky.vert`), sem teste nem escrita de profundidade; a direção de cada pixel vem da matriz inversa de view-projection.
- O panorama é carregado por `SkyTexture` em `GL_RGB16F` linear, sem inverter (a primeira linha da imagem é o céu), com mipmaps e a correção da costura do `atan` no cálculo das derivadas. `.hdr` passa por exposição e ACES; imagens comuns só pela intensidade, para aparecerem como foram pintadas. `SkyRotation` gira em torno de Y.
- Panorama sem imagem válida (sem asset, vínculo quebrado, arquivo ausente) cai para a cor chapada; o erro de carga é logado uma vez por caminho.
- **O fundo não ilumina a cena.** A luz de ambiente e os reflexos continuam vindo do gradiente de 3 cores, qualquer que seja o modo. Iluminação por imagem (IBL) é um passo futuro.

## Partículas

`ParticleEmitterComponent` configura o emissor; `ParticleSystem` (`Particles/`) simula na CPU, por cena, e guarda o estado fora do arquivo de cena. Roda no Play (`Scene::OnUpdate`) e, no editor, anima como preview a entidade selecionada.

- Emissão em cone ao redor do eixo +Y local da entidade (gire a entidade para apontar o jato), com `EmissionRate`, `BurstCount`, vida/velocidade aleatórias entre mínimo e máximo, gravidade e tamanho/cor interpolados ao longo da vida. As partículas vivem em espaço de mundo.
- Desenho: um quad por partícula gerado no vertex shader (`particle.vert`), lendo posição/tamanho/cor de um SSBO reescrito por emissor; um VAO vazio por contexto (por isso funciona na janela de Play). `Additive` soma luz (fogo, brilho); sem ele usa alpha normal ordenado de trás para frente.
- Textura opcional (`TexturePath`, relativa ao projeto); sem ela, um círculo suave procedural. Sem iluminação, sem sombras, sem partículas ocultas pela fumaça de outras (sem *soft particles*).
- Limite de 10 000 partículas vivas por emissor.

## Meshes embutidas

Cube, Sphere, Capsule, Cylinder e Plane. A geometria é gerada uma vez na CPU (`PrimitiveMeshFactory`, sem chamadas OpenGL) e enviada à GPU em `Renderer::Init()`, com uma malha por primitiva reaproveitada por todas as entidades. Modelos externos (`.obj`/`.fbx`/`.gltf`/`.glb`) são importados pelo `ModelLoader` (Assimp) e referenciados por `MeshRendererComponent::ModelAsset`; quando válido, ele tem prioridade sobre a primitiva.

## Contexto compartilhado (Play)

A `PlayWindow` cria sua janela GLFW compartilhando o contexto OpenGL do editor, então VAOs, VBOs e shaders valem nas duas. Estado global de contexto (bind atual, viewport, depth test) **não** é compartilhado e é reconfigurado a cada troca de contexto.

## Linhas

`Renderer::DrawLines` usa um shader dedicado, sem iluminação, para os gizmos de câmera, collider, luz e raycast.

## Shaders

Os shaders GLSL ficam em `Prism/shaders/` (um `.vert` e um `.frag` por programa), não em strings no C++. `Renderer::Init` carrega cada um com `Shader::CreateFromFiles`; o mapa de arquivos, o `#include`, os defines injetados pelo C++ (`MAX_LIGHTS`) e o **hot reload** estão em [Prism/shaders/README.md](../Prism/shaders/README.md).

Resumo: salvar um shader com o editor aberto recompila em ~0,5 s (`Renderer::ReloadChangedShaders`, chamado por `Application::Run`). Se não compilar, a versão anterior continua em uso e o erro vai para o Console. A pasta é copiada para ao lado do executável no build, para o jogo distribuído não depender da árvore de código-fonte.
