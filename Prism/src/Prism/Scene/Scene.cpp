#include "Scene.h"
#include "Entity.h"
#include "../Scripting/ScriptEngine.h"
#include "../Physics/PhysicsEngine.h"
#include "../Project/Project.h"
#include "../Core/Log.h"
#include <algorithm>
#include <filesystem>

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

        // Se o modo Play estiver rodando e esta entidade tiver um script
        // carregado, desliga ele (chama OnDestroy()) ANTES de destruir a
        // entidade - depois de m_Registry.destroy() os components dela
        // (incluindo ScriptComponent) deixam de existir, entao precisa ser
        // nesta ordem.
        if (m_IsRunning && m_Registry.all_of<ScriptComponent>(handle))
            ScriptEngine::UnloadScript(entity);

        // Mesma logica para o corpo fisico: destroi ANTES da entidade
        // sumir do registry, senao PhysicsEngine::DestroyBodyForEntity
        // nao teria mais como checar RigidBodyComponent/ColliderComponent
        // (embora hoje ele nao precise disso para destruir - so consulta o
        // proprio mapa EntityToBody - mantemos a ordem por consistencia
        // com o padrao acima e para proteger contra mudancas futuras).
        if (m_IsRunning && m_Registry.all_of<RigidBodyComponent, ColliderComponent>(handle))
            PhysicsEngine::DestroyBodyForEntity(*this, entity);

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
        // Scripts e fisica so rodam durante o modo Play (m_IsRunning) -
        // fora disso a Scene fica estatica, exibindo so o estado editado
        // (mesmo comportamento de Unity/Unreal/Godot fora do botao Play).
        if (!m_IsRunning)
            return;

        // Fisica ANTES dos scripts, de proposito: assim um script que leia
        // TransformComponent (via entity:GetTransform()) em OnUpdate() ve
        // a posicao ja atualizada pela simulacao deste MESMO frame, nao a
        // do frame anterior - importante por exemplo para um script que
        // decide algo com base em "onde meu personagem esta depois da
        // fisica mover ele".
        PhysicsEngine::Simulate(*this, deltaTime);

        auto view = m_Registry.view<ScriptComponent>();
        for (auto entityHandle : view) {
            // Copia o handle para uma Entity de curta duracao - so para
            // repassar ao ScriptEngine, que trabalha em termos de Entity
            // (nao entt::entity cru) para poder chamar GetComponent<T>()
            // de dentro dos bindings Lua (ver ScriptEngine.cpp).
            ScriptEngine::UpdateScript(Entity(entityHandle, this), deltaTime);
        }
    }

    void Scene::OnScriptsStart() {
        if (m_IsRunning)
            return; // idempotente - ja rodando, nao recarrega tudo de novo
        m_IsRunning = true;

        // Fisica ANTES dos scripts: PhysicsEngine::OnSceneStart cria um
        // corpo Box3D para toda entidade RigidBody+Collider ja existente -
        // se um script OnCreate() precisar aplicar uma forca/impulso
        // imediatamente (ver ScriptEngine::RegisterAPI, ApplyForce), o
        // corpo fisico ja precisa existir nesse momento.
        PhysicsEngine::OnSceneStart(*this);

        auto view = m_Registry.view<ScriptComponent>();
        for (auto entityHandle : view) {
            auto& script = view.get<ScriptComponent>(entityHandle);
            if (script.ScriptPath.empty())
                continue; // ScriptComponent existe mas nenhum arquivo foi escolhido ainda - nada a carregar

            // ScriptComponent::ScriptPath e RELATIVO a Scripts/ do projeto
            // ativo (ver comentario em Components.h) - resolve para
            // absoluto aqui, ja que ScriptEngine::LoadScript le do disco
            // direto e nao conhece Project.
            auto project = Project::GetActive();
            if (!project) {
                PRISM_CORE_ERROR("Scene::OnScriptsStart: nenhum projeto ativo - nao e possivel resolver o caminho de scripts.");
                break;
            }
            std::filesystem::path absolutePath = project->GetScriptDirectory() / script.ScriptPath;
            ScriptEngine::LoadScript(Entity(entityHandle, this), absolutePath);
        }
    }

    void Scene::OnScriptsStop() {
        if (!m_IsRunning)
            return; // idempotente
        m_IsRunning = false;

        auto view = m_Registry.view<ScriptComponent>();
        for (auto entityHandle : view)
            ScriptEngine::UnloadScript(Entity(entityHandle, this));

        // Fisica DEPOIS dos scripts: OnDestroy() de um script ainda pode
        // querer ler a posicao final do corpo fisico (ex: salvar onde o
        // personagem parou) antes do mundo Box3D ser destruido.
        PhysicsEngine::OnSceneStop(*this);
    }

}
