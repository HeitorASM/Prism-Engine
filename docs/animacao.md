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

API nativa **v5** (clipes) e **v7** (máquina de estados): módulos C++ do projeto precisam ser **recompilados**.

## Máquina de estados

No Inspector do `Animator`, a seção **Máquina de estados** monta um grafo (estilo Unity):

- **Parâmetros**: `Float`, `Bool` ou `Trigger` (um trigger fica ligado até uma transição usá-lo).
- **Estados**: cada um toca um clipe (com loop e velocidade próprios). Arraste as caixas para organizar; a borda verde é o estado de entrada.
- **Transições**: setas entre estados. A caixa fixa **Qualquer estado** cria saídas válidas de qualquer estado (bom para "pular" ou "morrer"). Cada transição tem *crossfade*, *tempo de saída* opcional (fração 0–1 do clipe de origem) e **condições** (todas precisam valer). Uma transição sem condições e sem tempo de saída nunca dispara.
- As transições do estado atual são testadas antes das de "Qualquer estado". O trigger usado por uma transição é consumido.
- Com estados no grafo, o campo "Clipe" do Animator não vale mais: *Tocar ao iniciar* entra no estado de entrada. Chamar `PlayAnimation` direto **suspende** o grafo; `SetAnimState` ou o início do Play o retomam.
- A seção **Teste** tem os parâmetros ao vivo: no editor use *Tocar grafo (previa)* e mexa nos valores; em jogo os scripts os controlam.

```lua
function OnUpdate(dt)
    entity:SetAnimFloat("speed", moveSpeed)        -- Float
    entity:SetAnimBool("grounded", isGrounded)     -- Bool
    if Input.IsKeyPressed(Key.Space) then
        entity:SetAnimTrigger("jump")              -- Trigger
    end
end
```

| Lua | C++ (`ScriptableEntity`) |
|---|---|
| `entity:SetAnimFloat(nome, v)` | `SetAnimFloat(nome, v)` |
| `entity:SetAnimBool(nome, b)` | `SetAnimBool(nome, b)` |
| `entity:SetAnimTrigger(nome)` | `SetAnimTrigger(nome)` |
| `entity:GetAnimFloat(nome)` | — |
| `entity:SetAnimState(estado [, fade])` | `SetAnimState(estado, fade = 0)` |
| `entity:GetAnimState()` | `GetAnimState()` |

Todas devolvem `false` (e avisam no log) se não há Animator, grafo ou o nome não existe.

## Eventos de animação

Em **Eventos de animação** (Inspector do `Animator`) escolha o clipe, o tempo em segundos e um nome. Quando o clipe passa por esse tempo (também a cada volta do loop), o script da entidade recebe o evento — útil para passos, golpes que acertam, projéteis:

```lua
function OnAnimationEvent(name)
    if name == "passo" then entity:PlaySound("passo.wav") end
end
```

C++: `void OnAnimationEvent(const char* name) override { ... }`. Eventos só disparam em jogo (não na prévia do editor) e são entregues depois que todos os Animators avançaram no frame.

## Root motion

Marque **Root motion** no `Animator`: o movimento horizontal (XZ) do osso raiz do clipe (o osso de menor profundidade, normalmente `Hips`) passa a mover a **entidade do Animator**, e some da pose (o personagem não escorrega). A altura (Y) continua na pose. Só vale em jogo. Não combine com um Rigidbody dinâmico na mesma entidade — a física sobrescreve a posição; use root motion em personagens sem física ou cinemáticos.

## Formato de arquivo

Bloco `Animator` (versão 2): `AssetID` do modelo, nome do clipe, `PlayOnStart`, `Loop`, `Speed` (v1); `RootMotion`, grafo (parâmetros, estados com posição no editor, transições com condições) e eventos (v2). Cenas antigas abrem normalmente (sem grafo). O estado de execução (tempo, crossfade, paleta, valores dos parâmetros) não é salvo nem copiado.

## Limites atuais

Sem *morph targets* (blend shapes), sem *blend trees* (mistura por parâmetro), sem camadas/máscaras de ossos, sem editor de keyframes, sem retargeting nem IK, e o root motion só usa translação XZ (a rotação do osso raiz continua na pose).
