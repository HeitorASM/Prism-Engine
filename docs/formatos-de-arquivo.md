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

`EngineVersion` é a versão da engine (`Version.py`) que criou o projeto. Só serve para o editor avisar quando difere da atual; projetos sem a chave abrem normalmente. O arquivo é gravado de forma atômica (arquivo temporário + rename), então um crash no meio do save não o corrompe. Um projeto novo também recebe um `.gitignore` com `Cache/` e `Binaries/`.

> As opções de render **não ficam mais no projeto**: o ambiente e o skybox são do `EnvironmentComponent` da cena (gravado no `.prismmap`) e exposição, fundo e SSAO são do `CameraComponent`. Chaves antigas (`Exposure`, `Ambient`, `Env*`, `Sky*`) em um `.prismproj` existente são ignoradas ao ler e desaparecem no próximo save.

## `.prismmap`

- Magic: `PRSM`
- Versão atual: **19** (`kSceneFormatVersion` em `SceneSerializer.cpp`)

> Sera resetada futuramente, atualmente so e upada para motivos de desenvolvimento

Contém o nome da cena e, para cada entidade: tag, transform, os components opcionais (na ordem em que foram registrados no `ComponentRegistry`) e o índice do pai (`-1` = sem pai). `RelationshipComponent::Children` não é gravado; é reconstruído a partir dos índices de pai depois que todas as entidades são criadas.

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

> O número da versão **vive no código**. Ao subir `kSceneFormatVersion`, atualize "Versão atual" acima e esta tabela.

Do `RaycastComponent` só `TargetPosition`, `Enabled` e `IgnoreParentAndSiblings` são gravados; os campos de resultado (`Hit`, `HitPoint` etc.) são de runtime.

## Modelos 3D (`.obj`, `.fbx`, `.gltf`, `.glb`)

Importados via Assimp (`Assets/ModelLoader`). Os triângulos são agrupados **por material** (um submesh por material do arquivo, com a hierarquia de nós já aplicada nos vértices). Arrastar o modelo para a **Hierarquia** ou a **Viewport** cria uma entidade raiz com o nome do arquivo e, se houver mais de um material, uma entidade filha por material; cada uma recebe `MeshRenderer` (com `SubmeshIndex`) e `Material` já preenchido (albedo, normal, roughness/metallic, cor base e fatores).

- Texturas externas são procuradas relativas à pasta do modelo (ou só pelo nome do arquivo ali).
- Texturas embutidas (ex.: dentro de um `.glb`) são extraídas para `<pasta do modelo>/<nome>_textures/`.
- A textura roughness/metallic só é importada de glTF (canal G = roughness, canal B = metallic).
- UVs seguem a convenção OpenGL (V para cima), igual às texturas carregadas pela engine; **não** use `aiProcess_FlipUVs`.
- Arrastar um modelo para o painel *Mesh Renderer* de uma entidade existente aplica o material dele quando o modelo tem um único material; com vários materiais usa a malha mesclada, sem materiais.
- Ainda **não** importados: esqueleto/animação, câmeras e luzes do arquivo.

## `.prismprefab`

- Magic: `PPFB`
- Versão atual: **6** (`kPrefabFormatVersion` em `PrefabSerializer.cpp`)

Mesma estrutura por entidade do `.prismmap`, usando o mesmo `ComponentRegistry` (a v2 acompanha a v14 do mapa — `MeshRenderer::SubmeshIndex` — a v3 a v15 — `AudioSource`/`AudioListener` — a v4 a v16 — `Script::Properties` — a v5 a v17 — `NativeScript` — a v6 a v18 — `ParticleEmitter` — e a v7 a v19 — `UIElement`), mas sem nome de cena. `PrefabSerializer::Instantiate` adiciona as entidades a uma cena existente, sem substituí-la.

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

**Atualmente** Não há migração automática. Um arquivo de versão diferente da atual é recusado, com uma mensagem indicando a versão encontrada e a esperada. Sempre que o formato de `.prismmap` muda (novo component ou novo campo), `kSceneFormatVersion` sobe e os mapas antigos precisam ser recriados ou resalvos por uma versão compatível do editor.
