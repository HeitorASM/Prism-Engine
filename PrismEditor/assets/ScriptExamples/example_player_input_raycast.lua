-- example_player_input_raycast.lua
-- Script de exemplo mostrando a API de Input (teclado, mouse com delta,
-- captura/liberacao de cursor) e Physics.Raycast (ver
-- Prism/src/Prism/Core/Input.h e Prism/src/Prism/Physics/PhysicsEngine.h/
-- ScriptEngine::RegisterAPI para a implementacao). Requer que a entidade
-- tenha RigidBodyComponent + ColliderComponent (Dynamic) para o movimento
-- via ApplyImpulse/SetVelocity fazer sentido - ver guia do prototipo.
--
-- IMPORTANTE: marque RigidBodyComponent::FixedRotation = true (Properties
-- panel) nesta entidade. Este script controla a rotacao (yaw/pitch) via
-- transform.Rotation, NAO via fisica - sem FixedRotation, qualquer contato
-- fisico (esbarrar numa parede, cair de uma pequena altura) aplica torque
-- ao corpo e Simulate() sincroniza essa rotacao "acidental" de volta,
-- brigando visualmente com o que este script esta tentando manter (camera
-- girando sozinha/descontrolada ao esbarrar em algo). Com FixedRotation
-- ligado, o corpo continua caindo/colidindo/sendo empurrado normalmente
-- (so a ROTACAO fisica fica travada) - a rotacao inteira fica 100% por
-- conta deste script, sem nenhuma sincronizacao/conflito com a fisica.
--
-- Para usar: mesmo fluxo do example_spin.lua (ver aquele arquivo para o
-- passo a passo completo). So funciona de verdade durante o modo Play -
-- Input.* fora do Play sempre retorna false/0 (ver Input::SetContext,
-- Core/Input.h).

local moveSpeed = 4.0      -- unidades/segundo
local jumpImpulse = 5.0
local rayDistance = 3.0    -- alcance do raycast "para frente"
local mouseSensitivity = 0.15 -- graus por pixel de movimento do mouse

function OnCreate()
    log("example_player_input_raycast: OnCreate em '" .. entity:GetName() .. "'")

    -- Trava e esconde o cursor do SO (ver CursorMode.Locked, Core/Input.h)
    -- - padrao de camera FPS/TPS: sem isso, GetMouseDeltaX/Y ainda
    -- funcionaria, mas o cursor real esbarraria na borda da janela e
    -- pararia de gerar delta assim que chegasse la, quebrando uma rotacao
    -- continua de camera. Chamado uma vez aqui (nao a cada OnUpdate).
    Input.SetCursorMode(CursorMode.Locked)
end

