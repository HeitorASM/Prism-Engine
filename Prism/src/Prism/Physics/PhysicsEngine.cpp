#include "PhysicsEngine.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Core/Log.h"
#include "../Scripting/ScriptEngine.h"
#include "../Renderer/Renderer.h" // ResolveMesh: geometria dos colliders ConvexHull/TriangleMesh
#include "../Renderer/Mesh.h"
#include "../Project/Project.h"
#include "PhysicsLayers.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Body/BodyFilter.h> // JPH::IgnoreMultipleBodiesFilter (PhysicsEngine::Raycast, ignoreEntities)

#include <thread>
#include <mutex>

namespace Prism {
    // CAMADAS DE FISICA (ver Physics/PhysicsLayers.h): o ObjectLayer do Jolt e
    // (camada do usuario << 1) | movel. O broad-phase continua com DUAS camadas
    // (estatico / movel), escolhidas pelo bit baixo; quem colide com quem e decidido
    // pela matriz do projeto, copiada em OnSceneStart.
    namespace BroadPhaseLayers {
        static constexpr JPH::BroadPhaseLayer Static(0);
        static constexpr JPH::BroadPhaseLayer Moving(1);
        static constexpr uint32_t NumLayers = 2;
    }

    static constexpr JPH::ObjectLayer kNumObjectLayers = (JPH::ObjectLayer)(Physics::kLayerCount * 2);

    // Matriz ativa (copia da do projeto, tirada ao iniciar o Play). Lida pelas threads do Jolt;
    // so e reescrita em OnSceneStart, quando nenhuma simulacao esta rodando.
    static Physics::LayerMatrix s_ActiveMatrix;

    class PrismObjectLayerPairFilter : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
            if (!Physics::IsMovingLayer(inObject1) && !Physics::IsMovingLayer(inObject2))
                return false; // dois estaticos nunca precisam ser testados entre si
            return s_ActiveMatrix.ShouldCollide(Physics::UserLayerOf(inObject1), Physics::UserLayerOf(inObject2));
        }
    };

    class PrismBroadPhaseLayerInterface : public JPH::BroadPhaseLayerInterface {
    public:
        uint32_t GetNumBroadPhaseLayers() const override {
            return BroadPhaseLayers::NumLayers;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
            return Physics::IsMovingLayer(inLayer) ? BroadPhaseLayers::Moving : BroadPhaseLayers::Static;
        }

        // GetBroadPhaseLayerName so existe como metodo puro-virtual quando o profiler do Jolt esta ligado.
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
            switch ((JPH::BroadPhaseLayer::Type)inLayer) {
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::Static:  return "STATIC";
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::Moving: return "MOVING";
                default: return "INVALID";
            }
        }
