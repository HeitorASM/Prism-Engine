#include "Scene.h"
#include "Entity.h"
#include "ComponentRegistry.h"
#include "../Scripting/ScriptEngine.h"
#include "../Physics/PhysicsEngine.h"
#include "../Project/Project.h"
#include "../Renderer/PrimitiveMeshFactory.h"
#include "../Renderer/Renderer.h"
#include "../Core/Log.h"
#include <algorithm>
#include <filesystem>
#include <limits>
#include <cmath>
#include <unordered_map>

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

    Ref<Scene> Scene::Clone() {
        ComponentRegistry::RegisterAll(); // ver comentario equivalente em SceneSerializer - idempotente

        Ref<Scene> cloned = Scene::Create(m_Name);

        // Mapa handle-no-original -> handle-no-clone, preenchido durante a
        // primeira passada abaixo e usado na segunda (RelationshipComponent)
        // para remapear Parent/Children - mesma necessidade que
        // SceneSerializer::Deserialize ja tem (ver comentario la sobre
        // "pai pode aparecer depois do filho"), so que aqui o mapa e por
        // HANDLE diretamente (nao por indice posicional), ja que ambas as
        // Scenes (original e clone) existem ao mesmo tempo em memoria -
        // nao ha necessidade do truque de indice que a serializacao usa
        // para persistir em disco.
        std::unordered_map<entt::entity, entt::entity> handleMap;

        ForEachEntity([&](entt::entity originalHandle, TagComponent& tag) {
            Entity original(originalHandle, this);
            Entity copy = cloned->CreateEntity(tag.Tag);
            handleMap[originalHandle] = copy.GetHandle();

            // TransformComponent: toda entidade ja tem (CreateEntity acima
            // ja adicionou um default) - so precisa SOBRESCREVER com os
            // valores reais, nao AddComponent de novo (que faria
            // PRISM_ASSERT falhar - ver Entity::AddComponent). Nao passa
            // por ComponentRegistry porque TransformComponent nunca esta
            // la (toda entidade tem por definicao - ver comentario em
            // ComponentRegistration.cpp).
            copy.GetComponent<TransformComponent>() = original.GetComponent<TransformComponent>();

            // Todo Component OPCIONAL (ver ComponentRegistry.h) - cobre
            // MeshRenderer/Light/Collider/RigidBody/Script/Camera/Raycast
            // hoje, e qualquer Component futuro que se registre em
            // ComponentRegistration.cpp, sem precisar tocar em Clone()
            // de novo.
            //
            // DIFERENCA INTENCIONAL vs SceneSerializer: ComponentTypeInfo::Copy
            // copia o Component INTEIRO (todos os campos, incluindo os
            // transientes de runtime como RaycastComponent::Hit/HitEntity/
            // HitPoint - ver ComponentRegistration.cpp), enquanto
            // Serialize/Deserialize propositalmente OMITE esses campos (nao
            // fazem sentido persistir em disco). Aqui faz sentido copiar
            // tudo: Clone() duplica o estado EXATO de agora, nao "reseta
            // para o que seria salvo em disco".
            for (auto& info : ComponentRegistry::GetAll()) {
                if (info.Has(original))
                    info.Copy(original, copy);
            }
        });

        // Segunda passada: RelationshipComponent - so depois que TODAS as
        // entidades do clone existem (handleMap completo), mesmo motivo
        // que SceneSerializer::Deserialize ja documenta (o pai de uma
        // entidade pode ter sido criado DEPOIS dela nesta iteracao).
        ForEachEntity([&](entt::entity originalHandle, TagComponent&) {
            auto* rel = m_Registry.try_get<RelationshipComponent>(originalHandle);
            if (!rel || rel->Parent == entt::null)
                return;

            auto it = handleMap.find(rel->Parent);
            if (it == handleMap.end())
                return; // pai fora desta Scene (nao deveria acontecer) - clone fica sem pai em vez de crashar

            Entity clonedChild(handleMap[originalHandle], cloned.get());
            Entity clonedParent(it->second, cloned.get());
            cloned->SetParent(clonedChild, clonedParent);
        });

        return cloned;
    }

    // Helper interno (sem parentesco ainda) de DuplicateEntity: cria UMA
    // copia de 'source' com TransformComponent + todo Component opcional
    // presente (mesmo mecanismo de ComponentRegistry::Copy usado por
    // Clone(), ver comentario la), e recursivamente duplica os filhos
    // dela, preenchendo 'handleMap' (original -> copia) para que o
    // chamador possa remontar o parentesco INTERNO da subarvore depois
    // (o pai da RAIZ da subarvore e tratado por fora, em
    // Scene::DuplicateEntity - esta funcao nunca chama SetParent para o
    // topo, so entre pais/filhos dentro da propria copia).
    static Entity DuplicateEntityRecursive(Scene& scene, Entity source, std::unordered_map<entt::entity, entt::entity>& handleMap) {
        Entity copy = scene.CreateEntity(source.GetComponent<TagComponent>().Tag);
        handleMap[source.GetHandle()] = copy.GetHandle();

        copy.GetComponent<TransformComponent>() = source.GetComponent<TransformComponent>();

        for (auto& info : ComponentRegistry::GetAll()) {
            if (info.Has(source))
                info.Copy(source, copy);
        }

        if (auto* rel = scene.GetRegistry().try_get<RelationshipComponent>(source.GetHandle())) {
            // Copia a lista de filhos ANTES de iterar: CreateEntity() (chamada
            // dentro da recursao, via DuplicateEntityRecursive) pode invalidar
            // referencias para dentro do registry se o vector interno do EnTT
            // precisar realocar - iterar uma copia evita esse risco, mesmo
            // padrao ja usado por Scene::DestroyEntity e EditorLayer::RenderHierarchyNode.
            std::vector<entt::entity> childrenCopy = rel->Children;
            for (entt::entity childHandle : childrenCopy) {
                Entity child(childHandle, source.GetScene());
                Entity childCopy = DuplicateEntityRecursive(scene, child, handleMap);
                scene.SetParent(childCopy, copy);
            }
        }

        return copy;
    }

    Entity Scene::DuplicateEntity(Entity source) {
        if (!source)
            return Entity();

        std::unordered_map<entt::entity, entt::entity> handleMap;
        Entity copy = DuplicateEntityRecursive(*this, source, handleMap);

        // A copia nasce IRMA do original (mesmo pai) - se 'source' tiver
        // pai, reparenta a copia para debaixo dele; senao a copia ja
        // nasceu raiz (CreateEntity nunca adiciona RelationshipComponent
        // sozinho) e nao ha nada a fazer.
        if (auto* rel = m_Registry.try_get<RelationshipComponent>(source.GetHandle())) {
            if (rel->Parent != entt::null) {
                Entity parent(rel->Parent, this);
                SetParent(copy, parent);
            }
        }

        return copy;
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

    // Teste raio-vs-AABB classico ("slab method": trata a caixa como a
    // intersecao de 3 pares de planos, um por eixo, e vai estreitando o
    // intervalo [tMin, tMax] de parametros t do raio que ainda esta dentro
    // de TODOS os pares - se o intervalo fechar (tMin > tMax) antes de
    // testar os 3 eixos, o raio nunca passa dentro da caixa). Espaço LOCAL
    // da propria AABB - ver comentario de chamada em Scene::Raycast sobre
    // como o raio chega ja transformado para esse espaço.
    // Retorna true e preenche outT (distancia de entrada na caixa, sempre
    // >= 0) em caso de acerto.
    static bool RayIntersectsAABB(const glm::vec3& origin, const glm::vec3& dir,
                                   const glm::vec3& boxMin, const glm::vec3& boxMax,
                                   float& outT) {
        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();

        for (int axis = 0; axis < 3; ++axis) {
            float o = origin[axis];
            float d = dir[axis];
            float mn = boxMin[axis];
            float mx = boxMax[axis];

            if (std::abs(d) < 1e-8f) {
                // Raio paralelo a este par de planos: so passa se a origem
                // ja estiver dentro da fatia deste eixo (o Plano, com
                // espessura zero no Y, e o caso mais comum disto - um raio
                // vindo de cima, quase vertical, tem Direction.y != 0 na
                // pratica quase sempre, entao isto raramente dispara para
                // ele; ainda assim tratado aqui por seguranca/correcao geral).
                if (o < mn || o > mx)
                    return false;
                continue;
            }

            float invD = 1.0f / d;
            float t0 = (mn - o) * invD;
            float t1 = (mx - o) * invD;
            if (t0 > t1)
                std::swap(t0, t1);

            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMin > tMax)
                return false;
        }

        outT = tMin;
        return true;
    }

    VisualRaycastHit Scene::VisualRaycast(const VisualRay& ray, float maxDistance) {
        VisualRaycastHit best;
        best.Distance = maxDistance;

        auto view = m_Registry.view<MeshRendererComponent>();
        for (auto handle : view) {
            Entity entity(handle, this);
            glm::mat4 world = GetWorldTransform(entity);
            glm::mat4 inverseWorld = glm::inverse(world);

            // Transforma o raio para o espaço LOCAL da entidade (em vez de
            // transformar a AABB local para o mundo) - mais barato E mais
            // simples de fazer certo: a AABB continua axis-aligned no seu
            // proprio espaço, sem precisar recalcular os 8 cantos
            // transformados toda vez (o que geraria uma caixa maior que a
            // real sempre que houver rotacao). Direction usa w=0 (vetor,
            // nao ponto) de proposito - nao deve ser afetado pela
            // translacao da matriz.
            glm::vec3 localOrigin = glm::vec3(inverseWorld * glm::vec4(ray.Origin, 1.0f));
            glm::vec3 localDir = glm::vec3(inverseWorld * glm::vec4(ray.Direction, 0.0f));

            auto& meshComp = view.get<MeshRendererComponent>(handle);

            // Mesh resolvida (primitiva OU modelo importado) via o MESMO
            // helper que Renderer usa para DESENHAR a entidade (ver
            // Renderer::ResolveMesh) - picking e desenho precisam
            // concordar sobre "qual malha e essa", senao o clique
            // acertaria uma caixa de tamanho errado (ex: bounds da
            // primitiva 'Cubo' enquanto a tela ja mostra um modelo
            // importado bem maior/menor).
            Mesh* mesh = Renderer::ResolveMesh(meshComp);
            if (!mesh) continue; // Renderer::Init() nao rodou ainda (nao deveria acontecer na pratica - mesma guarda defensiva usada em Renderer::DrawMesh)
            LocalBounds bounds;
            bounds.Min = mesh->GetLocalBoundsMin();
            bounds.Max = mesh->GetLocalBoundsMax();

            float t;
            if (!RayIntersectsAABB(localOrigin, localDir, bounds.Min, bounds.Max, t))
                continue;

            // 't' esta em espaço LOCAL, onde 'localDir' nao é necessariamente
            // unitario (a inversa da world transform pode escalar) - reescala
            // pela razao entre o comprimento percorrido em mundo vs em
            // espaço local para obter uma distancia de MUNDO comparavel
            // entre entidades com escalas diferentes.
            glm::vec3 localHitPoint = localOrigin + localDir * t;
            glm::vec3 worldHitPoint = glm::vec3(world * glm::vec4(localHitPoint, 1.0f));
            float worldDistance = glm::length(worldHitPoint - ray.Origin);

            if (worldDistance < best.Distance) {
                best.Hit = true;
                best.Entity = handle;
                best.Distance = worldDistance;
                best.Point = worldHitPoint;
            }
        }

        return best;
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

        // RaycastComponent tambem ANTES dos scripts, mesmo motivo: um
        // script que leia o resultado de um sensor de raycast da propria
        // entidade (ou de outra) em OnUpdate() deve ver o hit deste MESMO
        // frame, nao o do frame anterior.
        UpdateRaycastComponents();

        auto view = m_Registry.view<ScriptComponent>();
        for (auto entityHandle : view) {
            // Copia o handle para uma Entity de curta duracao - so para
            // repassar ao ScriptEngine, que trabalha em termos de Entity
            // (nao entt::entity cru) para poder chamar GetComponent<T>()
            // de dentro dos bindings Lua (ver ScriptEngine.cpp).
            ScriptEngine::UpdateScript(Entity(entityHandle, this), deltaTime);
        }
    }

    void Scene::UpdateRaycastComponents() {
        auto view = m_Registry.view<RaycastComponent>();
        for (auto entityHandle : view) {
            auto& raycast = view.get<RaycastComponent>(entityHandle);
            if (!raycast.Enabled)
                continue; // resultado antigo fica congelado (ver comentario em Components.h)

            Entity entity(entityHandle, this);
            glm::mat4 world = GetWorldTransform(entity);

            // TargetPosition e um PONTO em espaco local (nao um vetor
            // direcao independente) - transforma tanto a origem (0,0,0
            // local, ou seja, a propria posicao de mundo da entidade)
            // quanto o alvo para espaco de mundo usando a MESMA matriz,
            // e a diferenca entre os dois pontos e o raio de mundo certo -
            // ja rotacionado/escalado igual a entidade, sem nenhuma
            // trigonometria manual aqui (ver comentario de RaycastComponent).
            glm::vec3 worldOrigin = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            glm::vec3 worldTarget = glm::vec3(world * glm::vec4(raycast.TargetPosition, 1.0f));
            glm::vec3 worldDirection = worldTarget - worldOrigin;

            // PhysicsEngine::Raycast normaliza a direcao internamente e
            // multiplica por maxDistance (ver comentario la) - passamos o
            // COMPRIMENTO de worldDirection como maxDistance para o raio
            // ir exatamente ate TargetPosition, nem mais nem menos (o
            // mesmo comportamento do RayCast3D da Godot: o alcance E a
            // distancia ate o ponto configurado, nao um valor separado).
            float maxDistance = glm::length(worldDirection);

            // Monta a lista de entidades a ignorar (ver
            // RaycastComponent::IgnoreParentAndSiblings, Components.h) -
            // recalculada a cada frame (barato: so percorre pai + filhos
            // diretos do pai, nao a arvore inteira) porque a hierarquia
            // pode mudar em runtime (SetParent via script/editor).
            // Nota: a propria 'entity' (dona deste RaycastComponent) acaba
            // entrando na lista tambem, ja que ela e uma das "irmas" dela
            // mesma sob o mesmo pai - isso e inofensivo (nao muda o
            // resultado), so redundante.
            std::vector<entt::entity> ignoreEntities;
            if (raycast.IgnoreParentAndSiblings) {
                Entity parent = entity.GetParent();
                if (parent.IsValid()) {
                    ignoreEntities.push_back(parent.GetHandle());
                    for (size_t i = 0; i < parent.GetChildCount(); i++) {
                        Entity sibling = parent.GetChildAt(i);
                        if (sibling.IsValid())
                            ignoreEntities.push_back(sibling.GetHandle());
                    }
                }
            }

            RaycastHit hit = (maxDistance > 0.0001f)
                ? PhysicsEngine::Raycast(*this, worldOrigin, worldDirection, maxDistance, ignoreEntities)
                : RaycastHit{}; // TargetPosition na origem (raio de comprimento zero) - nunca acerta nada

            raycast.Hit = hit.Hit;
            raycast.HitEntity = hit.Entity;
            raycast.HitPoint = hit.Point;
            raycast.HitNormal = hit.Normal;
            raycast.HitDistance = hit.Distance;
        }
    }

    void Scene::OnScriptsStart() {
        if (m_IsRunning)
            return; // idempotente - ja rodando, nao recarrega tudo de novo
        m_IsRunning = true;

        // Fisica ANTES dos scripts: PhysicsEngine::OnSceneStart cria um
        // corpo Jolt para toda entidade RigidBody+Collider ja existente -
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
        // personagem parou) antes do mundo fisico ser destruido.
        PhysicsEngine::OnSceneStop(*this);
    }

}
