-- Exemplo (Fase 3): variaveis publicas, trigger e Scene.
-- Coloque num objeto com RigidBody + Collider marcado como Trigger.

Properties = {
    spinSpeed = 90.0,
    points = 10,
}

local collected = false

function OnUpdate(dt)
    local t = entity:GetTransform()
    entity:SetRotation(0, (Time.Elapsed() * Properties.spinSpeed) % 360, 0)
end

function OnTriggerEnter(other)
    if collected then return end
    if other:GetName() == "Player" then
        collected = true
        print("Moeda coletada! +" .. Properties.points)
        entity:Destroy() -- removida no fim do frame
    end
end
