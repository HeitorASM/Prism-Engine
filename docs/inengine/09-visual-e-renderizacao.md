# Visual e renderização

## Luzes

Adicione o component **Light** (ou use **Entidade → Criar Luz**). Tipos:

- **Point**: luz de lâmpada, em todas as direções, com `Range` (alcance).
- **Spot**: cone, com `SpotAngle`.
- **Directional**: luz do sol, vinda de uma direção (a rotação da entidade define para onde aponta).

Cada luz tem **Color** e **Intensity**. São suportadas até **16 luzes** por frame.

**Sombras**: só a **primeira luz Directional** com `CastShadows` marcado projeta sombra (uma por cena). Point e Spot não projetam sombra ainda.

## Materiais

Sem Material, a entidade usa a cor do Mesh Renderer. Com o component **Material** você tem aparência **PBR**:

| Campo | Efeito |
|---|---|
| **Albedo** | Textura de cor (multiplicada pelo `AlbedoTint`) |
| **Normal** | Normal map, para relevo |
| **RoughnessMetallic** | Mapa no padrão glTF (verde = rugosidade, azul = metálico), mais os fatores `Roughness` e `Metallic` |

Rugosidade alta = fosco; baixa = brilhante. Metálico = reflete o ambiente. Arraste imagens do Conteúdo do Projeto para os slots. Modelos importados já trazem seus materiais.

## Ambiente e exposição

O menu **Renderização** controla o visual do projeto:

- **Exposição**: brilho final da imagem (1.0 é neutro).
- **Intensidade**: o quanto o ambiente ilumina. Com 0, só as luzes iluminam.
- **Céu, Horizonte e Chão**: as três cores do ambiente. É o que dá um tom às sombras e o reflexo dos metais.

## Fundo (skybox)

**Renderização → Fundo (Skybox)** escolhe o que aparece atrás da cena:

| Modo | Resultado |
|---|---|
| Cor chapada | Uma cor única |
| Gradiente | O mesmo gradiente Céu/Horizonte/Chão do ambiente |
| Panorama | Uma imagem equirretangular (2:1): `.hdr`, `.png`, `.jpg`, `.tga` ou `.bmp` |

Há rotação e intensidade para o panorama. **Importante:** o fundo **não ilumina** a cena; a luz de ambiente continua vindo do gradiente de três cores.

## Efeitos que já vêm ligados

- **SSAO**: escurece cantos e frestas, dando profundidade.
- **Tone mapping** (ACES) e correção de cor, para a imagem não estourar.

## Partículas

Adicione **Particle Emitter**. Principais campos: taxa de emissão, `Burst` inicial, vida e velocidade (mínimo/máximo), abertura do cone, gravidade, tamanho e cor no começo e no fim da vida, textura opcional e **Aditivo** (soma luz: bom para fogo e brilho). As partículas saem na direção do eixo +Y da entidade; gire a entidade para apontar o jato. Fora do Play, a entidade selecionada mostra uma prévia. Por script: `entity:PlayParticles()` e `StopParticles()`. O limite é de 10 000 partículas vivas por emissor.

## Modelos 3D

Formatos: `.obj`, `.fbx`, `.gltf`, `.glb`. Arraste o arquivo da pasta do projeto para a Hierarquia ou a Viewport; o modelo entra com um submesh por material, com as texturas e os fatores PBR do arquivo.

## Shaders

Os shaders ficam na pasta `Prism/shaders/` da engine. Com o editor aberto, salvar um shader o recompila em instantes (se der erro, a versão anterior continua e o erro aparece no Console).
