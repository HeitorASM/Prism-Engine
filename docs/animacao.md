# Animação

Animação **esquelética** (skinning) e de **nós rígidos** (portas, hélices, braços mecânicos) de modelos importados `.glb`, `.gltf` e `.fbx`. As duas usam o mesmo componente (`Animator`) e os mesmos clipes.

## Como funciona

- Na importação (`Assets/ModelLoader`), se o arquivo tem **ossos** ou **clipes**, o resultado traz um `ModelRig` (`Animation/ModelRig.h`): hierarquia de nós, ossos (com a matriz de bind inversa) e clipes (chaves de posição, rotação e escala, em segundos).
- Cada vértice carrega **até 4 ossos** e pesos (`MeshVertex::BoneIds/BoneWeights`, atributos 4 e 5). Vértices sem peso (primitivas e modelos estáticos) usam a matriz identidade.
- Malhas **skinadas** ficam no espaço da malha (o osso aplica a transformação do nó). Malhas **rígidas** de um nó animado ganham um osso sintético com peso 1 (pose de bind gravada nos vértices, como antes). Modelos sem ossos nem clipes são importados exatamente como antes.
- Limite: **128 ossos** por modelo (`kMaxBones`). Excedentes são ignorados (aviso no log).
- A paleta de ossos (`u_Bones[128]`, `shaders/skinning.glsl`) é enviada ao shader principal, ao das **sombras** (cascatas, spot e point) e ao do pré-passe do SSAO — sombras e SSAO seguem a pose animada.

## Component `Animator`

Fica na **raiz** do modelo (ou em qualquer ancestral das entidades de malha). Todas as malhas **do mesmo modelo**, na própria entidade ou abaixo dela, são animadas por ele.

| Campo | Significado |
|---|---|
| Modelo | O `.glb/.fbx` de onde vêm esqueleto e clipes. O botão **Usar modelo da malha** copia o da malha da entidade (ou da primeira filha). |
| Clipe | Clipe tocado ao iniciar o Play. |
| Tocar ao iniciar | Liga o clipe acima no Play. |
| Repetir | Loop. Sem loop o clipe para na última pose. |
| Velocidade | 0 = congelado, 1 = normal, 2 = dobro. |

Arrastar um modelo animado para a Hierarquia/Viewport já cria o `Animator` na entidade raiz, com o primeiro clipe. Sem `Animator`, a malha aparece na **pose de bind**.

**Prévia no editor**: o painel tem *Tocar prévia* / *Pausar prévia* / *Parar (pose de bind)*. É só visual — não altera o que é salvo.

Modelos já importados **não** precisam ser reimportados: a malha passa a ser lida com o esqueleto na próxima vez que o modelo carregar. Basta adicionar o `Animator` (menu *Add Component*) e clicar em *Usar modelo da malha*.

## Scripts

Lua:

```lua
function OnCreate()
    entity:PlayAnimation("Idle")            -- troca de clipe (não reinicia se já toca)
end

function OnUpdate(dt)
    if Input.IsKeyDown(Key.W) then
        entity:PlayAnimation("Walk", 0.2)   -- crossfade de 0,2 s
    else
        entity:PlayAnimation("Idle", 0.2)
    end
    if Input.IsKeyPressed(Key.Space) then
        entity:PlayAnimation("Jump", 0.1, false)   -- 3º argumento: loop (omitido = o do Animator)
    end
end
```

| Lua | C++ (`ScriptableEntity`) |
|---|---|
| `entity:PlayAnimation(nome [, fade [, loop]])` → `bool` | `PlayAnimation(nome, fade = 0, loop = -1)` → `bool` |
| `entity:StopAnimation()` (volta à pose de bind) | `StopAnimation()` |
| `entity:PauseAnimation([pausado])` | `PauseAnimation(pausado = true)` |
| `entity:SetAnimationSpeed(v)` | `SetAnimationSpeed(v)` |
| `entity:IsAnimationPlaying()` | `IsAnimationPlaying()` |
| `entity:GetAnimation()` (nome do clipe atual) | — |
| `entity:GetAnimationTime()` / `GetAnimationDuration()` | — |

`PlayAnimation` devolve `false` (e avisa no log) se a entidade não tem `Animator`, o modelo não tem esqueleto ou o clipe não existe. A busca do nome ignora maiúsculas se não houver coincidência exata. Chamar com o clipe que já está tocando **não reinicia** — pode ser chamado todo frame.

API nativa **v5**: módulos C++ do projeto precisam ser **recompilados**.

## Formato de arquivo

Bloco `Animator` (versão 1): `AssetID` do modelo, nome do clipe (string), `PlayOnStart`, `Loop`, `Speed`. O estado de execução (tempo, crossfade, paleta) não é salvo nem copiado (Play, desfazer, duplicar começam do zero).

## Limites atuais

Sem *morph targets* (blend shapes), sem máquina de estados visual, sem editor de keyframes, sem retargeting nem IK, sem root motion (a entidade não se move sozinha com o clipe) e sem eventos de animação.
