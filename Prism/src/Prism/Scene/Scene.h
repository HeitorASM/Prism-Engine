#pragma once

// ============================================================================
// Scene.h
// Uma Scene e o container de tudo que existe no mundo: as Entities e seus
// Components. Ela e a dona do entt::registry por baixo - Entity (ver
// Entity.h) e so um "handle" leve (ID + ponteiro pra Scene dona), nunca dono
// de dado nenhum.
//
// Isto substitui o cubo hardcoded que o EditorLayer desenhava diretamente
// via Renderer::DrawTestCube (fase anterior): agora esse mesmo cubo e uma
// Entity real dentro de uma Scene, com TagComponent + TransformComponent +
// MeshRendererComponent - exatamente os components que qualquer outra
// entidade adicionada no editor tambem vai ter.
// ============================================================================

#include "../Core/Base.h"
#include "Components.h"
#include <entt/entt.hpp>
#include <string>
#include <cstdint>

namespace Prism {

    class Entity;

    class Scene {
    public:
        Scene(const std::string& name = "Untitled Scene");
        ~Scene() = default;

        // Cria uma entidade nova, ja com TagComponent + TransformComponent
        // (toda entidade da engine tem essas duas por definicao - ver
        // Components.h). O nome e so um rotulo de exibicao, pode repetir.
        Entity CreateEntity(const std::string& name = "Entity");

        void DestroyEntity(Entity entity);

        // Chamado uma vez por frame pelo dono da Scene (hoje, EditorLayer).
        // Ainda nao ha nada para simular (fisica/scripts entram em fases
        // futuras - ver README) - existe desde ja para nao exigir mudar a
        // assinatura quando esses sistemas chegarem.
        void OnUpdate(float deltaTime);

        const std::string& GetName() const { return m_Name; }

        // Acesso direto ao registro EnTT - usado pelo Renderer/EditorLayer
        // para iterar sobre grupos de components (ex: todas as entidades
        // com MeshRendererComponent, para desenhar a cena inteira).
        entt::registry& GetRegistry() { return m_Registry; }

        // Percorre f(EntityID, TagComponent&) para toda entidade da cena -
        // usado pela Hierarchy panel do editor para listar entidades sem
        // que o painel precise conhecer entt diretamente.
        template<typename Fn>
        void ForEachEntity(Fn&& f) {
            m_Registry.view<TagComponent>().each([&](auto entityHandle, TagComponent& tag) {
                f(entityHandle, tag);
            });
        }

        static Ref<Scene> Create(const std::string& name = "Untitled Scene");

    private:
        std::string m_Name;
        entt::registry m_Registry;

        friend class Entity;
    };

}
