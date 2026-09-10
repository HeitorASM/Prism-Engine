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

        // --- Hierarquia (pai/filhos) --------------------------------------
        // Metodos de conveniencia sobre RelationshipComponent (ver
        // Components.h) - nao sao o unico jeito de navegar a hierarquia
        // (Scene::SetParent/GetRegistry ja bastam para o editor, ver
        // EditorLayer::RenderHierarchyPanel), mas evitam scripts Lua
        // (ScriptEngine.cpp) precisarem manipular RelationshipComponent
        // cru so para "achar minha camera filha" - ver
        // example_player_input_raycast.lua para o caso de uso que
        // motivou isto.

        // entt::null (Entity invalida, ver IsValid()) se esta entidade nao
        // tiver RelationshipComponent ou nao tiver pai (raiz da cena).
        Entity GetParent() {
            if (!HasComponent<RelationshipComponent>())
                return Entity();
            entt::entity parent = GetComponent<RelationshipComponent>().Parent;
            return (parent == entt::null) ? Entity() : Entity(parent, m_Scene);
        }

        size_t GetChildCount() {
            if (!HasComponent<RelationshipComponent>())
                return 0;
            return GetComponent<RelationshipComponent>().Children.size();
        }

        // Entity invalida se 'index' estiver fora do range - retornar uma
        // Entity invalida (em vez de PRISM_ASSERT) e proposital aqui: um
        // script Lua iterando 0..GetChildCount()-1 nunca deveria bater
        // nisso, mas um indice invalido vindo de um script com bug nao
        // deveria derrubar o programa inteiro (ver mesma filosofia em
        // ScriptEngine::RegisterAPI sobre erros de script serem
        // reportados, nao crashes).
        Entity GetChildAt(size_t index) {
            if (!HasComponent<RelationshipComponent>())
                return Entity();
            auto& children = GetComponent<RelationshipComponent>().Children;
            if (index >= children.size())
                return Entity();
            return Entity(children[index], m_Scene);
        }

        // Busca um filho DIRETO (nao neto/bisneto) por TagComponent::Tag,
        // igual Transform.Find() da Unity ou get_node() da Godot quando
        // usado com um nome simples. Entity invalida se nao encontrar.
        // Comparacao exata de string (sem case-insensitive, sem wildcard/
        // path com "/") - suficiente para o caso de uso atual (um script
        // de player achando "Camera" ou similar entre poucos filhos
        // diretos); um sistema de path completo tipo Godot
        // ("Body/Camera/Gun") fica para se/quando a necessidade aparecer.
        Entity GetChild(const std::string& name) {
            if (!HasComponent<RelationshipComponent>())
                return Entity();
            for (entt::entity childHandle : GetComponent<RelationshipComponent>().Children) {
                Entity child(childHandle, m_Scene);
                if (child.HasComponent<TagComponent>() && child.GetComponent<TagComponent>().Tag == name)
                    return child;
            }
            return Entity();
        }

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
