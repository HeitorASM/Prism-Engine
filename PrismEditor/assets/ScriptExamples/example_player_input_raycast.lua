-- Controlador de personagem em primeira pessoa (WASD + mouse look + pulo +
-- interacao por raycast). Ponto de partida comum para prototipos de FPS.
--
-- CONFIGURACAO NECESSARIA NA CENA (importante!):
--   1. Este script vai na entidade do CORPO do personagem (a que tem
--      RigidBodyComponent + ColliderComponent - uma capsula em pe e o
--      formato mais comum para personagens).
--   2. Crie uma entidade FILHA do corpo, chamada exatamente "Camera"
--      (Hierarchy panel: arraste a camera para dentro do corpo, ou crie
--      e renomeie), com um CameraComponent, posicionada na altura dos
--      "olhos" do personagem (ex: Y = 1.6 se o corpo tem ~1.8 de altura).
--
-- POR QUE CORPO E CAMERA SAO ENTIDADES SEPARADAS: olhar para cima/baixo
-- (pitch) e olhar para os lados (yaw) sao dois eixos de rotacao
-- DIFERENTES e nao devem ficar na mesma entidade. Se a mesma entidade
-- girasse nos dois eixos, o corpo "deitaria" ao olhar para cima/baixo, e
-- o movimento (que usa Forward/Right do CORPO) ficaria torto/inclinado
-- junto com a camera - exatamente o bug que uma versao anterior deste
-- script tinha. A solucao padrao (usada por praticamente toda engine):
--   - O CORPO gira so em YAW (Y) - o movimento (WASD) sempre fica correto
--     no plano horizontal, nao importa para onde a camera esteja olhando.
--   - A CAMERA (filha) gira so em PITCH (X) - olhar para cima/baixo afeta
--     so a visao, nunca o movimento.
-- Ver Entity:GetChild()/GetParent() (Entity.h/ScriptEngine.cpp) para a
-- API de hierarquia que torna isso possivel a partir de um script.

local moveSpeed = 4.0
local jumpImpulse = 5.0
local rayDistance = 3.0
local mouseSensitivity = 0.15

-- Limite de pitch (olhar para cima/baixo) em graus - evita a camera dar a
-- volta completa e ficar de cabeca para baixo (89, nao 90, para nunca
-- ficar EXATAMENTE alinhada com o eixo Y, o que pode causar instabilidade
-- numerica em alguns calculos de camera/view matrix).
local maxPitch = 89.0

-- Cacheado em OnCreate() (nao buscado a cada frame em OnUpdate) - GetChild
-- faz uma busca linear pelos filhos a cada chamada (ver Entity.h), e o
-- resultado nao muda depois que a cena comeca a rodar.
local camera = nil

function OnCreate()
    camera = entity:GetChild("Camera")

    if not camera:IsValid() then
        log("example_player_input_raycast: AVISO - nenhuma entidade filha chamada 'Camera' foi encontrada em '" .. entity:GetName() .. "'. Crie uma entidade filha com esse nome e um CameraComponent (ver comentario no topo deste arquivo). O script vai continuar rodando, mas sem controle de pitch (olhar para cima/baixo).")
    end

    Input.SetCursorMode(CursorMode.Locked)
end

function OnUpdate(deltaTime)
    local mouseDeltaX = Input.GetMouseDeltaX()
    local mouseDeltaY = Input.GetMouseDeltaY()

    -- Yaw: gira o CORPO (esta entidade) em Y - afeta tanto a visao quanto
    -- a direcao de movimento (Forward/Right abaixo), que e o comportamento
    -- correto para "virar o personagem inteiro para o lado".
    local transform = entity:GetTransform()
    transform.Rotation.y = transform.Rotation.y - mouseDeltaX * mouseSensitivity

    -- Pitch: gira SO A CAMERA (filha) em X - nunca o corpo. Isto e o que
    -- resolve o bug de movimento torto ao olhar para cima/baixo (ver
    -- comentario grande no topo do arquivo).
    if camera:IsValid() then
        local camTransform = camera:GetTransform()
        local newPitch = camTransform.Rotation.x - mouseDeltaY * mouseSensitivity
        if newPitch > maxPitch then newPitch = maxPitch end
        if newPitch < -maxPitch then newPitch = -maxPitch end
        camTransform.Rotation.x = newPitch
    end

    local moveX, moveZ = 0.0, 0.0
    if Input.IsKeyDown(Key.W) then moveZ = moveZ - 1.0 end
    if Input.IsKeyDown(Key.S) then moveZ = moveZ + 1.0 end
    if Input.IsKeyDown(Key.A) then moveX = moveX - 1.0 end
    if Input.IsKeyDown(Key.D) then moveX = moveX + 1.0 end

    -- Forward/Right do CORPO (nunca da camera) - como o corpo so gira em
    -- Y (yaw), estes vetores ficam sempre no plano horizontal, nao
    -- importa para onde a camera esteja olhando.
    local forward = entity:GetForward()
    local right = entity:GetRight()
    local worldMoveX = right.x * moveX + forward.x * (-moveZ)
    local worldMoveZ = right.z * moveX + forward.z * (-moveZ)

    local vel = entity:GetVelocity()
    entity:SetVelocity(worldMoveX * moveSpeed, vel.y, worldMoveZ * moveSpeed)

    if Input.IsKeyPressed(Key.Space) then
        entity:ApplyImpulse(0, jumpImpulse, 0)
    end

    -- Raycast de interacao: sai da CAMERA (nao do corpo), na direcao que
    -- a camera esta olhando (incluindo pitch) - e o que o jogador espera
    -- ao mirar em algo com o "olhar" (crosshair no centro da tela), nao
    -- so na direcao horizontal do corpo. Se nao houver camera valida, usa
    -- o corpo como alternativa (comportamento antigo).
    --
    -- GetWorldPosition/GetWorldForward (nao GetTransform().Translation
    -- nem GetForward comuns) sao ESSENCIAIS aqui: a camera e uma entidade
    -- FILHA do corpo (ver comentario no topo do arquivo), entao a versao
    -- local dessas funcoes ignoraria a posicao/rotacao (yaw) do corpo -
    -- o raio sairia do lugar errado e na direcao errada. Ver comentario
    -- em Entity:GetWorldForward (ScriptEngine.cpp) para detalhes.
    local rayOrigin = entity:GetWorldPosition()
    local rayDirection = entity:GetForward()
    if camera:IsValid() then
        rayOrigin = camera:GetWorldPosition()
        rayDirection = camera:GetWorldForward()
    end

    local hit = Physics.Raycast(rayOrigin, rayDirection, rayDistance)

    if hit.Hit and Input.IsKeyPressed(Key.E) then
        local other = hit:GetEntity()
        log("Interagiu com '" .. other:GetName() .. "' a " .. hit.Distance .. " unidades de distancia")
    end

    if Input.IsKeyPressed(Key.Escape) then
        Input.SetCursorMode(CursorMode.Normal)
    end
end

function OnDestroy()
    Input.SetCursorMode(CursorMode.Normal)
end
