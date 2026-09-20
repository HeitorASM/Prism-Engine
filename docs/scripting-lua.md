# Scripting Lua

> **AVISO:** scripting atual e **extremamente basico** a API Pode e sera mudada.
> atualmente limitada a coisas simples, como movimentação e fisica **basica**. 

Lua 5.4 embutido via sol2. Uma única VM global; cada script roda isolado em seu próprio `sol::environment`.

## Como usar

1. Crie um arquivo `.lua` em `Scripts/` do projeto.
2. Adicione um `ScriptComponent` à entidade (Propriedades → + Add Component) e informe o caminho relativo do arquivo.
3. Aperte **Play**.

Scripts só rodam durante o Play. Exemplos prontos em `PrismEditor/assets/ScriptExamples/`.

## Ciclo de vida

Um script pode definir três funções globais, todas opcionais:

```lua
function OnCreate() end            -- ao iniciar o Play
function OnUpdate(deltaTime) end   -- a cada frame
function OnDestroy() end           -- ao parar o Play
```

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
```

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

A tabela `Key` traz `A`–`Z`, `0`–`9`, `Space`, `Enter`, `Escape`, `Tab`, `LeftShift`, `LeftControl` e as setas (`Up`, `Down`, `Left`, `Right`). Qualquer código GLFW não listado pode ser passado como número.

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

- Sem callbacks `OnCollisionEnter`/`OnCollisionExit`.
- Sem hot-reload: edite o `.lua`, pare e rode de novo.
- Scripts enxergam a própria entidade e o que alcançam pela hierarquia (`GetParent`, `GetChild`) e por raycast; não há busca global de entidades.
- Sem debugger nem breakpoints.
