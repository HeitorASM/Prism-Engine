#include "Scene.h"
#include "Entity.h"
#include <algorithm>

namespace Prism {

    Scene::Scene(const std::string& name) : m_Name(name) {}

    Ref<Scene> Scene::Create(const std::string& name) {
        return CreateRef<Scene>(name);
    }

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity(m_Registry.create(), this);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<TagComponent>(name.empty() ? std::string("Entity") : name);
        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        entt::entity handle = entity.GetHandle();

        // Se a entidade tem um pai, tira ela da lista de Children dele
        // primeiro - senao o pai ficaria com um entt::entity morto na
        // lista (use-after-free logico na proxima vez que algo iterar
        // Children).
        if (auto* rel = m_Registry.try_get<RelationshipComponent>(handle)) {
            if (rel->Parent != entt::null) {
                if (auto* parentRel = m_Registry.try_get<RelationshipComponent>(rel->Parent)) {
                    auto& siblings = parentRel->Children;
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), handle), siblings.end());
                }
            }

            // Destroi a subarvore inteira primeiro (recursivo, filhos dos
            // filhos inclusive) - copia a lista de Children antes de
            // iterar porque cada DestroyEntity recursiva mexe na propria
            // lista de Children do PAI atual (que e outra entidade, sem
            // problema), mas nunca na lista que estamos iterando aqui.
            std::vector<entt::entity> childrenCopy = rel->Children;
            for (entt::entity childHandle : childrenCopy) {
                if (m_Registry.valid(childHandle))
                    DestroyEntity(Entity(childHandle, this));
            }
        }

        m_Registry.destroy(handle);
    }

    bool Scene::IsAncestorOf(entt::entity possibleAncestor, entt::entity entity) {
        if (possibleAncestor == entity)
            return true;

        auto* rel = m_Registry.try_get<RelationshipComponent>(entity);
        if (!rel || rel->Parent == entt::null)
            return false;

        return IsAncestorOf(possibleAncestor, rel->Parent);
    }

    bool Scene::SetParent(Entity child, Entity newParent) {
        entt::entity childHandle = child.GetHandle();
        entt::entity newParentHandle = newParent ? newParent.GetHandle() : entt::null;

        if (childHandle == newParentHandle)
            return false; // nao pode ser pai de si mesmo

        // Recusa ciclos: 'child' nao pode virar filho de um dos seus
        // proprios descendentes (isso quebraria GetWorldTransform, que
        // subiria a arvore para sempre). IsAncestorOf(child, newParent)
        // == true significa "child e ancestral de newParent", ou seja,
        // newParent esta ABAIXO de child na arvore atual.
        if (newParentHandle != entt::null && IsAncestorOf(childHandle, newParentHandle))
            return false;

        auto& childRel = m_Registry.get_or_emplace<RelationshipComponent>(childHandle);

        // Remove do pai antigo, se houver, antes de trocar.
        if (childRel.Parent != entt::null) {
            if (auto* oldParentRel = m_Registry.try_get<RelationshipComponent>(childRel.Parent)) {
                auto& siblings = oldParentRel->Children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), childHandle), siblings.end());
            }
        }

        childRel.Parent = newParentHandle;

        if (newParentHandle != entt::null) {
            auto& newParentRel = m_Registry.get_or_emplace<RelationshipComponent>(newParentHandle);
            newParentRel.Children.push_back(childHandle);
        }

        return true;
    }

    glm::mat4 Scene::GetWorldTransform(Entity entity) {
        entt::entity handle = entity.GetHandle();
        glm::mat4 local = m_Registry.get<TransformComponent>(handle).GetTransform();

        auto* rel = m_Registry.try_get<RelationshipComponent>(handle);
        if (!rel || rel->Parent == entt::null || !m_Registry.valid(rel->Parent))
            return local;

        return GetWorldTransform(Entity(rel->Parent, this)) * local;
    }

    void Scene::OnUpdate(float deltaTime) {
        // TODO(fase seguinte): scripts (Lua) e fisica (Box3D) vao atualizar
        // TransformComponent das entidades relevantes aqui. Por ora a Scene
        // e estatica - so o EditorLayer gira a entidade de teste manualmente
        // para fins de demonstracao visual (ver EditorLayer::RenderScene).
    }

}
