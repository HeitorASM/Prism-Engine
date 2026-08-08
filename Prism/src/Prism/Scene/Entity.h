#pragma once

// ============================================================================
// Entity.h
// Uma Entity NAO possui dado nenhum - ela e so um par (ID dentro do
// entt::registry, ponteiro para a Scene dona). Todo dado de verdade fica nos
// Components (ver Components.h), armazenados dentro do registro da Scene.
// Copiar uma Entity e barato de proposito (e so copiar um ID + um ponteiro) -
// isso e o padrao normal de uso de um ECS, bem diferente de copiar um
// GameObject de engines OOP tradicionais.
// ============================================================================

#include "Scene.h"
#include "../Core/Base.h"
#include "../Core/Log.h"
#include <entt/entt.hpp>

namespace Prism {

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene) : m_EntityHandle(handle), m_Scene(scene) {}
        Entity(const Entity&) = default;

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args) {
            PRISM_ASSERT(!HasComponent<T>(), "Entity ja possui este component!");
            return m_Scene->GetRegistry().emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent() {
            PRISM_ASSERT(HasComponent<T>(), "Entity nao possui este component!");
            return m_Scene->GetRegistry().get<T>(m_EntityHandle);
        }

        template<typename T>
        bool HasComponent() {
            return m_Scene->GetRegistry().all_of<T>(m_EntityHandle);
        }

        template<typename T>
        void RemoveComponent() {
            PRISM_ASSERT(HasComponent<T>(), "Entity nao possui este component!");
            m_Scene->GetRegistry().remove<T>(m_EntityHandle);
        }

        bool IsValid() const { return m_Scene && m_Scene->GetRegistry().valid(m_EntityHandle); }
        explicit operator bool() const { return IsValid(); }

        bool operator==(const Entity& other) const {
            return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
        }
        bool operator!=(const Entity& other) const { return !(*this == other); }

        entt::entity GetHandle() const { return m_EntityHandle; }
        Scene* GetScene() const { return m_Scene; }

    private:
        entt::entity m_EntityHandle{ entt::null };
        Scene* m_Scene = nullptr;
    };

}
