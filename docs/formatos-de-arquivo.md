# Formatos de arquivo

| Extensão | Conteúdo | Formato | Pasta |
|---|---|---|---|
| `.prismproj` | configuração do projeto | texto (`Chave=Valor`) | raiz do projeto |
| `.prismmap` | cena completa | binário | `Maps/` |
| `.prismprefab` | entidade e subárvore | binário | `Assets/Prefabs/` |
| `.prismmat` | um material | binário | `Assets/Materials/` |
| `.meta` | identidade de um asset | texto (`Chave=Valor`) | ao lado de cada asset |

Os arquivos binários usam little-endian e começam com um cabeçalho `magic` (4 bytes) + `versão` (uint32).

## `.prismproj`

Texto simples, legível e adequado ao controle de versão. Linhas iniciadas por `#` são comentários.

```
Name=MeuProjeto
AssetDirectory=Assets
ScriptDirectory=Scripts
MapDirectory=Maps
StartMap=Cena.prismmap
EngineVersion=0.1.0
```

`StartMap` é o mapa aberto automaticamente ao carregar o projeto.

`Layer0=` … `Layer31=` (opcionais) são os nomes das camadas de visibilidade 1 a 32; sem a chave, o nome é "Camada N".

`EngineVersion` é a versão da engine (`Version.py`) que criou o projeto. Só serve para o editor avisar quando difere da atual; projetos sem a chave abrem normalmente. O arquivo é gravado de forma atômica (arquivo temporário + rename), então um crash no meio do save não o corrompe. Um projeto novo também recebe um `.gitignore` com `Cache/` e `Binaries/`.

> As opções de render **não ficam mais no projeto**: o ambiente e o skybox são do `EnvironmentComponent` da cena (gravado no `.prismmap`) e exposição, fundo e SSAO são do `CameraComponent`. Chaves antigas (`Exposure`, `Ambient`, `Env*`, `Sky*`) em um `.prismproj` existente são ignoradas ao ler e desaparecem no próximo save.

## `.prismmap`

- Magic: `PRSM`
- Versão atual: **21** (`kSceneFormatVersion` em `SceneSerializer.cpp`). Mapas **v19 e v20** ainda são lidos (e regravados no formato atual ao salvar); formatos anteriores são recusados.

Contém o nome da cena e, para cada entidade: tag, transform, os components como **blocos** e o índice do pai (`-1` = sem pai). `RelationshipComponent::Children` não é gravado; é reconstruído a partir dos índices de pai depois que todas as entidades são criadas.

Cada component presente é um bloco `[nome][versão][tamanho][bytes]` (`ComponentRegistry::WriteComponentBlocks`). O nome é o `DisplayName` do registro e a versão é a do component (`ComponentTypeInfo::Version`). Como o bloco tem tamanho:

- um component **desconhecido** (de uma engine mais nova) é pulado com um aviso, e o resto do mapa abre;
- um bloco com dados inválidos descarta **só aquele component** (erro no Console);
- **campo novo no fim de um component** é só subir a `Version` dele e ler o campo novo quando `ComponentRegistry::CurrentBlockVersion()` for suficiente: **não** precisa subir `kSceneFormatVersion` nem invalida mapas antigos.

A gravação é atômica (arquivo temporário + rename): um crash no meio do save nunca corrompe o mapa existente.

O layout exato campo a campo é definido pelos blocos `Serialize`/`Deserialize` de cada component em `ComponentRegistration.cpp`, e o histórico de versões está nos comentários de `kSceneFormatVersion`. Resumo:

