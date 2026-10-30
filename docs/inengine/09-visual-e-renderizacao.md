# Visual e renderização

## Luzes

Adicione o component **Light** (ou use **Entidade → Criar Luz**). Tipos:

- **Point**: luz de lâmpada, em todas as direções, com `Range` (alcance).
- **Spot**: cone, com `SpotAngle`.
- **Directional**: luz do sol, vinda de uma direção (a rotação da entidade define para onde aponta).

Cada luz tem **Color** e **Intensity**. São suportadas até **16 luzes** por frame.

**Sombras**: marque `CastShadows` na luz. Directional (a primeira marcada) usa 4 cascatas; até **4 Spot** e **2 Point** projetam sombra por vez. As que passarem do limite continuam acesas, sem sombra.

## Materiais

Sem Material, a entidade usa a cor do Mesh Renderer. Com o component **Material** você tem aparência **PBR**:

| Campo | Efeito |
|---|---|
| **Albedo** | Textura de cor (multiplicada pelo `AlbedoTint`) |
| **Normal** | Normal map, para relevo |
| **RoughnessMetallic** | Mapa no padrão glTF (verde = rugosidade, azul = metálico), mais os fatores `Roughness` e `Metallic` |

Rugosidade alta = fosco; baixa = brilhante. Metálico = reflete o ambiente. Arraste imagens do Conteúdo do Projeto para os slots. Modelos importados já trazem seus materiais.

## Ambiente e skybox (por cena)

O visual da cena vive na entidade **Ambiente**, com o component **Environment**. Todo mapa novo já nasce com uma; para criar uma, use **Entidade → Criar Ambiente** (só uma por cena). Ele é gravado no mapa, então cada mapa tem o seu visual:

- **Intensidade**: o quanto o ambiente ilumina. Com 0, só as luzes iluminam.
- **Céu, Horizonte e Chão**: as três cores do ambiente. É o que dá um tom às sombras e o reflexo dos metais.
- **Fundo (skybox)**: o que aparece atrás da cena.

| Modo | Resultado |
|---|---|
| Cor chapada | Uma cor única |
| Gradiente | O mesmo gradiente Céu/Horizonte/Chão do ambiente |
| Panorama | Uma imagem equirretangular (2:1): `.hdr`, `.png`, `.jpg`, `.tga` ou `.bmp` |

Há rotação e brilho para o panorama. Com um panorama válido, ele **também ilumina a cena** (IBL): o difuso e os reflexos dos materiais vêm da imagem, e rotação, brilho e Intensidade do ambiente valem para a luz. Sem panorama, a luz de ambiente vem do gradiente de três cores.

## Opções de render da câmera

O component **Camera** tem a seção **Render**: **Fundo** (skybox da cena ou uma cor chapada), **Exposição** (brilho final, 1.0 é neutro) e **SSAO**, e a seção **Pós-processamento**: **Bloom** (limiar e intensidade), **FXAA**, **Saturação** e **Contraste**. Cada câmera pode ter as suas. Por script: `entity:GetCamera().Exposure`, `entity:GetEnvironment().Ambient` etc. (bom para ciclo dia/noite).

## Efeitos que já vêm ligados

- **SSAO**: escurece cantos e frestas, dando profundidade.
- **Tone mapping** (ACES) e correção de cor, para a imagem não estourar.

## Partículas

Adicione **Particle Emitter**. Principais campos: taxa de emissão, `Burst` inicial, vida e velocidade (mínimo/máximo), abertura do cone, gravidade, tamanho e cor no começo e no fim da vida, textura opcional e **Aditivo** (soma luz: bom para fogo e brilho). As partículas saem na direção do eixo +Y da entidade; gire a entidade para apontar o jato. Fora do Play, a entidade selecionada mostra uma prévia. Por script: `entity:PlayParticles()` e `StopParticles()`. O limite é de 10 000 partículas vivas por emissor.

## Modelos 3D

Formatos: `.obj`, `.fbx`, `.gltf`, `.glb`. Arraste o arquivo da pasta do projeto para a Hierarquia ou a Viewport; o modelo entra com um submesh por material, com as texturas e os fatores PBR do arquivo.

## Shaders

Os shaders ficam na pasta `Prism/shaders/` da engine. Com o editor aberto, salvar um shader o recompila em instantes (se der erro, a versão anterior continua e o erro aparece no Console).
