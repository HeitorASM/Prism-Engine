-- example_spin.lua
-- Script de exemplo para a Prism Engine - mostra a API atual do
-- ScriptEngine (ver Prism/src/Prism/Scripting/ScriptEngine.h/.cpp).
--
-- Para usar: copie este arquivo para a pasta Scripts/ do seu projeto,
-- adicione um ScriptComponent a uma entidade (menu Entidade > Criar
-- Character ja adiciona um vazio, ou "+ Add Component > Script" em
-- qualquer entidade), digite "example_spin.lua" no campo Arquivo da
-- Properties panel, e aperte o botao "Play" na menu bar.
--
-- Tres funcoes especiais, TODAS opcionais (defina so as que precisar):
--   OnCreate()            -- chamada uma vez, quando o Play comeca (ou o
--                             script e recarregado via botao "Recarregar")
--   OnUpdate(deltaTime)   -- chamada toda frame enquanto o Play estiver ligado
--   OnDestroy()           -- chamada uma vez quando o Play para (ou a
--                             entidade e destruida enquanto rodando)
--
-- A variavel global `entity` ja existe dentro do script - e a PROPRIA
-- entidade dona deste ScriptComponent (ver env["entity"] em
-- ScriptEngine::LoadScript).

local rotationSpeed = 90.0 -- graus por segundo

function OnCreate()
    log("example_spin: OnCreate em '" .. entity:GetName() .. "'")
end

function OnUpdate(deltaTime)
    local transform = entity:GetTransform()
    transform.Rotation.y = transform.Rotation.y + rotationSpeed * deltaTime

    -- NOTA: se esta entidade tiver RigidBodyComponent+ColliderComponent
    -- (fisica ativa) e voce quer que ela SEMPRE gire nesta velocidade
    -- exata (nunca varie por colisao/torque), marque
    -- RigidBodyComponent::FixedRotation = true na Properties panel - senao
    -- o passo de fisica do PROXIMO frame pode sincronizar de volta uma
    -- rotacao diferente (se o corpo colidir com algo, por exemplo). A
    -- maioria dos objetos "so girando" (decorativos, sem fisica) nao
    -- precisa disto - sem corpo fisico associado, nao ha nada competindo
    -- com o valor que este script escreve.
end

function OnDestroy()
    log("example_spin: OnDestroy em '" .. entity:GetName() .. "'")
end