| Versão | Mudança |
|---|---|
| v2 | `Light`, `Collider`, `RigidBody` e `Script` (opcionais, com flag de presença) |
| v3 | `Camera` |
| v4 | `Relationship` (parenting; só o índice do pai é gravado) |
| v5–v9 | campos novos em `Light`, `RigidBody` e `Raycast` |
| v10 | `Material` |
| v11 | `Material::LinkedAsset` (vínculo vivo com `.prismmat`) |
| v12 | `PrefabInstanceRoot`/`PrefabInstanceMember` (vínculo vivo com prefab, bloco à parte) |
| v13 | `MeshRenderer::ModelAsset` (modelo importado) |
| v14 | `MeshRenderer::SubmeshIndex` (qual material/submesh do modelo a entidade desenha; `-1` = malha mesclada) |
| v15 | `AudioSource` e `AudioListener` |
| v16 | `Script::Properties` (variáveis públicas editadas) |
| v17 | `NativeScript` (nome da classe do script C++) |
| v18 | `ParticleEmitter` (emissor de partículas) |
| v19 | `UIElement` (elemento de UI in-game) |
| v20 | `Environment` (ambiente e skybox da cena) e `Camera` com `ClearMode`, `ClearColor`, `Exposure`, `SSAO` |
| (bloco Camera v3) | `Camera` ganha `Bloom`, `BloomThreshold`, `BloomIntensity`, `FXAA`, `Saturation`, `Contrast` no fim do bloco; **sem** subir a versão do mapa |
| (bloco Camera v4) | `Camera` ganha `CullMask` (camadas que enxerga) no fim do bloco; **sem** subir a versão do mapa |
| (bloco MeshRenderer v2) | `MeshRenderer` ganha `Layers` (camadas em que está) no fim do bloco; **sem** subir a versão do mapa |
| v21 | components em **blocos** com nome, versão e tamanho (antes: flag + campos na ordem do registro) |

> O número da versão **vive no código**. Ao subir `kSceneFormatVersion` (só quando a estrutura **fora** dos blocos muda), atualize "Versão atual" acima e esta tabela.

Do `RaycastComponent` só `TargetPosition`, `Enabled` e `IgnoreParentAndSiblings` são gravados; os campos de resultado (`Hit`, `HitPoint` etc.) são de runtime.

## Modelos 3D (`.obj`, `.fbx`, `.gltf`, `.glb`)

Importados via Assimp (`Assets/ModelLoader`). Os triângulos são agrupados **por material** (um submesh por material do arquivo, com a hierarquia de nós já aplicada nos vértices). Arrastar o modelo para a **Hierarquia** ou a **Viewport** cria uma entidade raiz com o nome do arquivo e, se houver mais de um material, uma entidade filha por material; cada uma recebe `MeshRenderer` (com `SubmeshIndex`) e `Material` já preenchido (albedo, normal, roughness/metallic, cor base e fatores).

- Texturas externas são procuradas relativas à pasta do modelo (ou só pelo nome do arquivo ali).
- Texturas embutidas (ex.: dentro de um `.glb`) são extraídas para `<pasta do modelo>/<nome>_textures/`.
- A textura roughness/metallic só é importada de glTF (canal G = roughness, canal B = metallic).
- UVs seguem a convenção OpenGL (V para cima), igual às texturas carregadas pela engine; **não** use `aiProcess_FlipUVs`.
- Arrastar um modelo para o painel *Mesh Renderer* de uma entidade existente aplica o material dele quando o modelo tem um único material; com vários materiais usa a malha mesclada, sem materiais.
- Esqueleto e animações são importados (ver [animacao.md](animacao.md)): vértices ganham até 4 ossos/pesos, malhas skinadas **não** têm a transformação de nó gravada, e nós animados rígidos usam um osso sintético. O component `Animator` é um bloco próprio (versão 1: modelo, clipe, `PlayOnStart`, `Loop`, `Speed`).
- Ainda **não** importados: câmeras e luzes do arquivo, *morph targets*.

## `.prismprefab`

- Magic: `PPFB`
- Versão atual: **9** (`kPrefabFormatVersion` em `PrefabSerializer.cpp`). Prefabs v7–v8 ainda são lidos.

Mesma estrutura por entidade do `.prismmap`, usando o mesmo `ComponentRegistry` (a v2 acompanha a v14 do mapa — `MeshRenderer::SubmeshIndex` — a v3 a v15 — `AudioSource`/`AudioListener` — a v4 a v16 — `Script::Properties` — a v5 a v17 — `NativeScript` — a v6 a v18 — `ParticleEmitter` — a v7 a v19 — `UIElement` — a v8 a v20 — `Environment`/opções de câmera — e a v9 a v21 — blocos de component), mas sem nome de cena. Gravação atômica, como no mapa. `PrefabSerializer::Instantiate` adiciona as entidades a uma cena existente, sem substituí-la.

