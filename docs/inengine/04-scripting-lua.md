# Scripting em Lua

> A API ainda está em evolução e pode mudar.
>
> **Sobre a linguagem principal:** no início do desenvolvimento a Prism só suportava Lua. Hoje ela também suporta C++, e o trabalho atual em scripting está levando o **C++ a ser a linguagem principal** da engine, com o **Lua como uma opção**. Lua continua ótimo para prototipar e testar ideias rápido. Veja [Scripting em C++](05-scripting-cpp.md).

Lua 5.4. É a forma mais rápida de dar comportamento ao jogo: você edita o arquivo, aperta Play e vê o resultado, sem compilar. **Todas as funções disponíveis** (Lua e C++) estão na [API de scripting](06-api-de-scripting.md); esta página mostra como usar a linguagem.

## Como usar

1. Crie um arquivo `.lua` em `Scripts/` (ou pelo botão **Novo...** do component Script).
2. Adicione o component **Script** à entidade e escolha o arquivo.
3. Aperte **Play**. Scripts só rodam durante o Play.

Cada script é independente: tem suas próprias variáveis, e a variável `entity` aponta para a entidade dona dele.

## Funções do script

Todas são opcionais (a lista completa está na [API](06-api-de-scripting.md#ciclo-de-vida)):

```lua
function OnCreate() end                 -- uma vez, ao iniciar o Play
function OnUpdate(dt) end               -- a cada frame (dt = segundos desde o frame anterior)
function OnDestroy() end                -- ao parar o Play ou destruir a entidade
function OnCollisionEnter(other) end    -- começou a tocar 'other'
function OnTriggerEnter(other) end      -- 'other' entrou num collider Trigger
function OnClick() end                  -- botão de UI (sem onClick) desta entidade foi clicado
```

Erros nunca derrubam o editor: aparecem no **Console** e o script com erro para de ser chamado até você corrigir e reiniciar o Play.

## Exemplo: andar com WASD

```lua
Properties = { velocidade = 5.0 }

function OnUpdate(dt)
    local v = Properties.velocidade * dt
    if Input.IsKeyDown(Key.W) then entity:Translate(0, 0, -v) end
    if Input.IsKeyDown(Key.S) then entity:Translate(0, 0,  v) end
    if Input.IsKeyDown(Key.A) then entity:Translate(-v, 0, 0) end
    if Input.IsKeyDown(Key.D) then entity:Translate( v, 0, 0) end
end
```

## Variáveis públicas (`Properties`)

Uma tabela global `Properties` aparece em **Propriedades**, editável por entidade (com desfazer e botão Reset), sem mexer no arquivo. Só `number`, `boolean` e `string`. O valor editado já vale no `OnCreate`. (Isto só existe em Lua por enquanto.)

## Exemplo: coletar moedas

No script da moeda (com Collider **Trigger**):

```lua
function OnTriggerEnter(other)
    if other:GetName() == "Jogador" then
        Audio.Play("Assets/Audio/moeda.wav")
        entity:Destroy()
    end
end
```

## Exemplo: placar na tela

```lua
local pontos = 0

function OnTriggerEnter(other)
    pontos = pontos + 1
    Scene.Find("Placar"):GetUIDocument():Find("moedas").Text = "Moedas: " .. pontos
end
```

## Exemplo: mirar com raycast

```lua
function OnUpdate(dt)
    if Input.IsMouseButtonDown(Mouse.Left) and not UI.IsPointerOverUI() then
        local hit = Physics.Raycast(entity:GetWorldPosition(), entity:GetWorldForward(), 50, entity)
        if hit.Hit then
            log("acertou " .. hit:GetEntity():GetName())
        end
    end
end
```

Há mais exemplos prontos em `PrismEditor/assets/ScriptExamples/` da engine.

## Bibliotecas e limites

Só `base`, `math`, `string` e `table` estão abertas: scripts não leem arquivos nem executam comandos do sistema, de propósito. Ainda **não** existem: recarga de script durante o Play, `Scene.Load` (trocar de cena), gamepad e depurador.