function OnUpdate(deltaTime)
    -- --- Olhar ao redor (mouse), rotaciona a entidade (yaw) e a camera
    -- (pitch, se for o caso) ---
    -- GetMouseDeltaX/Y (nao GetMouseX/Y - posicao ABSOLUTA, inutil com o
    -- cursor travado, ver OnCreate acima) - "quanto o mouse se moveu
    -- desde o frame anterior", multiplicado por deltaTime (nao apenas por
    -- mouseSensitivity - o delta de mouse ja e "por frame", entao NAO
    -- deve ser multiplicado de novo por deltaTime; isso e proposital,
    -- diferente do movimento por teclado abaixo que sim usa deltaTime).
    local mouseDeltaX = Input.GetMouseDeltaX()
    local mouseDeltaY = Input.GetMouseDeltaY()
    local transform = entity:GetTransform()

    -- Yaw (eixo Y, olhar esquerda/direita) - sem limite, gira livremente.
    transform.Rotation.y = transform.Rotation.y - mouseDeltaX * mouseSensitivity

    -- Pitch (eixo X, olhar cima/baixo) - COM limite (-89 a 89 graus): sem
    -- clamp, o personagem conseguiria "olhar para tras de cabeca para
    -- baixo" passando de 90 graus, o que inverte a sensacao de olhar
    -- para cima/baixo (o classico bug de camera FPS sem clamp de pitch).
    -- mouseDeltaY POSITIVO = mouse se moveu para BAIXO na tela (mesma
    -- convencao Y-para-baixo do GLFW, ver GetMouseY/Input.h) - por isso
    -- SUBTRAI aqui (mouse para baixo deve olhar para baixo, rotacao X
    -- negativa nesta convencao de Euler da engine).
    local newPitch = transform.Rotation.x - mouseDeltaY * mouseSensitivity
    if newPitch > 89.0 then newPitch = 89.0 end
    if newPitch < -89.0 then newPitch = -89.0 end
    transform.Rotation.x = newPitch

    -- --- Movimento por teclado (WASD), estilo "segurar para andar" ---
    -- IsKeyDown = estado continuo (true enquanto a tecla estiver
    -- fisicamente apertada) - certo para movimento, errado para acoes de
    -- "um clique so" como pular (ver IsKeyPressed abaixo).
    --
    -- moveX/moveZ aqui sao em ESPACO LOCAL da camera (W sempre "para onde
    -- estou olhando", independente do yaw atual) - precisam ser
    -- convertidos para espaco de MUNDO usando entity:GetForward()/
    -- GetRight() (ver ScriptEngine.cpp) antes de virar velocidade - sem
    -- essa conversao, W sempre empurraria na direcao fixa -Z do MUNDO, e o
    -- personagem so andaria "para a direcao certa" quando o yaw fosse 0 (o
    -- classico bug de "andar para tras/de lado depois de virar a camera").
    local moveX, moveZ = 0.0, 0.0
    if Input.IsKeyDown(Key.W) then moveZ = moveZ - 1.0 end
    if Input.IsKeyDown(Key.S) then moveZ = moveZ + 1.0 end
    if Input.IsKeyDown(Key.A) then moveX = moveX - 1.0 end
    if Input.IsKeyDown(Key.D) then moveX = moveX + 1.0 end

    -- GetForward()/GetRight() (ver ScriptEngine.cpp) usam a MESMA
    -- convencao de "frente"/"direita" que o resto da engine ja usa
    -- (Renderer, luzes Spot/Directional) - evita reimplementar a
    -- trigonometria de yaw manualmente aqui no script, o que seria facil
    -- de errar o sinal (frente/direita invertidos) sem uma fonte unica de
    -- verdade. -moveZ porque forward JA aponta para onde a camera olha
    -- (W = "andar para frente" = seguir forward), enquanto moveZ negativo
    -- (definido acima) representa "W apertado" - multiplicar por -moveZ
    -- inverte esse sinal de volta para "quantidade de forward a somar".
    local forward = entity:GetForward()
    local right = entity:GetRight()
    local worldMoveX = right.x * moveX + forward.x * (-moveZ)
    local worldMoveZ = right.z * moveX + forward.z * (-moveZ)

    -- SEMPRE chama SetVelocity (nao so "if moveX ~= 0 or moveZ ~= 0") -
    -- Box3D nao aplica nenhum atrito/desaceleracao automatica quando
    -- ninguem mexe na velocidade de um corpo Dynamic (ver
    -- PhysicsEngine::SetLinearVelocity) - se este script so chamasse
    -- SetVelocity enquanto alguma tecla estivesse pressionada, soltar
    -- todas as teclas deixaria a ULTIMA velocidade XZ definida gravada no
    -- corpo para sempre (o personagem continuaria deslizando
    -- indefinidamente com a velocidade do ultimo frame em que uma tecla
    -- estava apertada) - exatamente o bug de "andar sozinho depois de
    -- soltar" se corrigido aqui. Com moveX/moveZ=0 quando nenhuma tecla
    -- esta apertada, este SetVelocity(0, vel.y, 0) e o que efetivamente
    -- PARA o personagem no eixo XZ a cada frame sem tecla segurada.
    -- SetVelocity so nos eixos X/Z, preservando a velocidade Y atual
    -- (gravidade/pulo) - sem isso, andar (ou parar) cancelaria a
    -- queda/pulo em andamento a cada frame.
    local vel = entity:GetVelocity()
    entity:SetVelocity(worldMoveX * moveSpeed, vel.y, worldMoveZ * moveSpeed)



    -- --- Pulo (Espaco), acao de UM clique so ---
    -- IsKeyPressed = so true no frame exato da transicao solta->apertada
    -- (ver comentario grande em Input::IsKeyPressed, Core/Input.h) - sem
    -- isso (usando IsKeyDown aqui por engano), o personagem "pularia"
    -- continuamente a cada frame enquanto Espaco ficasse segurado.
    if Input.IsKeyPressed(Key.Space) then
        entity:ApplyImpulse(0, jumpImpulse, 0)
    end

    -- --- Raycast "para frente", exemplo de interacao/mira ---
    -- Usa entity:GetForward() (ver ScriptEngine.cpp) - a mesma direcao
    -- "para onde a camera esta olhando" que o movimento WASD acima ja usa,
    -- entao apontar/mirar sempre bate com o que esta na tela, em qualquer
    -- yaw/pitch (nao so quando o personagem esta olhando exatamente para
    -- -Z do mundo).
    local origin = entity:GetTransform().Translation
    local hit = Physics.Raycast(origin, entity:GetForward(), rayDistance)

    if hit.Hit and Input.IsKeyPressed(Key.E) then
        -- hit:GetEntity() reconstroi um Entity de verdade (utilizavel com
        -- :GetName(), :ApplyForce(), etc) a partir do resultado do
        -- raycast - so chame isto quando hit.Hit for true.
        local other = hit:GetEntity()
        log("Interagiu com '" .. other:GetName() .. "' a " .. hit.Distance .. " unidades de distancia")
    end

    -- --- Soltar o cursor (Esc), exemplo de "abrir menu de pausa" ---
    -- Sem isto, o cursor ficaria travado (CursorMode.Locked, ver
    -- OnCreate) pelo resto da sessao de Play inteira - um jogo de
    -- verdade tipicamente libera o cursor ao pausar/abrir um menu, e
    -- trava de novo ao voltar (nao implementado aqui, so a liberacao,
    -- para manter o exemplo focado na API de Input).
    if Input.IsKeyPressed(Key.Escape) then
        Input.SetCursorMode(CursorMode.Normal)
    end
end

function OnDestroy()
    log("example_player_input_raycast: OnDestroy em '" .. entity:GetName() .. "'")

    -- Libera o cursor ao sair - sem isto, se este script for o unico
    -- travando o cursor e a PlayWindow fechar por outro motivo (nao o
    -- Esc tratado acima), o cursor do SO ficaria "preso" ate o processo
    -- inteiro reiniciar o modo (na pratica, PlayWindow::Open ja reseta o
    -- CursorMode para Normal a cada nova sessao - ver Input::SetContext -
    -- mas soltar aqui tambem e uma boa pratica defensiva).
    Input.SetCursorMode(CursorMode.Normal)
end