## `.prismmat`

- Magic: `PMAT`
- Versão atual: **1** (`kMaterialFormatVersion` em `MaterialSerializer.cpp`)

Guarda os campos de um `MaterialComponent`: caminhos de `Albedo`, `Normal` e `RoughnessMetallic`, mais `AlbedoTint`, `RoughnessFactor` e `MetallicFactor`.

## `.meta`

Um `.meta` acompanha **cada asset** e guarda a sua identidade estável (`AssetID`). O nome é o do asset **mais** `.meta`: `Rock.png` → `Rock.png.meta` (a extensão original é mantida, para `Rock.png` e `Rock.jpg` não disputarem o mesmo arquivo).

```
# Prism asset meta - nao apague; guarda a identidade do asset.
Version=1
ID=8f3a1c5d2b7e9041
Type=Texture
```

- `ID`: exatamente 16 dígitos hexadecimais (64 bits). `0000000000000000` é reservado para "sem asset" e nunca é válido.
- `Type`: deduzido da extensão (`Texture`, `Material`, `Prefab`, `Scene`, `Script`, `Model`, `Audio`). Se divergir da extensão, o `.meta` é corrigido na próxima varredura, mantendo o `ID`.
- Texto e legível de propósito: gera diffs pequenos no Git. **Deve ir para o controle de versão** junto com o asset, senão cada colega gera IDs diferentes para o mesmo arquivo.
- Chaves desconhecidas são ignoradas na leitura (uma versão futura pode acrescentar campos).
- A gravação é atômica (arquivo temporário + rename): um crash nunca deixa um `.meta` pela metade.

### Mover e renomear assets

Mover ou renomear um asset **sempre junto com o seu `.meta`** preserva o ID, e portanto toda referência a ele. Casos que a varredura (`AssetRegistry::Refresh`) trata:

| Situação | Resultado |
|---|---|
| Asset novo, sem `.meta` | ganha um ID novo e um `.meta` |
| Asset + `.meta` movidos juntos | **mesmo ID**, caminho novo |
| Só o asset movido (sem o `.meta`) | ganha ID novo; o `.meta` antigo vira órfão |
| `.meta` sem asset (órfão) | reportado no log; **nunca** apagado automaticamente |
| `.meta` corrompido | aviso e ID novo |
| Dois `.meta` com o mesmo ID (asset copiado com o `.meta`) | o primeiro em ordem alfabética mantém o ID; os demais ganham ID novo |

> Os `.meta` ficam ocultos no painel Conteúdo do Projeto. Mover/renomear arquivos pelo Explorer do sistema exige levar o `.meta` junto; o painel ainda não move nem renomeia arquivos.

## Compatibilidade

- **Mapas e prefabs** (v21 / v9 em diante): component novo, campo novo no fim de um component e component desconhecido **não** quebram arquivos existentes (ver os blocos em `.prismmap`). Os formatos anteriores mais recentes (mapa v19–v20, prefab v7–v8) são lidos e regravados no formato atual ao salvar; formatos mais antigos são recusados com uma mensagem clara. Um arquivo **mais novo** que a engine é recusado.
- **Mudança que quebra** (reordenar/remover campos, renomear o `DisplayName` de um component): exige subir a versão do component ou escrever uma migração.
- **`.prismproj` e `.meta`**: texto, chaves desconhecidas são ignoradas.

## Física: camadas, juntas e personagem (formato de cena 22)

- `Collider` bloco v2: acrescenta `Layer` (0..31, fora do intervalo vira 0).
- `Raycast` bloco v2: acrescenta `LayerMask` (uint32; padrão = todas).
- Novos blocos `Joint` (tipo, entidade conectada pelo **nome/Tag**, âncora, eixo, limites, mola, colidir entre si) e `Character Controller` (raio, altura, degrau, inclinação máxima, massa, escala da gravidade, colar no chão). Cenas antigas continuam abrindo: os campos novos ficam no valor padrão.
- `.prismproj`: `PhysicsLayer<i>=nome` e `PhysicsMask<i>=uint32` (máscara de colisão da camada i; só gravadas quando diferentes do padrão).
