local rotationSpeed = 90.0 -- graus por segundo

function OnCreate()
    log("example_spin: OnCreate em '" .. entity:GetName() .. "'")
end

function OnUpdate(deltaTime)
    local transform = entity:GetTransform()
    transform.Rotation.y = transform.Rotation.y + rotationSpeed * deltaTime

end

function OnDestroy()
    log("example_spin: OnDestroy em '" .. entity:GetName() .. "'")
end
