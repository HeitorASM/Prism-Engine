# Scripting Lua

> **AVISO:** a API ainda está em evolução e pode mudar.

Lua 5.4 embutido via sol2. Uma única VM global; cada script roda isolado em seu próprio `sol::environment`.

## Como usar

1. Crie um arquivo `.lua` em `Scripts/` do projeto.
2. Adicione um `ScriptComponent` à entidade (Propriedades → + Add Component) e informe o caminho relativo do arquivo.
3. Aperte **Play**.

Scripts só rodam durante o Play. Exemplos prontos em `PrismEditor/assets/ScriptExamples/`.

## Ciclo de vida

Um script pode definir estas funções globais, todas opcionais:

```lua
function OnCreate() end                  -- ao iniciar o Play
function OnUpdate(deltaTime) end         -- a cada frame
function OnDestroy() end                 -- ao parar o Play ou destruir a entidade
function OnCollisionEnter(other) end     -- começou a tocar 'other' (Entity)
function OnCollisionExit(other) end      -- parou de tocar
function OnTriggerEnter(other) end       -- 'other' entrou num collider marcado como Trigger
function OnTriggerExit(other) end
```

Os eventos de contato chegam na thread principal, depois da simulação do frame. Cada corpo envolvido recebe o evento com o outro como argumento.

## Variáveis públicas (`Properties`)

Declare uma tabela global `Properties` e os valores aparecem no painel Propriedades, editáveis por entidade (com Undo/Redo e botão Reset):

```lua
Properties = { speed = 5.0, jumpForce = 8, canDoubleJump = true, label = "Jogador" }

function OnUpdate(dt)
    local p = entity:GetTransform()
    p.Translation.x = p.Translation.x + Properties.speed * dt
end
```

Só `number`, `boolean` e `string` são suportados. O valor editado é aplicado antes do `OnCreate`. Só é guardado o que você muda; os padrões continuam no arquivo.

Erros de sintaxe ou de runtime **nunca derrubam o editor**. Eles vão para o Console, e o script com erro deixa de ser chamado a cada frame até ser corrigido e o Play reiniciado.

## Variável `entity`

Cada script tem uma variável `entity`: a entidade dona do `ScriptComponent`.

### Transform

| Função | Descrição |
|---|---|
| `entity:GetTransform()` | `Transform` **local** editável (`Translation`, `Rotation` em graus, `Scale`, cada um com `.x/.y/.z`) |
| `entity:SetPosition(x, y, z)` | define a posição local |
| `entity:Translate(dx, dy, dz)` | soma ao deslocamento local |
| `entity:GetForward()` / `GetRight()` | direção **local** (ignora rotação de ancestrais) |
| `entity:GetWorldPosition()` | posição no mundo |
| `entity:GetWorldForward()` / `GetWorldRight()` | direção no mundo (inclui ancestrais) |

`GetTransform()` retorna o transform local, sem considerar ancestrais. Para posição de mundo, use `GetWorldPosition()`.

### Identificação e hierarquia

| Função | Descrição |
|---|---|
| `entity:GetName()` | nome (tag) |
| `entity:GetParent()` | entidade pai |
| `entity:GetChildCount()` | número de filhos |
| `entity:GetChildAt(i)` | filho pelo índice |
| `entity:GetChild(nome)` | filho pelo nome (busca linear) |
| `entity:IsValid()` | `false` para entidade inexistente |

Entidades podem ser comparadas com `==`.

### Física

| Função | Descrição |
|---|---|
| `entity:ApplyForce(x, y, z)` | aplica força |
| `entity:ApplyImpulse(x, y, z)` | aplica impulso |
| `entity:GetVelocity()` | retorna `Vec3` |
| `entity:SetVelocity(x, y, z)` | define a velocidade |

Não fazem nada se a entidade não tiver corpo físico ativo (`Collider` + `RigidBody`).

## Globais

### Log

```lua
log("mensagem")
log_warn("aviso")
log_error("erro")
```

Aparecem no Console com prefixo `[Lua]`.

### Vec3

```lua
local v = Vec3.new(1, 2, 3)   -- campos x, y, z
local d = (b - a):Normalized() * 5   -- +, -, * e / por número, -v, ==
-- v:Length(), v:Normalized(), v:Dot(o), v:Cross(o), v:Distance(o), tostring(v)
```

### Time e Mathf

