# Formatos de arquivo

| Extensão | Conteúdo | Formato | Pasta |
|---|---|---|---|
| `.prismproj` | configuração do projeto | texto (`Chave=Valor`) | raiz do projeto |
| `.prismmap` | cena completa | binário | `Maps/` |
| `.prismprefab` | entidade e subárvore | binário | `Assets/Prefabs/` |
| `.prismmat` | um material | binário | `Assets/Materials/` |

Os arquivos binários usam little-endian e começam com um cabeçalho `magic` (4 bytes) + `versão` (uint32).

## `.prismproj`

Texto simples, legível e adequado ao controle de versão. Linhas iniciadas por `#` são comentários.

```
Name=MeuProjeto
AssetDirectory=Assets
ScriptDirectory=Scripts
MapDirectory=Maps
StartMap=Cena.prismmap
Exposure=1
Ambient=0.1
EnvZenith=0.36,0.55,0.95
EnvHorizon=0.8,0.85,0.9
EnvGround=0.22,0.2,0.18
```

`StartMap` é o mapa aberto automaticamente ao carregar o projeto.

`Exposure`, `Ambient`, `EnvZenith`, `EnvHorizon` e `EnvGround` são os ajustes de renderização do projeto (menu **Renderização** do editor; ver [Renderização](renderizacao.md)). As cores são `r,g,b` em sRGB, de 0 a 1.

- Sempre escritos com **ponto decimal**, independente do idioma do sistema (um Windows em português não muda o formato do arquivo).
- Chaves ausentes usam o valor padrão, então projetos criados antes destas chaves existirem abrem com o visual de sempre.
- Valor inválido (texto, número de componentes errado, `nan`) é ignorado com um aviso no log e a chave volta ao padrão, sem afetar as outras.
- Valores fora da faixa são limitados: `Exposure` a 0.05–16 (zero deixaria a cena preta), `Ambient` a 0–2 e cada canal de cor a 0–1.

## `.prismmap`

- Magic: `PRSM`
- Versão atual: **10** (`kSceneFormatVersion` em `SceneSerializer.cpp`)

> Sera resetada futuramente, atualmente so e upada para motivos de desenvolvimento

Contém o nome da cena e, para cada entidade: tag, transform, os components opcionais (na ordem em que foram registrados no `ComponentRegistry`) e o índice do pai (`-1` = sem pai). `RelationshipComponent::Children` não é gravado; é reconstruído a partir dos índices de pai depois que todas as entidades são criadas.

O layout exato campo a campo é definido pelos blocos `Serialize`/`Deserialize` de cada component em `ComponentRegistration.cpp`, e o histórico de versões está nos comentários de `kSceneFormatVersion`.

Do `RaycastComponent` só `TargetPosition`, `Enabled` e `IgnoreParentAndSiblings` são gravados; os campos de resultado (`Hit`, `HitPoint` etc.) são de runtime.

## `.prismprefab`

- Magic: `PPFB`
- Versão atual: **1** (`kPrefabFormatVersion` em `PrefabSerializer.cpp`)

Mesma estrutura por entidade do `.prismmap`, usando o mesmo `ComponentRegistry`, mas sem nome de cena. `PrefabSerializer::Instantiate` adiciona as entidades a uma cena existente, sem substituí-la.

## `.prismmat`

- Magic: `PMAT`
- Versão atual: **1** (`kMaterialFormatVersion` em `MaterialSerializer.cpp`)

Guarda os campos de um `MaterialComponent`: caminhos de `Albedo`, `Normal` e `RoughnessMetallic`, mais `AlbedoTint`, `RoughnessFactor` e `MetallicFactor`.

## Compatibilidade

**Atualmente** Não há migração automática. Um arquivo de versão diferente da atual é recusado, com uma mensagem indicando a versão encontrada e a esperada. Sempre que o formato de `.prismmap` muda (novo component ou novo campo), `kSceneFormatVersion` sobe e os mapas antigos precisam ser recriados ou resalvos por uma versão compatível do editor.