#endif
    };

    // Filtro ObjectLayer vs BroadPhaseLayer: um objeto estatico so precisa olhar a camada movel.
    class PrismObjectVsBroadPhaseLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
            if (!Physics::IsMovingLayer(inLayer1))
                return inLayer2 == BroadPhaseLayers::Moving;
            return true;
        }
    };

    // Filtro de CONSULTA (raycast): so objetos cuja camada de fisica esta na mascara.
    class PrismLayerMaskObjectFilter : public JPH::ObjectLayerFilter {
    public:
        explicit PrismLayerMaskObjectFilter(uint32_t mask) : m_Mask(mask) {}
        bool ShouldCollide(JPH::ObjectLayer inLayer) const override {
            return (m_Mask & Physics::LayerBit(Physics::UserLayerOf(inLayer))) != 0u;
        }
    private:
        uint32_t m_Mask;
    };

    static PrismBroadPhaseLayerInterface& GetBroadPhaseLayerInterface() {
        static PrismBroadPhaseLayerInterface instance;
        return instance;
    }
    static PrismObjectVsBroadPhaseLayerFilter& GetObjectVsBroadPhaseLayerFilter() {
        static PrismObjectVsBroadPhaseLayerFilter instance;
        return instance;
    }
    static PrismObjectLayerPairFilter& GetObjectLayerPairFilter() {
        static PrismObjectLayerPairFilter instance;
        return instance;
    }

    // Jolt exige uma chamada global de registro de tipos
    // (JPH::RegisterTypes()) e um Factory globais ANTES de qualquer
    // PhysicsSystem ser criado. Feito uma unica vez por processo via flag
    // estatica - seguro mesmo se varias Scenes distintas chamarem
    // OnSceneStart ao longo da vida do editor.
    static void EnsureGlobalInit() {
        static bool s_Initialized = false;
        if (s_Initialized)
            return;

        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        s_Initialized = true;

        // Nunca chamamos JPH::UnregisterTypes() nem deletamos
        // Factory::sInstance, de proposito: e infraestrutura global do
        // PROCESSO (mesma vida util do contexto OpenGL ou da VM Lua
        // base), nao de uma Scene, entao vive ate o processo terminar.
    }

    // --- Conversao glm <-> Jolt ---------------------------------------------
    // JPH::Vec3/JPH::Quat NAO tem o mesmo layout de memoria de
    // glm::vec3/glm::quat (Jolt usa SIMD internamente - Vec3 na verdade
    // ocupa 16 bytes, nao 12), entao precisamos SEMPRE passar pelos
    // metodos/construtores publicos da API, nunca reinterpret_cast.

    static JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    static glm::vec3 FromJolt(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

    // JPH::Quat guarda (x,y,z,w) via GetX/GetY/GetZ/GetW - mapeamento
    // direto com glm::quat(w,x,y,z) na ordem de CONSTRUCAO (glm arma
    // internamente como .x/.y/.z/.w tambem).
    static JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
    static glm::quat FromJolt(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

    // TransformComponent::Rotation e Euler EM GRAUS na ordem yawPitchRoll(Y,X,Z)
    // (ver TransformComponent::GetTransform() em Components.h) - depende
    // so da convencao de eixos da engine, nao da lib de fisica.
    static glm::quat EulerDegreesToQuat(const glm::vec3& eulerDegrees) {
        glm::mat4 rotationMatrix = glm::yawPitchRoll(
            glm::radians(eulerDegrees.y), glm::radians(eulerDegrees.x), glm::radians(eulerDegrees.z));
        return glm::quat_cast(rotationMatrix);
    }

    // Inverso de EulerDegreesToQuat - usado para escrever de volta em
    // TransformComponent::Rotation depois que o Jolt move o corpo (ver
    // Simulate() abaixo).
    static glm::vec3 QuatToEulerDegrees(const glm::quat& q) {
        glm::vec3 pitchYawRoll = glm::eulerAngles(q); // (pitch=x, yaw=y, roll=z) em radianos, convencao glm padrao
        return glm::degrees(glm::vec3(pitchYawRoll.x, pitchYawRoll.y, pitchYawRoll.z));
    }

    // --- Eventos de contato ---------------------------------------------------
    // O Jolt chama OnContactAdded/OnContactRemoved de THREADS DO JOB SYSTEM,
    // durante PhysicsSystem::Update - nunca chamar Lua/ECS de la. Este
    // ouvinte so ENFILEIRA os eventos (sob mutex); Simulate() os drena na
    // thread principal, logo depois do Update, e so entao chama os scripts.
    class PhysicsEngine::ContactCollector : public JPH::ContactListener {
    public:
        struct Event {
            uint32_t EntityA = 0;
            uint32_t EntityB = 0;
            bool Added = true;
            bool Trigger = false; // um dos dois corpos e sensor (Collider::IsTrigger)
        };

        void RegisterBody(JPH::BodyID id, uint32_t entity, bool isSensor) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Bodies[id.GetIndexAndSequenceNumber()] = { entity, isSensor };
        }

        void UnregisterBody(JPH::BodyID id) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Bodies.erase(id.GetIndexAndSequenceNumber());
        }

        std::vector<Event> TakeEvents() {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<Event> taken;
            taken.swap(m_Events);
            return taken;
        }

        void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold&, JPH::ContactSettings&) override {
            Push(body1.GetID(), body2.GetID(), true);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
            Push(pair.GetBody1ID(), pair.GetBody2ID(), false);
        }

    private:
        struct BodyInfo {
            uint32_t Entity = 0;
            bool IsSensor = false;
        };

        void Push(JPH::BodyID id1, JPH::BodyID id2, bool added) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto a = m_Bodies.find(id1.GetIndexAndSequenceNumber());
            auto b = m_Bodies.find(id2.GetIndexAndSequenceNumber());
            if (a == m_Bodies.end() || b == m_Bodies.end())
                return; // corpo ja destruido/desconhecido
            Event event;
            event.EntityA = a->second.Entity;
            event.EntityB = b->second.Entity;
            event.Added = added;
            event.Trigger = a->second.IsSensor || b->second.IsSensor;
            m_Events.push_back(event);
        }

        std::mutex m_Mutex;
        std::unordered_map<uint32_t, BodyInfo> m_Bodies; // BodyID -> entidade
        std::vector<Event> m_Events;
    };

    // --- SceneState: construtor/destrutor/move definidos AQUI (nao no .h) ---
    // Ver o comentario extenso em PhysicsEngine.h, dentro da struct
    // SceneState, para o motivo completo: unique_ptr<JPH::PhysicsSystem>
    // (e dos outros dois) exige tipo COMPLETO no ponto onde o
    // destrutor/move e gerado - JPH::PhysicsSystem so e forward-declarado
    // no .h, entao "= default" so pode viver aqui, depois de
    // <Jolt/Physics/PhysicsSystem.h> (e os outros headers do Jolt) terem
    // sido incluidos de verdade neste arquivo.
    // Estado de juntas e personagens de UMA cena (fora do header para nao expor tipos do Jolt).
    struct PhysicsEngine::Extras {
        struct JointRecord {
            JPH::Ref<JPH::Constraint> Constraint;
            uint32_t EntityA = 0;
            uint32_t EntityB = 0xFFFFFFFFu; // 0xFFFFFFFF = mundo
        };
        struct CharacterRecord {
            JPH::Ref<JPH::CharacterVirtual> Character;
            glm::vec3 DesiredVelocity{ 0.0f }; // horizontal (m/s); persiste ate o proximo Move
            float JumpSpeed = 0.0f;            // pedido de pulo pendente (consumido no proximo passo)
            glm::vec3 LastSynced{ 0.0f };      // ultima posicao escrita no Transform (detecta teleporte por script)
            uint32_t Layer = 0;
        };
        std::vector<JointRecord> Joints;
        std::unordered_map<uint32_t, CharacterRecord> Characters;
        JPH::Ref<JPH::GroupFilterTable> JointGroups;                  // pares ligados que NAO colidem
        std::unordered_map<uint32_t, JPH::CollisionGroup::SubGroupID> SubGroups; // entidade -> subgrupo
    };

    PhysicsEngine::SceneState::SceneState() = default;
    PhysicsEngine::SceneState::~SceneState() = default;
    PhysicsEngine::SceneState::SceneState(SceneState&&) noexcept = default;
    PhysicsEngine::SceneState& PhysicsEngine::SceneState::operator=(SceneState&&) noexcept = default;

    // --- Mapa global de estado por-Scene (definido aqui pelo mesmo motivo) ---
    // Precisa ser uma funcao (nao um "inline static" no .h) porque
    // std::unordered_map<Scene*, SceneState> exige SceneState completo em
    // qualquer arquivo que instancie o map - se ficasse "inline static" no
    // .h, TODO arquivo que inclui PhysicsEngine.h precisaria enxergar os
    // tipos completos do Jolt tambem (exatamente o vazamento que o .h evita
    // de proposito). static local dentro da funcao = inicializado na
    // primeira chamada, vive pelo resto do processo (mesmo padrao de
    // GetBroadPhaseLayerInterface() etc acima).
    std::unordered_map<Scene*, PhysicsEngine::SceneState>& PhysicsEngine::GetStates() {
        static std::unordered_map<Scene*, SceneState> s_States;
        return s_States;
    }

    static JPH::EMotionType ToJoltMotionType(BodyType type) {
        switch (type) {
            case BodyType::Static:    return JPH::EMotionType::Static;
            case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
            case BodyType::Dynamic:   return JPH::EMotionType::Dynamic;
        }
        return JPH::EMotionType::Static;
    }

    static JPH::ObjectLayer ToObjectLayer(BodyType type, int userLayer) {
        return Physics::MakeObjectLayer(userLayer, type != BodyType::Static);
    }

    PhysicsEngine::SceneState* PhysicsEngine::GetState(Scene& scene) {
        auto it = GetStates().find(&scene);
        if (it == GetStates().end())
            return nullptr;
        return &it->second;
    }

    void PhysicsEngine::OnSceneStart(Scene& scene) {
        if (GetState(scene) != nullptr) {
            PRISM_CORE_ERROR("PhysicsEngine::OnSceneStart chamado para uma Scene que ja tinha um mundo fisico ativo - ignorado.");
            return;
        }

        EnsureGlobalInit();

        // Matriz de colisao do projeto (copia: muda so ao iniciar um Play).
        if (auto project = Project::GetActive())
            s_ActiveMatrix = project->GetPhysicsMatrix();
        else
            s_ActiveMatrix = Physics::LayerMatrix();

        SceneState state;
        state.Ext = std::make_unique<Extras>();

        // TempAllocator: bloco de memoria temporaria usado pelo Jolt
        // DURANTE um PhysicsSystem::Update() (alocacoes de curta duracao
        // do solver/broad-phase, liberadas ao fim do mesmo Update()) -
        // 10 MB e o valor usado no HelloWorld.cpp oficial do Jolt e mais
        // que suficiente para o volume de corpos que esta engine lida
        // hoje; aumentar so seria necessario com milhares de corpos
        // ativos simultaneos.
        state.TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        // JobSystem: Jolt paraleliza o solver entre threads via um pool
        // de jobs proprio (nao usa std::async/std::thread diretamente no
        // seu core) - JobSystemThreadPool e a implementacao pronta que a
        // propria lib fornece para isso (equivalente ao que o Jolt
        // Samples/HelloWorld usa). std::thread::hardware_concurrency()-1
        // deixa uma thread livre para o resto da engine (main
        // thread/render) - o "-1" com clamp a 1 evita 0 ou negativo em
        // maquinas de apenas 1 core relatado.
        uint32_t numThreads = std::max(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
        state.JobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, (int)numThreads);

        state.PhysicsSystem = std::make_unique<JPH::PhysicsSystem>();

        // Limites de quantos corpos/pares de corpo/contatos o Jolt
        // pre-aloca - valores do exemplo oficial HelloWorld.cpp. Se isto
        // virar um gargalo real, estes numeros sao o primeiro lugar a
        // revisar.
        constexpr JPH::uint kMaxBodies = 4096;
        constexpr JPH::uint kNumBodyMutexes = 0; // 0 = Jolt escolhe um default razoavel
        constexpr JPH::uint kMaxBodyPairs = 4096;
        constexpr JPH::uint kMaxContactConstraints = 2048;

        state.PhysicsSystem->Init(
            kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
            GetBroadPhaseLayerInterface(), GetObjectVsBroadPhaseLayerFilter(), GetObjectLayerPairFilter());

        // Convencao +Y para cima, mesma usada pelo resto da engine
        // (camera, TransformComponent, etc).
        state.PhysicsSystem->SetGravity(JPH::Vec3(0.0f, -10.0f, 0.0f));

        // Eventos de colisao/trigger para os scripts (ver ContactCollector).
        state.Contacts = std::make_unique<ContactCollector>();
        state.PhysicsSystem->SetContactListener(state.Contacts.get());

        GetStates()[&scene] = std::move(state);

        // Cria um corpo fisico para toda entidade elegivel (ver
        // CreateBodyForEntity para os criterios) ja existente na Scene no
        // momento em que o Play comeca.
        auto view = scene.GetRegistry().view<RigidBodyComponent, ColliderComponent>();
        for (auto entityHandle : view)
            CreateBodyForEntity(scene, Entity(entityHandle, &scene));

        // Personagens e juntas DEPOIS de todos os corpos (uma junta liga dois corpos que ja existem).
        for (auto entityHandle : scene.GetRegistry().view<CharacterControllerComponent>())
            CreateCharacterForEntity(scene, Entity(entityHandle, &scene));
        for (auto entityHandle : scene.GetRegistry().view<JointComponent>())
            CreateJointForEntity(scene, Entity(entityHandle, &scene));
    }

    void PhysicsEngine::OnSceneStop(Scene& scene) {
        SceneState* state = GetState(scene);
        if (!state)
            return; // idempotente - chamar sem um mundo ativo nao faz nada

        // O Jolt exige que cada corpo seja explicitamente removido da
        // broad-phase (RemoveBody) e destruido (DestroyBody) antes do
        // PhysicsSystem em si ser liberado - ver Jolt docs "Body
        // Lifetime". Iteramos EntityToBody para isso.
        // Juntas e personagens saem ANTES dos corpos (o Jolt exige remover a junta antes de destruir os corpos).
        if (state->Ext) {
            for (auto& joint : state->Ext->Joints)
                state->PhysicsSystem->RemoveConstraint(joint.Constraint);
            state->Ext->Joints.clear();
            state->Ext->Characters.clear(); // o destrutor do CharacterVirtual remove o corpo interno
        }

        JPH::BodyInterface& bodyInterface = state->PhysicsSystem->GetBodyInterface();
        for (auto& [entityHandle, bodyId] : state->EntityToBody) {
            bodyInterface.RemoveBody(bodyId);
            bodyInterface.DestroyBody(bodyId);
        }

        GetStates().erase(&scene);
    }

    void PhysicsEngine::CreateBodyForEntity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state) {
            // Chamado fora do modo Play (ex: editor ajustando um Collider
            // antes de apertar Play) - nao ha mundo fisico para criar o
            // corpo ainda; ele sera criado quando OnSceneStart rodar.
            return;
        }

        if (!entity.HasComponent<RigidBodyComponent>() || !entity.HasComponent<ColliderComponent>())
            return; // ver comentario no .h: precisa dos DOIS components

        if (entity.HasComponent<CharacterControllerComponent>()) {
            PRISM_CORE_WARN("PhysicsEngine: a entidade '", entity.GetComponent<TagComponent>().Tag,
                             "' tem Character Controller E Rigid Body - o personagem vence, o Rigid Body e ignorado.");
            return;
        }

        // Recarregar: se ja existia um corpo para esta entidade (ex:
        // CreateBodyForEntity chamado de novo apos o editor mudar o
        // Collider com o Play ja rodando), destroi o antigo primeiro.
        DestroyBodyForEntity(scene, entity);

        auto& rigidBody = entity.GetComponent<RigidBodyComponent>();
        auto& collider = entity.GetComponent<ColliderComponent>();

        // Usamos a transform de MUNDO (ancestrais inclusos - ver
        // Scene::GetWorldTransform, parenting) para a posicao/rotacao
        // inicial do corpo Jolt, nao so o TransformComponent local -
        // senao um filho de um pai deslocado nasceria fisicamente na
        // posicao ERRADA (a posicao local, nao a posicao real dele na
        // cena). Depois de criado, o corpo fisico e independente do
        // parenting (Jolt nao sabe de RelationshipComponent) - ver
        // docs/fisica.md, "Limitacoes conhecidas".
        glm::mat4 worldMatrix = scene.GetWorldTransform(entity);
        glm::vec3 worldPosition = glm::vec3(worldMatrix[3]);

        // Extrair a rotacao de uma matriz de mundo que ainda tem ESCALA
        // embutida nas colunas (Translation * Rotation * Scale) produz um
        // quaternion distorcido a menos que a escala seja exatamente
        // {1,1,1}. Por isso normalizamos as 3 colunas de rotacao ANTES de
        // extrair o quaternion (normalizar o quaternion resultante depois
        // so corrige a magnitude, nao o eixo de rotacao ja errado). O
        // Jolt valida quaternions de entrada via seus proprios asserts
        // internos (USE_ASSERTS, ver vendor/CMakeLists.txt) e derruba o
        // processo se receber um quaternion distorcido.
        glm::vec3 col0 = glm::vec3(worldMatrix[0]);
        glm::vec3 col1 = glm::vec3(worldMatrix[1]);
        glm::vec3 col2 = glm::vec3(worldMatrix[2]);
        glm::mat3 rotationOnly(
            glm::length(col0) > 0.00001f ? col0 / glm::length(col0) : glm::vec3(1, 0, 0),
            glm::length(col1) > 0.00001f ? col1 / glm::length(col1) : glm::vec3(0, 1, 0),
            glm::length(col2) > 0.00001f ? col2 / glm::length(col2) : glm::vec3(0, 0, 1)
        );
        glm::quat worldRotation = glm::normalize(glm::quat_cast(rotationOnly));

        // Monta o Shape primeiro (o Jolt separa "shape" de "body creation
        // settings" - o shape e uma referencia contada (JPH::RefConst) que pode, inclusive, ser
        // compartilhada entre varios corpos identicos; nao fazemos isso
        // aqui ainda, cada entidade tem seu proprio Shape, mas a API ja
        // permite otimizar isso no futuro se necessario).
        //
        // Tamanho minimo: o Jolt tem asserts internos (USE_ASSERTS, ver
        // vendor/CMakeLists.txt - habilitados em builds Debug) que
        // ENCERRAM O PROCESSO se uma dimensao de shape for pequena
        // demais. Qualquer dimensao abaixo de kMinShapeDimension e PRESA
        // (clamped) a esse minimo antes de chegar no Jolt, com um aviso
        // no log. So a FISICA impoe esse minimo; o mesh visual pode ficar
        // arbitrariamente pequeno.
        //
        // O valor 0.05 bate com JPH::cDefaultConvexRadius: BoxShape/
        // CapsuleShape/SphereShape arredondam levemente os cantos com um
        // "convex radius", e o Jolt calcula
        // mConvexRadius = min(convexRadius, halfExtent.ReduceMin()). Uma
        // dimensao menor que o raio convexo deixa a shape degenerada
        // (ex: um piso com half-extents (5, 0.01, 5)) e o Jolt entra em
        // caminhos instaveis/assertivos. O ConvexRadius EXPLICITO abaixo
        // (kSafeConvexRadius) garante que nunca excede metade da menor
        // dimensao.
        constexpr float kMinShapeDimension = 0.05f;
        glm::vec3 safeSize = collider.Size;
        bool sizeWasClamped = false;
        for (int axis = 0; axis < 3; axis++) {
            if (safeSize[axis] < kMinShapeDimension) {
                safeSize[axis] = kMinShapeDimension;
                sizeWasClamped = true;
            }
        }
        if (sizeWasClamped) {
            PRISM_CORE_WARN("PhysicsEngine: ColliderComponent::Size da entidade '", entity.GetComponent<TagComponent>().Tag,
                             "' tem dimensao menor que ", kMinShapeDimension, " - presa a esse minimo para a fisica (o Jolt nao aceita shapes proximas de zero/degeneradas). O visual (mesh) nao e afetado por este limite.");
        }

        // ConvexRadius explicito (nao o default do Jolt, cDefaultConvexRadius
        // = 0.05) para BoxShape - nunca maior que METADE da menor
        // dimensao de safeSize, o que evita o caso degenerado descrito
        // acima mesmo para caixas legitimamente finas (ex: um "papel"
        // fino de 0.05 a 0.1 de espessura, que o usuario pode querer de
        // verdade, nao so um erro de digitacao) sem precisar prender a
        // espessura num valor maior so por causa do raio convexo.
        float safeConvexRadius = glm::min(0.05f, glm::min(safeSize.x, glm::min(safeSize.y, safeSize.z)) * 0.5f);

        // ConvexHull/TriangleMesh: a geometria vem do MeshRendererComponent da
        // mesma entidade (ver ColliderShape, Components.h), com a escala de
        // MUNDO aplicada aos vertices (diferente de Size, que e absoluto).
        // Qualquer falha (sem malha, malha degenerada, Jolt recusando a
        // forma) cai para uma Box a partir dos bounds - ou, sem malha
        // nenhuma, para a Box de Size - com aviso no log, para o corpo
        // existir em vez de quebrar o Play.
        ColliderShape effectiveShape = collider.Shape;
        const bool isMeshBased = (effectiveShape == ColliderShape::ConvexHull || effectiveShape == ColliderShape::TriangleMesh);
        // Fonte da geometria: LevelMeshComponent (Level Builder; triangulado na hora) ou MeshRenderer.
        std::vector<glm::vec3> levelPositions;
        std::vector<uint32_t> levelIndices;
        const std::vector<glm::vec3>* sourcePositions = nullptr;
        const std::vector<uint32_t>* sourceIndices = nullptr;
        glm::vec3 sourceBoundsMin(0.0f), sourceBoundsMax(0.0f);
        glm::vec3 worldScale(glm::length(col0), glm::length(col1), glm::length(col2));
        if (isMeshBased) {
            if (entity.HasComponent<LevelMeshComponent>()) {
                for (const Level::BuiltSubmesh& built : entity.GetComponent<LevelMeshComponent>().Mesh.Build()) {
                    const uint32_t baseVertex = (uint32_t)levelPositions.size();
                    for (const Level::BuiltVertex& v : built.Vertices)
                        levelPositions.push_back(v.Position);
                    for (uint32_t index : built.Indices)
                        levelIndices.push_back(baseVertex + index);
                }
                if (!levelPositions.empty()) {
                    sourcePositions = &levelPositions;
                    sourceIndices = &levelIndices;
                    sourceBoundsMin = sourceBoundsMax = levelPositions.front();
                    for (const glm::vec3& p : levelPositions) {
                        sourceBoundsMin = glm::min(sourceBoundsMin, p);
                        sourceBoundsMax = glm::max(sourceBoundsMax, p);
                    }
                }
            }
            else if (entity.HasComponent<MeshRendererComponent>()) {
                if (const Mesh* sourceMesh = Renderer::ResolveMesh(entity.GetComponent<MeshRendererComponent>())) {
                    sourcePositions = &(*sourcePositions);
                    sourceIndices = &(*sourceIndices);
                    sourceBoundsMin = sourceMesh->GetLocalBoundsMin();
                    sourceBoundsMax = sourceMesh->GetLocalBoundsMax();
                }
            }
            if (!sourcePositions || sourcePositions->empty()) {
                PRISM_CORE_WARN("PhysicsEngine: Collider ConvexHull/TriangleMesh da entidade '", entity.GetComponent<TagComponent>().Tag,
                                 "' precisa de um MeshRenderer (ou LevelMesh) com malha valida - usando Box de Size.");
                effectiveShape = ColliderShape::Box;
            }
            else if (effectiveShape == ColliderShape::TriangleMesh && rigidBody.Type == BodyType::Dynamic) {
                // Jolt: MeshShape nao suporta corpos Dynamic (nao tem volume
                // para calcular massa/inercia) e dispara assert.
                PRISM_CORE_WARN("PhysicsEngine: TriangleMesh nao e permitido em Rigid Body Dynamic (entidade '", entity.GetComponent<TagComponent>().Tag,
                                 "') - usando ConvexHull. Use Static/Kinematic para a malha exata.");
                effectiveShape = ColliderShape::ConvexHull;
            }
        }

        JPH::RefConst<JPH::Shape> shape;
        switch (effectiveShape) {
            case ColliderShape::Box: {
                // Collider::Size ja e meio-tamanho (half-extents) por
                // convencao da propria engine (ver comentario em
                // ColliderComponent::Size em Components.h) - bate
                // exatamente com o que JPH::BoxShape espera (tambem
                // recebe half-extents no construtor, ver Jolt docs
                // "BoxShape"). ConvexRadius explicito (ver
                // safeConvexRadius acima) em vez do default do
                // construtor, para caixas finas nao ficarem degeneradas.
                shape = new JPH::BoxShape(ToJolt(safeSize), safeConvexRadius);
                break;
            }
            case ColliderShape::Sphere: {
                shape = new JPH::SphereShape(safeSize.x);
                break;
            }
            case ColliderShape::Capsule: {
                // Capsula alinhada ao eixo Y (convencao usual - "de pe").
                // JPH::CapsuleShape recebe (halfHeightOfCylinder, radius)
                // - a MESMA interpretacao de Size.y/Size.x que
                // ColliderComponent::Size ja documenta (Size.y = altura
                // TOTAL da parte cilindrica, sem contar as tampas
                // hemisfericas - por isso dividimos por 2 aqui).
                float halfHeight = safeSize.y * 0.5f;
                shape = new JPH::CapsuleShape(halfHeight, safeSize.x);
                break;
            }
            case ColliderShape::ConvexHull: {
                JPH::Array<JPH::Vec3> points;
                points.reserve((*sourcePositions).size());
                for (const glm::vec3& p : (*sourcePositions))
                    points.push_back(ToJolt(p * worldScale));

                JPH::ConvexHullShapeSettings hullSettings(points);
                JPH::Shape::ShapeResult result = hullSettings.Create();
                if (result.HasError()) {
                    PRISM_CORE_WARN("PhysicsEngine: Jolt nao conseguiu criar o ConvexHull da entidade '", entity.GetComponent<TagComponent>().Tag,
                                     "' (", result.GetError().c_str(), ") - usando Box dos bounds da malha.");
                    glm::vec3 half = glm::max((sourceBoundsMax - sourceBoundsMin) * 0.5f * worldScale, glm::vec3(kMinShapeDimension));
                    shape = new JPH::BoxShape(ToJolt(half), glm::min(0.05f, glm::min(half.x, glm::min(half.y, half.z)) * 0.5f));
                }
                else {
                    shape = result.Get();
                }
                break;
            }
            case ColliderShape::TriangleMesh: {
                const auto& positions = (*sourcePositions);
                const auto& indices = (*sourceIndices);
                JPH::TriangleList triangles;
                triangles.reserve(indices.size() / 3);
                for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                    glm::vec3 a = positions[indices[i]] * worldScale;
                    glm::vec3 b = positions[indices[i + 1]] * worldScale;
                    glm::vec3 c = positions[indices[i + 2]] * worldScale;
                    triangles.push_back(JPH::Triangle(JPH::Float3(a.x, a.y, a.z), JPH::Float3(b.x, b.y, b.z), JPH::Float3(c.x, c.y, c.z)));
                }

                JPH::MeshShapeSettings meshSettings(triangles);
                JPH::Shape::ShapeResult result = meshSettings.Create();
                if (result.HasError()) {
                    PRISM_CORE_WARN("PhysicsEngine: Jolt nao conseguiu criar o TriangleMesh da entidade '", entity.GetComponent<TagComponent>().Tag,
                                     "' (", result.GetError().c_str(), ") - usando Box dos bounds da malha.");
                    glm::vec3 half = glm::max((sourceBoundsMax - sourceBoundsMin) * 0.5f * worldScale, glm::vec3(kMinShapeDimension));
                    shape = new JPH::BoxShape(ToJolt(half), glm::min(0.05f, glm::min(half.x, glm::min(half.y, half.z)) * 0.5f));
                }
                else {
                    shape = result.Get();
                }
                break;
            }
        }

        JPH::BodyCreationSettings bodyCreationSettings(
            shape,
            ToJolt(worldPosition),
            ToJolt(worldRotation),
            ToJoltMotionType(rigidBody.Type),
            ToObjectLayer(rigidBody.Type, collider.Layer));

        bodyCreationSettings.mGravityFactor = rigidBody.UseGravity ? 1.0f : 0.0f;

        // motionQuality LinearCast ativa CCD (continuous collision
        // detection) para este corpo, evitando que objetos rapidos
        // atravessem paredes finas num unico step.
        bodyCreationSettings.mMotionQuality = rigidBody.ContinuousCollisionDetection
            ? JPH::EMotionQuality::LinearCast
            : JPH::EMotionQuality::Discrete;

        // Trava as 3 rotacoes fisicas do corpo - essencial para qualquer
        // entidade cuja rotacao e controlada por script em vez de fisica
        // (camera FPS/TPS, corpo do player): sem isto, esbarrar em algo
        // ou cair de uma pequena altura aplica torque ao corpo, e
        // Simulate() sincroniza essa rotacao "acidental" de volta para
        // TransformComponent, brigando visualmente com o script. O Jolt
        // expoe isto via BodyCreationSettings::mAllowedDOFs (Degrees Of
        // Freedom): removemos os 3 bits de rotacao (RotationX/Y/Z) do
        // conjunto default (All), mantendo os 3 de translacao livres - a
        // translacao (cair, ser empurrado, colidir) continua normal, so a
        // rotacao fisica fica congelada.
        if (rigidBody.FixedRotation) {
            bodyCreationSettings.mAllowedDOFs = static_cast<JPH::EAllowedDOFs>(
                static_cast<uint32_t>(JPH::EAllowedDOFs::TranslationX) |
                static_cast<uint32_t>(JPH::EAllowedDOFs::TranslationY) |
                static_cast<uint32_t>(JPH::EAllowedDOFs::TranslationZ));
        }

        // Trigger/sensor: detecta sobreposicao mas nao gera resposta
        // fisica (nao empurra nada).
        bodyCreationSettings.mIsSensor = collider.IsTrigger;

        // Atrito e restitution (elasticidade) do material - Jolt combina
        // os dois lados de um contato automaticamente (default:
        // sqrt(a*b) para friction, max(a,b) para restitution - ver
        // comentario em RigidBodyComponent::Friction/Restitution,
        // Components.h). Relevante para qualquer BodyType (uma rampa
        // Static com atrito baixo ainda afeta corpos Dynamic que
        // deslizam nela), nao so Dynamic.
        bodyCreationSettings.mFriction = rigidBody.Friction;
        bodyCreationSettings.mRestitution = rigidBody.Restitution;

        // Damping (arrasto/resistencia simulado a cada step, independente
        // de contato - ver comentario grande em
        // RigidBodyComponent::LinearDamping/AngularDamping, Components.h).
        // Jolt so integra isto para corpos Dynamic de qualquer forma
        // (Static/Kinematic nao sao afetados mesmo com o campo setado),
        // entao nao ha necessidade de condicionar isto a rigidBody.Type
        // aqui.
        bodyCreationSettings.mLinearDamping = rigidBody.LinearDamping;
        bodyCreationSettings.mAngularDamping = rigidBody.AngularDamping;

        // Massa customizada: por padrao (EOverrideMassProperties::
        // CalculateMassAndInertia) o Jolt calcularia massa E inercia
        // sozinho a partir do Shape * uma densidade generica fixa,
        // ignorando RigidBodyComponent::Mass. CalculateInertia pede pro
        // Jolt calcular a INERCIA a partir da forma, mas usar a MASSA
        // exata definida na Properties panel. So faz sentido para Dynamic
        // (o Jolt ignora MassPropertiesOverride em Static/Kinematic).
        // Mass minimo de 0.001f evita massa zero/negativa travando o
        // solver.
        if (rigidBody.Type == BodyType::Dynamic) {
            bodyCreationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bodyCreationSettings.mMassPropertiesOverride.mMass = std::max(rigidBody.Mass, 0.001f);
        }

        JPH::BodyInterface& bodyInterface = state->PhysicsSystem->GetBodyInterface();
        JPH::Body* body = bodyInterface.CreateBody(bodyCreationSettings);

        // user data de 64 bits do proprio Jolt - armazena o entt::entity
        // dono para que eventos/queries do Jolt consigam voltar para "qual
        // entidade e essa" sem busca linear. entt::entity e tipicamente um
        // uint32_t por baixo - cabe num uint64_t.
        body->SetUserData(static_cast<JPH::uint64>(static_cast<uint32_t>(entity.GetHandle())));

        // EActivation::Activate para corpos que devem comecar acordados
        // (Dynamic caindo por gravidade, por exemplo) - Static nunca
        // "acorda" de qualquer forma, e nao ha problema em passar
        // Activate para ele tambem (Jolt ignora ativacao em corpos
        // Static internamente).
        bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);

        state->EntityToBody[(uint32_t)entity.GetHandle()] = body->GetID();
        if (state->Contacts)
            state->Contacts->RegisterBody(body->GetID(), (uint32_t)entity.GetHandle(), collider.IsTrigger);
    }

    void PhysicsEngine::DestroyBodyForEntity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state)
            return;

        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return;

        RemoveJointsReferencing(*state, (uint32_t)entity.GetHandle()); // uma junta nao pode sobreviver a um dos corpos

        // Jolt exige RemoveBody (tira da simulacao/broad-phase) ANTES de
        // DestroyBody (libera a memoria do corpo). Ver Jolt docs "Body
        // Lifetime".
        JPH::BodyInterface& bodyInterface = state->PhysicsSystem->GetBodyInterface();
        if (state->Contacts)
            state->Contacts->UnregisterBody(it->second);
        bodyInterface.RemoveBody(it->second);
        bodyInterface.DestroyBody(it->second);

        state->EntityToBody.erase(it);
    }

    float PhysicsEngine::GetFixedTimeStep() {
        return kFixedTimeStep;
    }

    void PhysicsEngine::Simulate(Scene& scene, float deltaTime, const std::function<void(float)>& onFixedStep) {
        SceneState* state = GetState(scene);
        if (!state)
            return;

        // Timestep FIXO via acumulador: o Jolt (como a maioria dos motores
        // de fisica) e projetado e testado com um passo constante -
        // alimentar deltaTime bruto e variavel direto no Update() degrada
        // estabilidade (colisoes perdidas em quedas de frame-rate,
        // jitter).
        state->Accumulator += deltaTime;
        int stepsThisFrame = 0;
        while (state->Accumulator >= kFixedTimeStep && stepsThisFrame < kMaxStepsPerFrame) {
            // PhysicsSystem::Update(deltaTime, collisionSteps, tempAllocator, jobSystem)
            // - collisionSteps=1 (kCollisionSteps) e o valor recomendado
            // pela doc oficial do Jolt quando ja se está chamando Update()
            // com um timestep fixo pequeno (1/60) via acumulador externo,
            // em vez de pedir para o proprio Jolt subdividir um
            // deltaTime maior internamente (que e para o que
            // collisionSteps > 1 serve).
            // Scripts (OnFixedUpdate) ANTES do passo: forcas e velocidades que eles
            // aplicam entram neste passo. Um script pode destruir/criar entidades
            // (adiado pelo ScriptEngine) mas nao o mundo fisico, entao 'state' segue valido.
            if (onFixedStep)
                onFixedStep(kFixedTimeStep);
            StepCharacters(scene, *state, kFixedTimeStep);
            state->PhysicsSystem->Update(kFixedTimeStep, kCollisionSteps, state->TempAllocator.get(), state->JobSystem.get());
            state->Accumulator -= kFixedTimeStep;
            stepsThisFrame++;
        }
        if (stepsThisFrame == kMaxStepsPerFrame)
            state->Accumulator = 0.0f; // descarta o resto acumulado - preferimos desacelerar a travar ("espiral da morte")

        SyncCharacters(scene, *state);

        // --- Sincroniza corpos movidos de volta para TransformComponent ---
        // A API publica estavel do Jolt nao expoe uma lista de "corpos
        // movidos neste step" (existe um BodyActivationListener para
        // ativar/dormir, mas nao um "moved this step" direto). Por isso
        // iteramos EntityToBody inteiro e perguntamos GetMotionType a
        // cada corpo - so sincronizamos corpos NAO-estaticos (Static
        // nunca se move por definicao, e o TransformComponent dele dita a
        // posicao inicial, nunca o contrario). Se isso virar gargalo
        // medido, da pra otimizar guardando uma lista separada so de
        // corpos nao-estaticos em SceneState.
        JPH::BodyInterface& bodyInterface = state->PhysicsSystem->GetBodyInterface();
        for (auto& [entityHandleValue, bodyId] : state->EntityToBody) {
            if (bodyInterface.GetMotionType(bodyId) == JPH::EMotionType::Static)
                continue; // estatico nunca se move - nada a sincronizar

            entt::entity handle = (entt::entity)entityHandleValue;
            if (!scene.GetRegistry().valid(handle))
                continue; // entidade pode ter sido destruida no mesmo frame - ver DestroyEntity/ScriptEngine

            auto* transform = scene.GetRegistry().try_get<TransformComponent>(handle);
            if (!transform)
                continue;

            // Limitacao conhecida (ver docs/fisica.md): escrevemos
            // POSICAO/ROTACAO DE MUNDO direto no TransformComponent LOCAL
            // da entidade, ignorando qualquer ancestral
            // (RelationshipComponent). Isso e correto para entidades SEM
            // pai (o caso comum), mas produz resultado errado para uma
            // entidade fisica que tambem seja filha de outra na hierarquia
            // (o Jolt nao sabe que existe um pai). Corrigir isso exigiria
            // multiplicar pela inversa da transform do pai a cada
            // sincronizacao.
            transform->Translation = FromJolt(bodyInterface.GetPosition(bodyId));

            // Rotacao so e sincronizada de volta se o corpo NAO tiver
            // FixedRotation (ver RigidBodyComponent::FixedRotation,
            // Components.h): com rotacao travada, o quat do corpo nunca
            // muda de verdade, entao reconverter Euler->quat->Euler aqui
            // toda vez so arriscaria introduzir deriva/ambiguidade de
            // representacao sem nenhum ganho, competindo com um script que
            // esteja escrevendo transform->Rotation no mesmo frame.
            auto* rigidBody = scene.GetRegistry().try_get<RigidBodyComponent>(handle);
            if (!rigidBody || !rigidBody->FixedRotation)
                transform->Rotation = QuatToEulerDegrees(glm::normalize(FromJolt(bodyInterface.GetRotation(bodyId))));
        }

        // --- Eventos de colisao -> OnCollisionEnter/Exit, OnTriggerEnter/Exit ---
        // Enter so no PRIMEIRO contato do par de entidades e Exit no ULTIMO
        // (um par pode ter varios contatos de sub-shape ao mesmo tempo). Um
        // dos corpos ser sensor (Collider::IsTrigger) vira Trigger* em vez
        // de Collision*. Cada evento vai para os DOIS scripts do par.
        if (state->Contacts) {
            std::vector<ContactCollector::Event> events = state->Contacts->TakeEvents();
            for (const ContactCollector::Event& event : events) {
                uint32_t low = std::min(event.EntityA, event.EntityB);
                uint32_t high = std::max(event.EntityA, event.EntityB);
                uint64_t pairKey = ((uint64_t)low << 32) | (uint64_t)high;

                bool dispatch = false;
                if (event.Added) {
                    dispatch = (state->ContactCounts[pairKey]++ == 0);
                }
                else {
                    auto count = state->ContactCounts.find(pairKey);
                    if (count != state->ContactCounts.end() && --count->second <= 0) {
                        state->ContactCounts.erase(count);
                        dispatch = true;
                    }
                }
                if (!dispatch)
                    continue;

                // Os callbacks podem destruir entidades: revalida a cada chamada.
                auto& registry = scene.GetRegistry();
                entt::entity handleA = (entt::entity)event.EntityA;
                entt::entity handleB = (entt::entity)event.EntityB;
                if (registry.valid(handleA) && registry.valid(handleB))
                    ScriptEngine::DispatchContactEvent(Entity(handleA, &scene), Entity(handleB, &scene), event.Added, event.Trigger);
                if (registry.valid(handleA) && registry.valid(handleB))
                    ScriptEngine::DispatchContactEvent(Entity(handleB, &scene), Entity(handleA, &scene), event.Added, event.Trigger);
            }
        }
    }

    void PhysicsEngine::ApplyForce(Scene& scene, Entity entity, const glm::vec3& force) {
        SceneState* state = GetState(scene);
        if (!state)
            return;
        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return; // silencioso de proposito - ver comentario no .h (script chamando fora do Play, ou entidade sem fisica)
        state->PhysicsSystem->GetBodyInterface().AddForce(it->second, ToJolt(force));
    }

    void PhysicsEngine::ApplyLinearImpulse(Scene& scene, Entity entity, const glm::vec3& impulse) {
        SceneState* state = GetState(scene);
        if (!state)
            return;
        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return;
        state->PhysicsSystem->GetBodyInterface().AddImpulse(it->second, ToJolt(impulse));
    }

    glm::vec3 PhysicsEngine::GetLinearVelocity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state)
            return glm::vec3(0.0f);
        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return glm::vec3(0.0f);
        return FromJolt(state->PhysicsSystem->GetBodyInterface().GetLinearVelocity(it->second));
    }

    void PhysicsEngine::SetLinearVelocity(Scene& scene, Entity entity, const glm::vec3& velocity) {
        SceneState* state = GetState(scene);
        if (!state)
            return;
        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return;
        state->PhysicsSystem->GetBodyInterface().SetLinearVelocity(it->second, ToJolt(velocity));
    }

    RaycastHit PhysicsEngine::Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const std::vector<entt::entity>& ignoreEntities, uint32_t layerMask) {
        RaycastHit result; // Hit=false por padrao (ver RaycastHit, PhysicsEngine.h)

        SceneState* state = GetState(scene);
        if (!state)
            return result; // Scene nao esta rodando fisica (fora do modo Play) - ver GetState

        // direction pode chegar nao-normalizada (ver comentario no .h) -
        // JPH::RRayCast espera Direction = vetor deslocamento COMPLETO do
        // raio (origin -> origin + Direction), por isso normalizamos e
        // multiplicamos por maxDistance aqui, em vez de passar direction
        // crua.
        float lengthSq = glm::dot(direction, direction);
        if (lengthSq < 0.0000001f) // direcao (quase) zero - nao ha raio nenhum para lancar
            return result;
        glm::vec3 normalizedDirection = direction / sqrtf(lengthSq);
        glm::vec3 translation = normalizedDirection * maxDistance;

        JPH::RRayCast ray(ToJolt(origin), ToJolt(translation));

        // NarrowPhaseQuery::CastRay (variante simples, "closest hit").
        // Sem filtro fino por camada/mascara: os parametros de filtro de
        // CastRay tem default vazio ({}), que aceita qualquer
        // BroadPhaseLayer/ObjectLayer.
        JPH::RayCastResult rayResult;
        //
        // 'ignoreEntities' (ver comentario no .h) vira um BodyFilter via
        // JPH::IgnoreMultipleBodiesFilter - classe utilitaria que o
        // proprio Jolt fornece exatamente para "aceita qualquer corpo,
        // EXCETO esta lista de BodyIDs" (ver Jolt/Physics/Body/
        // BodyFilter.h e exemplos oficiais do Jolt, Samples/RayCast) -
        // preferido a escrever uma subclasse de JPH::BodyFilter do zero.
        // BodyIDs resolvidos via SceneState::EntityToBody (mesmo mapa que
        // CreateBodyForEntity preenche) - entidades sem corpo fisico
        // ativo (SEM RigidBody+Collider, ou fora do modo Play - mas aqui
        // ja sabemos que esta rodando, ver GetState acima) sao
        // simplesmente omitidas da lista, sem erro.
        JPH::IgnoreMultipleBodiesFilter ignoreFilter;
        for (entt::entity ignored : ignoreEntities) {
            JPH::BodyID ignoredId = FindBodyID(*state, (uint32_t)ignored);
            if (!ignoredId.IsInvalid())
                ignoreFilter.IgnoreBody(ignoredId);
        }

        PrismLayerMaskObjectFilter layerFilter(layerMask); // so camadas de fisica na mascara
        bool hit = state->PhysicsSystem->GetNarrowPhaseQuery().CastRay(ray, rayResult, {}, layerFilter, ignoreFilter);

        if (!hit)
            return result; // Hit=false - nao acertou nada dentro de maxDistance

        result.Hit = true;
        result.Distance = rayResult.mFraction * maxDistance; // mFraction e 0..1 do comprimento total do raio (translation)
        result.Point = origin + normalizedDirection * result.Distance;

        JPH::BodyInterface& bodyInterface = state->PhysicsSystem->GetBodyInterface();

        // Normal da superficie no ponto de impacto - GetWorldSpaceSurfaceNormal
        // exige o ponto de impacto em espaco local do shape, entao
        // pedimos ao proprio corpo pra traduzir (mesmo padrao usado nos
        // exemplos oficiais do Jolt para raycasts, ver Samples/RayCast).
        {
            JPH::BodyLockRead lock(state->PhysicsSystem->GetBodyLockInterface(), rayResult.mBodyID);
            if (lock.Succeeded()) {
                const JPH::Body& hitBody = lock.GetBody();
                JPH::Vec3 worldPoint = ray.GetPointOnRay(rayResult.mFraction);
                JPH::Vec3 normal = hitBody.GetWorldSpaceSurfaceNormal(rayResult.mSubShapeID2, worldPoint);
                result.Normal = FromJolt(normal);
            }
        }

        // Traduz o BodyID atingido de volta para a entt::entity dona -
        // mesmo mecanismo de user data que CreateBodyForEntity grava no
        // corpo (ver comentario la e em SceneState::EntityToBody).
        JPH::uint64 userData = bodyInterface.GetUserData(rayResult.mBodyID);
        result.Entity = (entt::entity)(uint32_t)userData;

        return result;
    }

    // ========================================================================
    // Auxiliares, juntas e personagens
    // ========================================================================

    JPH::BodyID PhysicsEngine::FindBodyID(SceneState& state, uint32_t entity) {
        auto it = state.EntityToBody.find(entity);
        if (it != state.EntityToBody.end())
            return it->second;
        if (state.Ext) {
            auto character = state.Ext->Characters.find(entity);
            if (character != state.Ext->Characters.end() && character->second.Character)
                return character->second.Character->GetInnerBodyID();
        }
        return JPH::BodyID(); // invalido
    }

    void PhysicsEngine::RemoveJointsReferencing(SceneState& state, uint32_t entity) {
        if (!state.Ext)
            return;
        auto& joints = state.Ext->Joints;
        for (size_t i = joints.size(); i-- > 0;) {
            if (joints[i].EntityA == entity || joints[i].EntityB == entity) {
                state.PhysicsSystem->RemoveConstraint(joints[i].Constraint);
                joints.erase(joints.begin() + (std::ptrdiff_t)i);
            }
        }
    }

    void PhysicsEngine::DestroyJointForEntity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state || !state->Ext)
            return;
        auto& joints = state->Ext->Joints;
        const uint32_t id = (uint32_t)entity.GetHandle();
        for (size_t i = joints.size(); i-- > 0;) {
            if (joints[i].EntityA == id) { // so a junta que ESTA entidade declara (as de outras que a citam continuam)
                state->PhysicsSystem->RemoveConstraint(joints[i].Constraint);
                joints.erase(joints.begin() + (std::ptrdiff_t)i);
            }
        }
    }

    void PhysicsEngine::CreateJointForEntity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state || !state->Ext || !entity.HasComponent<JointComponent>())
            return;
        DestroyJointForEntity(scene, entity);

        const JointComponent& joint = entity.GetComponent<JointComponent>();
        const std::string& name = entity.GetComponent<TagComponent>().Tag;
        const uint32_t idA = (uint32_t)entity.GetHandle();

        auto bodyA = state->EntityToBody.find(idA);
        if (bodyA == state->EntityToBody.end()) {
            PRISM_CORE_WARN("PhysicsEngine: a junta da entidade '", name, "' precisa de Rigid Body + Collider na mesma entidade - ignorada.");
            return;
        }

        // Outra ponta: primeira entidade com o nome pedido que tem corpo; vazio = mundo.
        uint32_t idB = 0xFFFFFFFFu;
        JPH::BodyID bodyBId;
        if (!joint.ConnectedEntity.empty()) {
            for (auto handle : scene.GetRegistry().view<TagComponent>()) {
                if ((uint32_t)handle == idA || scene.GetRegistry().get<TagComponent>(handle).Tag != joint.ConnectedEntity)
                    continue;
                auto found = state->EntityToBody.find((uint32_t)handle);
                if (found != state->EntityToBody.end()) {
                    idB = (uint32_t)handle;
                    bodyBId = found->second;
                    break;
                }
            }
            if (idB == 0xFFFFFFFFu) {
                PRISM_CORE_WARN("PhysicsEngine: a junta de '", name, "' aponta para '", joint.ConnectedEntity,
                                 "', mas nenhuma entidade com esse nome tem Rigid Body + Collider - ignorada.");
                return;
            }
        }

        const JPH::BodyLockInterface& locks = state->PhysicsSystem->GetBodyLockInterfaceNoLock();
        JPH::Body* body1 = locks.TryGetBody(bodyA->second);
        JPH::Body* body2 = bodyBId.IsInvalid() ? &JPH::Body::sFixedToWorld : locks.TryGetBody(bodyBId);
        if (!body1 || !body2)
            return;

        // Ponto e eixo em espaco de MUNDO, na pose atual (= pose inicial do Play).
        const glm::mat4 world = scene.GetWorldTransform(entity);
        const glm::vec3 anchor = glm::vec3(world * glm::vec4(joint.Anchor, 1.0f));
        glm::vec3 axis = glm::vec3(world * glm::vec4(joint.Axis, 0.0f));
        axis = (glm::dot(axis, axis) > 1e-8f) ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        const JPH::RVec3 point = ToJolt(anchor);
        const JPH::Vec3 jaxis = ToJolt(axis);
        const JPH::Vec3 normal = jaxis.GetNormalizedPerpendicular();

        JPH::TwoBodyConstraint* constraint = nullptr;
        switch (joint.Type) {
            case JointType::Fixed: {
                JPH::FixedConstraintSettings settings;
                settings.mSpace = JPH::EConstraintSpace::WorldSpace;
                settings.mAutoDetectPoint = true;
                constraint = settings.Create(*body1, *body2);
                break;
            }
            case JointType::Point: {
                JPH::PointConstraintSettings settings;
                settings.mSpace = JPH::EConstraintSpace::WorldSpace;
                settings.mPoint1 = settings.mPoint2 = point;
                constraint = settings.Create(*body1, *body2);
                break;
            }
            case JointType::Hinge: {
                JPH::HingeConstraintSettings settings;
                settings.mSpace = JPH::EConstraintSpace::WorldSpace;
                settings.mPoint1 = settings.mPoint2 = point;
                settings.mHingeAxis1 = settings.mHingeAxis2 = jaxis;
                settings.mNormalAxis1 = settings.mNormalAxis2 = normal;
                if (joint.UseLimits) { // o Jolt exige min <= 0 <= max, em radianos
                    settings.mLimitsMin = JPH::DegreesToRadians(std::min(std::max(joint.LimitMin, -180.0f), 0.0f));
                    settings.mLimitsMax = JPH::DegreesToRadians(std::min(std::max(joint.LimitMax, 0.0f), 180.0f));
                }
                constraint = settings.Create(*body1, *body2);
                break;
            }
            case JointType::Slider: {
                JPH::SliderConstraintSettings settings;
                settings.mSpace = JPH::EConstraintSpace::WorldSpace;
                settings.mAutoDetectPoint = true;
                settings.SetSliderAxis(jaxis);
                if (joint.UseLimits) {
                    settings.mLimitsMin = std::min(joint.LimitMin, 0.0f);
                    settings.mLimitsMax = std::max(joint.LimitMax, 0.0f);
                }
                constraint = settings.Create(*body1, *body2);
                break;
            }
            case JointType::Distance: {
                JPH::DistanceConstraintSettings settings;
                settings.mSpace = JPH::EConstraintSpace::WorldSpace;
                settings.mPoint1 = settings.mPoint2 = point;
                // mMin/mMax < 0 = a distancia atual (haste rigida ou mola em torno dela).
                if (joint.UseLimits) {
                    settings.mMinDistance = std::max(joint.LimitMin, 0.0f);
                    settings.mMaxDistance = std::max(joint.LimitMax, settings.mMinDistance);
                }
                if (joint.UseSpring) {
                    settings.mLimitsSpringSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
                    settings.mLimitsSpringSettings.mFrequency = std::max(joint.SpringFrequency, 0.01f);
                    settings.mLimitsSpringSettings.mDamping = std::max(joint.SpringDamping, 0.0f);
                }
                constraint = settings.Create(*body1, *body2);
                break;
            }
        }
        if (!constraint) {
            PRISM_CORE_WARN("PhysicsEngine: o Jolt nao conseguiu criar a junta de '", name, "'.");
            return;
        }
        state->PhysicsSystem->AddConstraint(constraint);

        Extras::JointRecord record;
        record.Constraint = constraint;
        record.EntityA = idA;
        record.EntityB = idB;
        state->Ext->Joints.push_back(record);

        // Os dois corpos ligados nao colidem entre si (a menos que CollideConnected): grupo de colisao com tabela.
        if (!joint.CollideConnected && idB != 0xFFFFFFFFu) {
            constexpr JPH::uint kMaxSubGroups = 256;
            if (!state->Ext->JointGroups)
                state->Ext->JointGroups = new JPH::GroupFilterTable(kMaxSubGroups);
            auto subGroupOf = [&](uint32_t id) -> JPH::CollisionGroup::SubGroupID {
                auto found = state->Ext->SubGroups.find(id);
                if (found != state->Ext->SubGroups.end())
                    return found->second;
                if (state->Ext->SubGroups.size() >= kMaxSubGroups)
                    return JPH::CollisionGroup::cInvalidSubGroup;
                auto sub = (JPH::CollisionGroup::SubGroupID)state->Ext->SubGroups.size();
                state->Ext->SubGroups[id] = sub;
                return sub;
            };
            JPH::CollisionGroup::SubGroupID subA = subGroupOf(idA), subB = subGroupOf(idB);
            if (subA != JPH::CollisionGroup::cInvalidSubGroup && subB != JPH::CollisionGroup::cInvalidSubGroup) {
                state->Ext->JointGroups->DisableCollision(subA, subB);
                body1->SetCollisionGroup(JPH::CollisionGroup(state->Ext->JointGroups, 1, subA));
                body2->SetCollisionGroup(JPH::CollisionGroup(state->Ext->JointGroups, 1, subB));
            }
            else {
                PRISM_CORE_WARN("PhysicsEngine: limite de ", kMaxSubGroups, " corpos com junta sem colisao interna atingido - '", name, "' vai colidir com o corpo ligado.");
            }
        }
    }

    void PhysicsEngine::DestroyCharacterForEntity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state || !state->Ext)
            return;
        auto it = state->Ext->Characters.find((uint32_t)entity.GetHandle());
        if (it == state->Ext->Characters.end())
            return;
        RemoveJointsReferencing(*state, (uint32_t)entity.GetHandle());
        if (state->Contacts && it->second.Character)
            state->Contacts->UnregisterBody(it->second.Character->GetInnerBodyID());
        state->Ext->Characters.erase(it); // o destrutor do CharacterVirtual remove o corpo interno
    }

    void PhysicsEngine::CreateCharacterForEntity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state || !state->Ext || !entity.HasComponent<CharacterControllerComponent>())
            return;
        DestroyCharacterForEntity(scene, entity);

        const CharacterControllerComponent& cc = entity.GetComponent<CharacterControllerComponent>();
        int layer = 0;
        if (entity.HasComponent<ColliderComponent>())
            layer = std::min(std::max((int)entity.GetComponent<ColliderComponent>().Layer, 0), Physics::kLayerCount - 1);

        const float radius = std::max(cc.Radius, 0.05f);
        const float halfCylinder = std::max(cc.Height * 0.5f - radius, 0.05f);
        JPH::RefConst<JPH::Shape> shape = new JPH::CapsuleShape(halfCylinder, radius);

        JPH::CharacterVirtualSettings settings;
        settings.mShape = shape;
        settings.mUp = JPH::Vec3::sAxisY();
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(cc.MaxSlopeAngle);
        settings.mMass = std::max(cc.Mass, 0.001f);
        // A origem e o CENTRO da capsula: o "volume de apoio" e a calota de baixo (abaixo do centro da esfera inferior).
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), halfCylinder);
        // Corpo cinematico interno: outros corpos esbarram no personagem, raycasts e triggers o enxergam.
        settings.mInnerBodyShape = shape;
        settings.mInnerBodyLayer = Physics::MakeObjectLayer(layer, true);

        const glm::vec3 position = glm::vec3(scene.GetWorldTransform(entity)[3]);
        const uint32_t id = (uint32_t)entity.GetHandle();
        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            &settings, ToJolt(position), JPH::Quat::sIdentity(), (JPH::uint64)id, state->PhysicsSystem.get());

        Extras::CharacterRecord record;
        record.Character = character;
        record.LastSynced = entity.GetComponent<TransformComponent>().Translation;
        record.Layer = (uint32_t)layer;
        state->Ext->Characters[id] = record;

        if (state->Contacts)
            state->Contacts->RegisterBody(character->GetInnerBodyID(), id, false);
    }

    void PhysicsEngine::CharacterMove(Scene& scene, Entity entity, const glm::vec3& horizontalVelocity) {
        SceneState* state = GetState(scene);
        if (!state || !state->Ext)
            return;
        auto it = state->Ext->Characters.find((uint32_t)entity.GetHandle());
        if (it != state->Ext->Characters.end() && std::isfinite(horizontalVelocity.x) && std::isfinite(horizontalVelocity.z))
            it->second.DesiredVelocity = glm::vec3(horizontalVelocity.x, 0.0f, horizontalVelocity.z);
    }

    void PhysicsEngine::CharacterJump(Scene& scene, Entity entity, float jumpSpeed) {
        SceneState* state = GetState(scene);
        if (!state || !state->Ext)
            return;
        auto it = state->Ext->Characters.find((uint32_t)entity.GetHandle());
        if (it != state->Ext->Characters.end() && std::isfinite(jumpSpeed))
            it->second.JumpSpeed = std::max(jumpSpeed, 0.0f);
    }

    bool PhysicsEngine::CharacterIsGrounded(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state || !state->Ext)
            return false;
        auto it = state->Ext->Characters.find((uint32_t)entity.GetHandle());
        return it != state->Ext->Characters.end() && it->second.Character
            && it->second.Character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    glm::vec3 PhysicsEngine::CharacterGetVelocity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state || !state->Ext)
            return glm::vec3(0.0f);
        auto it = state->Ext->Characters.find((uint32_t)entity.GetHandle());
        return (it != state->Ext->Characters.end() && it->second.Character) ? FromJolt(it->second.Character->GetLinearVelocity()) : glm::vec3(0.0f);
    }

    void PhysicsEngine::StepCharacters(Scene& scene, SceneState& state, float deltaTime) {
        if (!state.Ext || state.Ext->Characters.empty())
            return;
        auto& registry = scene.GetRegistry();
        for (auto& [id, record] : state.Ext->Characters) {
            entt::entity handle = (entt::entity)id;
            if (!registry.valid(handle) || !record.Character)
                continue;
            auto* transform = registry.try_get<TransformComponent>(handle);
            auto* cc = registry.try_get<CharacterControllerComponent>(handle);
            if (!transform || !cc)
                continue;
            JPH::CharacterVirtual& character = *record.Character;

            // Um script moveu o Transform (respawn/teleporte): o personagem vai junto e para.
            if (glm::length(transform->Translation - record.LastSynced) > 0.001f) {
                character.SetPosition(ToJolt(transform->Translation));
                character.SetLinearVelocity(JPH::Vec3::sZero());
                record.LastSynced = transform->Translation;
            }

            const JPH::Vec3 gravity = state.PhysicsSystem->GetGravity() * cc->GravityScale;
            const bool onGround = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            const JPH::Vec3 groundVelocity = character.GetGroundVelocity();
            const JPH::Vec3 current = JPH::Vec3(0.0f, character.GetLinearVelocity().GetY(), 0.0f);

            JPH::Vec3 velocity;
            if (onGround && current.GetY() - groundVelocity.GetY() < 0.1f) {
                velocity = groundVelocity; // no chao (e nao subindo): acompanha a plataforma
                if (record.JumpSpeed > 0.0f)
                    velocity += JPH::Vec3(0.0f, record.JumpSpeed, 0.0f);
            }
            else {
                velocity = current; // no ar: mantem a velocidade vertical
            }
            record.JumpSpeed = 0.0f; // o pedido de pulo vale por UM passo
            velocity += gravity * deltaTime;
            velocity += ToJolt(record.DesiredVelocity);
            character.SetLinearVelocity(velocity);

            JPH::CharacterVirtual::ExtendedUpdateSettings update;
            update.mStickToFloorStepDown = cc->StickToFloor ? JPH::Vec3(0.0f, -0.5f, 0.0f) : JPH::Vec3::sZero();
            update.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(cc->StepHeight, 0.0f), 0.0f);

            const JPH::ObjectLayer objectLayer = Physics::MakeObjectLayer((int)record.Layer, true);
            JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(GetObjectVsBroadPhaseLayerFilter(), objectLayer);
            JPH::DefaultObjectLayerFilter objectFilter(GetObjectLayerPairFilter(), objectLayer);
            JPH::IgnoreSingleBodyFilter bodyFilter(character.GetInnerBodyID());
            JPH::ShapeFilter shapeFilter;
            character.ExtendedUpdate(deltaTime, gravity, update, broadPhaseFilter, objectFilter, bodyFilter, shapeFilter, *state.TempAllocator);
        }
    }

    void PhysicsEngine::SyncCharacters(Scene& scene, SceneState& state) {
        if (!state.Ext)
            return;
        auto& registry = scene.GetRegistry();
        for (auto& [id, record] : state.Ext->Characters) {
            entt::entity handle = (entt::entity)id;
            if (!registry.valid(handle) || !record.Character)
                continue;
            if (auto* transform = registry.try_get<TransformComponent>(handle)) {
                // Mesma limitacao dos corpos (docs/fisica.md): posicao de mundo gravada no Transform LOCAL.
                transform->Translation = FromJolt(record.Character->GetPosition());
                record.LastSynced = transform->Translation;
            }
        }
    }

}
