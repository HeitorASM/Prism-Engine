# Level Builder (geometria estilo ProBuilder)

Objetivo: montar e editar a geometria de um mapa **dentro da engine**, para prototipar levels e demos rápido (blocos, rampas, escadas, paredes com janela...), sem sair para um programa de modelagem.

## Estado

| Etapa | O quê | Situação |
|---|---|---|
| 4a | `Level/EditableMesh`: malha poligonal editável, formas, extrude, inset, UV automático, Build, Raycast, arquivo | **pronta** (testada) |
| 4b | `LevelMeshComponent` na cena: renderer (cache de GPU), sombras, SSAO, colisão, picking, `.prismmap`/prefab, undo | **pronta** (sintaxe verificada; ver "Como testar") |
| 4c | Ferramenta no editor: criar formas, selecionar vértice/aresta/face, mover com gizmo + snap, extrudar/inset/..., UV e materiais por slot | **pronta** (sintaxe verificada) |
| 4d | Extras na malha: subdividir, destacar, espelhar, combinar, recentralizar pivô, arestas únicas | **pronta** (testada: 142 verificações) |
| 5 | Loop cut, bevel, seleção por caixa/laço, oclusão no overlay, junção de objetos (static batching), "kit" modular com snap | ideias |

## Como usar no editor

1. Menu **Entidade > Criar Level Builder** e escolha uma forma (Caixa, Sala = caixa invertida, Plano, Cilindro, Escada, Rampa). Também dá para **Add Component > Level Mesh** (nasce uma caixa 1x1x1).
2. Com o objeto selecionado, **Tab** (ou o botão *Editar nível* na barra da viewport / na seção *Level Mesh*) liga a edição. **Esc** ou Tab de novo sai.
3. Modos de seleção: **1** vértice, **2** aresta, **3** face. Clique seleciona; **Shift** soma/alterna. Trocar de modo converte a selecao (face → seus vértices; vértices → faces totalmente cobertas).
4. O **gizmo de mover** aparece no centro da seleção. **Snap de grade** ligado por padrão (tamanho no painel); **Ctrl** inverte o snap durante o arraste.
5. Operações (painel ou atalho): **Extrudar** (Ctrl+E, distância no painel; depois ajuste com o gizmo), **Inset** (Ctrl+I), **Subdividir**, **Destacar**, **Inverter**, **Excluir** (Del), **Selecionar todos** (Ctrl+A), mesclar vértices próximos, espelhar X/Y/Z, centralizar pivô.
6. **Faces selecionadas**: slot de material, grupo de suavização e UV (deslocamento, repetição, rotação, inverter U/V; vale para todas as faces selecionadas).
7. **Materiais por slot**: cor, texturas (arraste do painel de conteúdo), rugosidade, metálico, repetição, emissão, transparência, dupla face. O `Material` da própria entidade é **ignorado** nesta entidade.
8. **Colisão**: botão *Adicionar colisão* cria `Collider (TriangleMesh)` + `Rigid Body (Static)`. A malha usada é a que existe ao entrar no Play. `TriangleMesh` só vale para corpos Static/Kinematic; para Dynamic o Jolt usa `ConvexHull`.

Cada operação e cada gesto de arrastar é **um passo de undo/redo** (o comando guarda a malha inteira antes/depois).

## Como funciona por dentro (4b)

- `LevelMeshComponent` (`Components.h`): `EditableMesh Mesh`, `std::vector<MaterialComponent> Materials` (um por slot), `Layers`, `CastShadows` e `Revision`.
- **`Revision`** identifica o *conteúdo* da geometria. Quem altera a malha chama `Touch()` depois (o editor já faz). Cópias do componente (duplicar, undo, Play) mantêm a mesma revisão porque têm o mesmo conteúdo.
- **Renderer**: `Renderer::ResolveLevelMesh` converte `Build()` em um `Mesh` por slot usado e guarda num cache indexado por `Revision` (descarta o que não é visto há ~300 quadros). Componentes não guardam objetos OpenGL. Cada slot entra na lista de visíveis (`VisibleMesh`) com o próprio material, então batching, instancing, SSAO e sombras funcionam sem caminho especial. Culling por frustum e camadas valem como nos MeshRenderers; sem esqueleto.
- **Física**: `Collider` com `ConvexHull`/`TriangleMesh` usa os triângulos de `Build()` quando a entidade tem `LevelMeshComponent` (com a escala de mundo), no lugar da malha do MeshRenderer.
- **Picking**: `Scene::VisualRaycast` testa a malha exata (nao so a caixa), para clicar numa sala ou escada sem pegar o "ar" dentro da caixa.
- **Arquivo**: bloco de component `Level Mesh` (versão 1): camadas, sombra, nº de materiais + corpo de cada material (mesmo formato do bloco `Material` v4, agora compartilhado em `ComponentRegistration.cpp`), depois o `EditableMesh` binário. Leitura recusa dado truncado/corrompido. Cenas e prefabs usam o registro de components, sem mudar a versão do formato de cena.
- **Excluir entidade** agora desfaz restaurando também o `LevelMeshComponent`.

### Limitações conhecidas

