
local moveSpeed = 4.0      
local jumpImpulse = 5.0
local rayDistance = 3.0    
local mouseSensitivity = 0.15 

function OnCreate()

    Input.SetCursorMode(CursorMode.Locked)
end

function OnUpdate(deltaTime)

    local mouseDeltaX = Input.GetMouseDeltaX()
    local mouseDeltaY = Input.GetMouseDeltaY()
    local transform = entity:GetTransform()

    transform.Rotation.y = transform.Rotation.y - mouseDeltaX * mouseSensitivity

    local newPitch = transform.Rotation.x - mouseDeltaY * mouseSensitivity
    if newPitch > 89.0 then newPitch = 89.0 end
    if newPitch < -89.0 then newPitch = -89.0 end
    transform.Rotation.x = newPitch

    local moveX, moveZ = 0.0, 0.0
    if Input.IsKeyDown(Key.W) then moveZ = moveZ - 1.0 end
    if Input.IsKeyDown(Key.S) then moveZ = moveZ + 1.0 end
    if Input.IsKeyDown(Key.A) then moveX = moveX - 1.0 end
    if Input.IsKeyDown(Key.D) then moveX = moveX + 1.0 end

    local forward = entity:GetForward()
    local right = entity:GetRight()
    local worldMoveX = right.x * moveX + forward.x * (-moveZ)
    local worldMoveZ = right.z * moveX + forward.z * (-moveZ)

    local vel = entity:GetVelocity()
    entity:SetVelocity(worldMoveX * moveSpeed, vel.y, worldMoveZ * moveSpeed)


    if Input.IsKeyPressed(Key.Space) then
        entity:ApplyImpulse(0, jumpImpulse, 0)
    end

    local origin = entity:GetTransform().Translation
    local hit = Physics.Raycast(origin, entity:GetForward(), rayDistance)

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
