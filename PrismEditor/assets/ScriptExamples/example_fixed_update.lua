-- example_fixed_update.lua
-- OnFixedUpdate roda em passo FIXO (Time.FixedDeltaTime = 1/60 s), 0 ou mais vezes
-- por frame, sempre ANTES de cada passo da fisica. Use-o para forcas e velocidades
-- (o resultado nao depende do framerate); deixe OnUpdate para entrada e visual.
--
-- Coloque numa entidade com Rigid Body (Dynamic) + Collider.
-- Hot reload: com o Play ligado, edite e salve este arquivo - ele recarrega sozinho,
-- mantendo as variaveis alteradas em execucao (como 'elapsed' abaixo).

local pushStrength = 12.0   -- constante do arquivo: ao salvar, assume o valor novo
elapsed = 0.0               -- estado de execucao: preservado no hot reload

function OnFixedUpdate(fixedDt)
    elapsed = elapsed + fixedDt
    -- vai e volta no eixo X, 1 ciclo a cada ~2 s
    local direction = math.sin(elapsed * math.pi)
    entity:ApplyForce(direction * pushStrength, 0, 0)
end

function OnReload()
    log("example_fixed_update: recarregado (elapsed = " .. string.format("%.2f", elapsed) .. ")")
end
