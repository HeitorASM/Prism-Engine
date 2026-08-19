#pragma once

// ============================================================================
// Raycast.h
// Tipos de dado usados por Scene::VisualRaycast (ver Scene.h) - um raio de
// mundo (origem + direcao) e o resultado de testa-lo contra as bounding
// boxes VISUAIS (MeshRendererComponent) das entidades da cena.
//
// NAO CONFUNDIR com PhysicsEngine::RaycastHit / PhysicsEngine::Raycast
// (Physics/PhysicsEngine.h) - aquele e um raycast FISICO de verdade via
// Jolt, contra ColliderComponent, e so funciona enquanto a Scene esta
// "rodando" (Scene::IsRunning) com corpos fisicos criados. Este aqui
// (VisualRay/VisualRaycastHit) testa contra a bounding box do MESH
// desenhado, funciona a qualquer momento (inclusive parado, editando), e
// nao depende de fisica nenhuma existir - por isso o nome "Visual" em vez
// de reusar o mesmo nome. Foi criado para o picking por clique da viewport
// do editor (ver EditorLayer::RenderViewportPanel), que precisa funcionar
// mesmo fora do modo Play/sem nenhum RigidBodyComponent na cena.
// ============================================================================

#include "../Core/Base.h"
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace Prism {

    // Um raio de mundo: parte de Origin e segue Direction (deve vir
    // normalizada - Scene::VisualRaycast nao normaliza por conta propria,
    // ja que origens diferentes as vezes precisam da direcao "crua" para
    // outros calculos, ex: EditorLayer construindo o raio a partir do
    // unprojection do mouse).
    struct VisualRay {
        glm::vec3 Origin{ 0.0f };
        glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
    };

    // Resultado de um Scene::VisualRaycast bem-sucedido. 'Hit' e o dado
    // principal a checar primeiro - os outros campos so sao validos quando
    // Hit e true (mesmo padrao de "resultado opcional simples" que
    // PhysicsEngine::RaycastHit ja usa).
    struct VisualRaycastHit {
        bool Hit = false;
        entt::entity Entity = entt::null;

        // Distancia ao longo do raio (Origin + Direction * Distance = Point)
        // ate o ponto de impacto - usada para saber qual entidade esta MAIS
        // PERTO quando o raio atravessa a bounding box de varias.
        float Distance = 0.0f;

        // Ponto de impacto em espaco de mundo - guardado pronto (em vez de
        // exigir recalcular Origin + Direction * Distance sempre) porque
        // quem chama VisualRaycast tipicamente quer o ponto de impacto
        // direto (ex: posicionar um efeito, ou um futuro gizmo de debug do raio).
        glm::vec3 Point{ 0.0f };
    };

}
