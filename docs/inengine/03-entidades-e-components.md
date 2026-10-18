# Entidades e components

## A ideia

Uma cena é feita de **entidades**. Uma entidade sozinha é só um nome e uma posição; o que ela **é** (um cubo, uma luz, o jogador) vem dos **components** que você adiciona a ela em **Propriedades → + Add Component**. Isso é o modelo ECS (entidade/component), implementado com a biblioteca EnTT.

Toda entidade tem:

- **Nome** (tag): usado para achá-la por script (`Scene.Find("Jogador")`).
- **Transform**: posição, rotação (em graus) e escala. É **local**: relativa ao pai, se houver.

## Hierarquia

Uma entidade pode ser **filha** de outra (arraste na **Hierarquia**). O filho herda a posição, a rotação e a escala do pai: mover o pai move todos os filhos. Para posição "de verdade" no mundo, use `GetWorldPosition()` nos scripts.

> Física e hierarquia não se combinam bem: um corpo físico deve ser, de preferência, uma entidade **sem pai**. Veja [Física](07-fisica.md).

## Components disponíveis

| Component | Para que serve |
|---|---|
| **Mesh Renderer** | Faz a entidade aparecer: uma forma pronta (Cube, Sphere, Capsule, Cylinder, Plane) ou um modelo importado, com uma cor. |
| **Material** | Aparência PBR: textura de cor, normal map, roughness e metallic. |
| **Light** | Luz: Point, Spot ou Directional, com cor, intensidade, alcance e opção de projetar sombra (Directional). |
| **Camera** | Câmera do jogo (campo de visão, planos). A que estiver como `Primary` é a que vale no Play. |
| **Collider** | Forma de colisão: Box, Sphere, Capsule, ConvexHull ou TriangleMesh. Pode ser **Trigger**. |
| **Rigid Body** | Corpo físico: Static, Kinematic ou Dynamic, massa, atrito, gravidade. Precisa de um Collider. |
| **Script** | Liga um arquivo Lua da pasta `Scripts/` à entidade. |
| **Native Script** | Liga uma classe C++ do módulo do projeto à entidade. |
| **Raycast** | Dispara um raio e guarda o que ele acertou (sensor). |
| **Audio Source** | Toca um som (2D ou 3D). |
| **Audio Listener** | Marca a entidade que "ouve" (normalmente a câmera). |
| **Particle Emitter** | Emite partículas (fogo, fumaça, faíscas). |
| **UI Element** | Elemento de interface in-game: painel, texto, imagem ou botão. |

Cada component tem sua própria seção em **Propriedades**, com o botão **X** para removê-lo.

## Criar entidades

- Menu **Entidade**: **Criar Cubo**, **Criar Luz**, **Criar Character** (cubo verde com cápsula, corpo cinemático e slot de script), **Criar Camera**, **Criar UI** e **Criar Entidade Vazia**.
- Arrastar um **modelo** (`.obj`, `.fbx`, `.gltf`, `.glb`) ou um **prefab** do Conteúdo do Projeto para a Hierarquia ou a Viewport.
- Por script: `Scene.Create`, `Scene.CreateMesh` e `Scene.Instantiate`.

## Exemplos de combinações

- **Chão**: Mesh Renderer (Plane ou Cube esticado) + Collider (Box) + Rigid Body `Static`.
- **Caixa que cai**: Mesh Renderer + Collider + Rigid Body `Dynamic`.
- **Moeda coletável**: Mesh Renderer + Collider com **Trigger** + Script com `OnTriggerEnter`.
- **Personagem controlado por script**: Mesh Renderer + Collider (Capsule) + Rigid Body `Kinematic` + Script.
- **Fogueira**: Particle Emitter (aditivo) + uma Light laranja.
- **Placar na tela**: UI Element do tipo Texto + um script que muda o `Text`.