- O overlay de edição é um wireframe 2D **sem oclusão** (você vê vértices atrás da malha) e a seleção por clique escolhe o mais próximo na tela.
- Subdividir uma face deixa uma **junção em T** com a vizinha não subdividida (visualmente invisível, mas `IsClosed()` passa a dizer "aberta"); subdivida todas as faces vizinhas se precisar de malha fechada.
- Cada quadro de arraste do gizmo reconstrói a malha de GPU (entradas antigas são descartadas pelo cache); para malhas com milhares de faces isso pode pesar.
- Mudar a geometria durante o Play (script) exige `Touch()` no componente; ainda não há API Lua.

## `EditableMesh` (Etapa 4a)

`Prism/src/Prism/Level/EditableMesh.h` — só `std` + `glm` (sem OpenGL), testado em `tests/EditableMeshTests.cpp`.

- **Vértices compartilhados** e **faces de N lados** (quad, n-gon, até polígonos côncavos planares). Cada face guarda `MaterialSlot`, `SmoothingGroup` (0 = chapado) e ajustes de UV (`Offset`, `Tiling`, `RotationDeg`, `FlipU/V`).
- **Formas**: `CreateBox`, `CreatePlane` (subdividido), `CreateCylinder`, `CreateStairs`, `CreateRamp`.
- **Edição**: `ExtrudeFaces` (para fora ou para dentro; paredes só na borda da seleção), `InsetFaces` (moldura de largura constante), `MoveVertices`, `TransformVertices`, `FlipFaces`, `DeleteFaces`, `SetFaceMaterial`, `SetSmoothingGroup`, `MergeVertices` (solda), `RemoveUnusedVertices`. Devolvem os índices das faces para o editor manter a seleção.
- **Consulta**: `IsValid`, `IsClosed` (superfície fechada: o requisito para colisão sólida), `FaceNormal/Center/Area`, `GetBounds`, `VerticesOfFaces`, `Raycast` (seleção de face na viewport).
- **Saída**: `Build()` triangula (ear clipping), calcula normais (chapadas ou suavizadas por grupo), UV e tangentes e devolve **um `BuiltSubmesh` por slot de material usado**. A conversão para `Mesh` (GPU) e para o colisor `TriangleMesh` do Jolt é a Etapa 4b.
- **Arquivo**: `Write`/`Read` binário e versionado. `Read` rejeita (sem travar) dado truncado ou corrompido e deixa o destino intacto.

### Convenções

- Frente da face = sentido **anti-horário visto de fora** (mesmo critério de `PrimitiveMeshFactory`). Y para cima. Formas centradas na origem.
- **UV por posição**: o mesmo ponto 3D sempre tem o mesmo UV, com 1 unidade de mundo = 1 repetição (`Tiling` muda isso). Faces coplanares vizinhas emendam sem costura e a textura não estica quando a face muda de tamanho — o que importa em protótipo.
- Índices de **face** mudam depois de `DeleteFaces`/`MergeVertices`; os de **vértice**, depois de `RemoveUnusedVertices`/`MergeVertices`.
- `ExtrudeFaces`: cada vértice anda pela média das normais das faces selecionadas que o usam (para uma região plana, é a normal da face).
- `InsetFaces` exige `amount` menor que metade da menor largura da face (não há proteção contra a face "virar do avesso").

### Exemplo

```cpp
using namespace Prism::Level;
EditableMesh room = EditableMesh::CreateBox({ 6, 3, 6 });
room.FlipFaces({ 0, 1, 2, 3, 4, 5 });                 // paredes viradas para dentro
// janela: inset na parede da frente e apaga a face de dentro (buraco com moldura)
uint32_t wall = /* face com normal -Z, escolhida por Raycast ou FaceNormal */ 0;
room.InsetFaces({ wall }, 0.6f);
room.DeleteFaces({ wall });
std::vector<BuiltSubmesh> parts = room.Build();       // um por material
```

## Operações extras (4d)

- `UniqueEdges()` (arestas únicas, a < b), `FacesUsingVertices`.
- `SubdivideFaces`: cada face vira quads ao redor do centro (4 quads num quad); pontos médios compartilhados entre faces vizinhas selecionadas; mantém material/UV/grupo.
- `DetachFaces`: duplica os vértices das faces (deixam de ser compartilhados) para mover só elas.
- `Mirror(eixo)`: reflete pela origem e inverte as faces (normais continuam para fora).
- `Append(outra, matriz)`: junta outra malha (espelho corrige o sentido das faces).
- `RecenterOnBounds()`: origem no centro da caixa envolvente; devolve o deslocamento.

## Como testar

- `PrismEditableMeshTests` (no CMake de `tests/`): geometria, extrude/inset, UV, arquivo, raycast e as operações 4d (142 verificações).
- No editor: criar uma Sala, entrar na edição (Tab), selecionar a face da frente, `Inset` + `Excluir` para abrir uma janela, `Extrudar` o chão, atribuir um segundo slot com textura; adicionar colisão e dar Play com um Character.
- Salvar o mapa, reabrir, e conferir que a geometria, os slots e a colisão voltam iguais; Ctrl+Z/Ctrl+Y em cada passo.