`Time.DeltaTime()`, `Time.Elapsed()` (segundos desde o Play) e `Time.FrameCount()`. `Mathf.Clamp(v, min, max)`, `Mathf.Lerp(a, b, t)`, `Mathf.Sign(v)`.

### Scene

Operam na cena do script que chamou.

| Função | Descrição |
|---|---|
| `Scene.Find("Nome")` | primeira entidade com esse nome (confira `:IsValid()`) |
| `Scene.FindAll("Nome")` | tabela com todas |
| `Scene.Create("Nome")` | entidade vazia |
| `Scene.CreateMesh("Nome", "Cube", x, y, z)` | entidade visível (`Cube`, `Sphere`, `Capsule`, `Cylinder`, `Plane`) |
| `Scene.Instantiate("Assets/Prefabs/X.prismprefab", pos)` | instancia um prefab (caminho relativo ao projeto; `pos` opcional) |
| `Scene.Destroy(e)` / `e:Destroy()` | destrói no **fim do frame** |

### Mais métodos de `entity`

`SetName`, `SetRotation(x, y, z)` (graus), `SetScale`, `SetParent(pai)`, `Destroy()`, `GetScript()` (as variáveis e funções do script daquela entidade, para scripts conversarem) e getters de components que devolvem `nil` se ausentes: `GetMeshRenderer()` (`Color`), `GetMaterial()` (`AlbedoTint`, `RoughnessFactor`, `MetallicFactor`, caminhos de textura), `GetLight()` (`Color`, `Intensity`, `Range`, `SpotAngle`…), `GetCamera()` (`FOV`, `Primary`…), `GetAudioSource()` (`Volume`, `Pitch`, `Loop`…), `GetParticleEmitter()` (`Enabled`, `EmissionRate`, `SpreadAngle`, `SetColorStart(r,g,b,a)`…). Partículas: `entity:PlayParticles()` (reinicia e solta o burst), `StopParticles()`, `ClearParticles()`, `IsParticlesPlaying()`, `GetParticleCount()`.

`print(...)` agora vai para o Console, como `log`.

### Input

Por polling. Só retorna valores úteis durante o Play.

| Função | Descrição |
|---|---|
| `Input.IsKeyDown(Key.W)` | tecla pressionada agora |
| `Input.IsKeyPressed(Key.Space)` | tecla apertada neste frame |
| `Input.IsMouseButtonDown(botão)` | botão do mouse pressionado |
| `Input.GetMouseX()` / `GetMouseY()` | posição do cursor |
| `Input.GetMouseDeltaX()` / `GetMouseDeltaY()` | movimento do mouse (use em câmeras FPS/TPS) |
| `Input.SetCursorMode(modo)` / `GetCursorMode()` | controla o cursor |

`CursorMode` tem `Normal`, `Hidden` e `Locked`.

A tabela `Key` traz `A`–`Z`, `0`–`9`, `F1`–`F12`, `Space`, `Enter`, `Escape`, `Tab`, `Backspace`, Shift/Control/Alt (esquerdos e direitos) e as setas (`Up`, `Down`, `Left`, `Right`). A tabela `Mouse` traz `Left`, `Right`, `Middle`. Qualquer código GLFW não listado pode ser passado como número.

### Raycast

```lua
local hit = Physics.Raycast(origem, direcao, maxDist, ignorar)
if hit.Hit then
    log(hit:GetEntity():GetName())
    -- hit.Point, hit.Normal, hit.Distance
end
```

`maxDist` (padrão 1000) e `ignorar` (uma entidade a excluir do teste) são opcionais. O raio considera apenas a cena do script que o chamou.

## Bibliotecas do Lua

Só `base`, `math`, `string` e `table` estão abertas. `io`, `os`, `package` e `debug` ficam de fora de propósito, para que um script de gameplay não leia arquivos nem execute comandos do sistema.

## Limitações

- Sem hot-reload: edite o `.lua`, pare e rode de novo.
- `Scene.Load` (trocar de cena) ainda não existe.
- Entidades criadas durante um frame só rodam `OnUpdate` a partir do próximo.
- Sem debugger nem breakpoints.

## Áudio

`entity:PlaySound()`, `entity:StopSound()`, `entity:IsSoundPlaying()`, `entity:SetSoundVolume(v)`, `entity:SetSoundPitch(p)` (precisam de um Audio Source) e a tabela global `Audio` (`Play`, `Play3D`, `SetMasterVolume`, `GetMasterVolume`). Detalhes em [audio.md](audio.md).
