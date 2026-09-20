#include "PhysicsEngine.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Core/Log.h"

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
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Body/BodyFilter.h> // JPH::IgnoreMultipleBodiesFilter (PhysicsEngine::Raycast, ignoreEntities)

#include <thread>

namespace Prism {
    namespace ObjectLayers {
        static constexpr JPH::ObjectLayer Static = 0;
        static constexpr JPH::ObjectLayer Moving = 1;
        static constexpr JPH::ObjectLayer NumLayers = 2;
    }

    namespace BroadPhaseLayers {
        static constexpr JPH::BroadPhaseLayer Static(0);
        static constexpr JPH::BroadPhaseLayer Moving(1);
        static constexpr uint32_t NumLayers = 2;
    }

    class PrismObjectLayerPairFilter : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
            if (inObject1 == ObjectLayers::Static && inObject2 == ObjectLayers::Static)
                return false; // dois estaticos nunca precisam ser testados entre si
            return true;
        }
    };

    // Mapeia ObjectLayer -> BroadPhaseLayer (1:1 aqui, ja que so temos as
    // duas camadas). Jolt exige essa interface implementada pelo
    // consumidor - ver Jolt docs "Broadphase", classe
    // BroadPhaseLayerInterface.
    class PrismBroadPhaseLayerInterface : public JPH::BroadPhaseLayerInterface {
    public:
        PrismBroadPhaseLayerInterface() {
            m_ObjectToBroadPhase[ObjectLayers::Static] = BroadPhaseLayers::Static;
            m_ObjectToBroadPhase[ObjectLayers::Moving] = BroadPhaseLayers::Moving;
        }

        uint32_t GetNumBroadPhaseLayers() const override {
            return BroadPhaseLayers::NumLayers;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
            return m_ObjectToBroadPhase[inLayer];
        }

        // GetBroadPhaseLayerName SO existe como metodo puro-virtual quando
        // JPH_PROFILE_ENABLED ou JPH_EXTERNAL_PROFILE esta definido (ver
        // Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h) - o Jolt liga
        // JPH_PROFILE_ENABLED automaticamente em builds Debug por padrao
        // (CMake option PROFILER_IN_DEBUG_AND_RELEASE, ON por padrao no
        // Build/CMakeLists.txt oficial). Sem esta implementacao, a classe
        // vira abstrata e nao pode ser instanciada em Debug no MSVC (mas
        // compila normalmente em Release ou em builds sem profiling) - por
        // isso precisa do #if para acompanhar exatamente a mesma condicao
        // usada pela declaracao na lib.
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
            switch ((JPH::BroadPhaseLayer::Type)inLayer) {
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::Static:  return "STATIC";
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::Moving: return "MOVING";
                default: return "INVALID";
            }
        }
#endif

    private:
        JPH::BroadPhaseLayer m_ObjectToBroadPhase[ObjectLayers::NumLayers];
    };

    // Filtro ObjectLayer vs BroadPhaseLayer (usado durante queries/steps
    // para saber se vale a pena descer na broad-phase tree de uma
    // determinada BroadPhaseLayer) - mesma logica de
    // PrismObjectLayerPairFilter acima, so que cruzando os dois tipos de
    // layer.
    class PrismObjectVsBroadPhaseLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
            if (inLayer1 == ObjectLayers::Static)
                return inLayer2 == BroadPhaseLayers::Moving;
            return true;
        }
    };

    // Estas 3 instancias vivem pelo processo inteiro (nao por-Scene) -
    // Jolt exige que os ponteiros passados para PhysicsSystem::Init
    // permanecam validos por toda a vida do PhysicsSystem, entao static
    // local (inicializado na primeira chamada, destruido no fim do
    // processo) e o padrao mais simples aqui.
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

    // --- SceneState: construtor/destrutor/move definidos AQUI (nao no .h) ---
    // Ver o comentario extenso em PhysicsEngine.h, dentro da struct
    // SceneState, para o motivo completo: unique_ptr<JPH::PhysicsSystem>
    // (e dos outros dois) exige tipo COMPLETO no ponto onde o
    // destrutor/move e gerado - JPH::PhysicsSystem so e forward-declarado
    // no .h, entao "= default" so pode viver aqui, depois de
    // <Jolt/Physics/PhysicsSystem.h> (e os outros headers do Jolt) terem
    // sido incluidos de verdade neste arquivo.
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

    static JPH::ObjectLayer ToObjectLayer(BodyType type) {
        return type == BodyType::Static ? ObjectLayers::Static : ObjectLayers::Moving;
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

        SceneState state;

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

        GetStates()[&scene] = std::move(state);

        // Cria um corpo fisico para toda entidade elegivel (ver
        // CreateBodyForEntity para os criterios) ja existente na Scene no
        // momento em que o Play comeca.
        auto view = scene.GetRegistry().view<RigidBodyComponent, ColliderComponent>();
        for (auto entityHandle : view)
            CreateBodyForEntity(scene, Entity(entityHandle, &scene));
    }

    void PhysicsEngine::OnSceneStop(Scene& scene) {
        SceneState* state = GetState(scene);
        if (!state)
            return; // idempotente - chamar sem um mundo ativo nao faz nada

        // O Jolt exige que cada corpo seja explicitamente removido da
        // broad-phase (RemoveBody) e destruido (DestroyBody) antes do
        // PhysicsSystem em si ser liberado - ver Jolt docs "Body
        // Lifetime". Iteramos EntityToBody para isso.
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

        JPH::RefConst<JPH::Shape> shape;
        switch (collider.Shape) {
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
        }

        JPH::BodyCreationSettings bodyCreationSettings(
            shape,
            ToJolt(worldPosition),
            ToJolt(worldRotation),
            ToJoltMotionType(rigidBody.Type),
            ToObjectLayer(rigidBody.Type));

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
    }

    void PhysicsEngine::DestroyBodyForEntity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state)
            return;

        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return;

        // Jolt exige RemoveBody (tira da simulacao/broad-phase) ANTES de
        // DestroyBody (libera a memoria do corpo). Ver Jolt docs "Body
        // Lifetime".
        JPH::BodyInterface& bodyInterface = state->PhysicsSystem->GetBodyInterface();
        bodyInterface.RemoveBody(it->second);
        bodyInterface.DestroyBody(it->second);

        state->EntityToBody.erase(it);
    }

    void PhysicsEngine::Simulate(Scene& scene, float deltaTime) {
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
            state->PhysicsSystem->Update(kFixedTimeStep, kCollisionSteps, state->TempAllocator.get(), state->JobSystem.get());
            state->Accumulator -= kFixedTimeStep;
            stepsThisFrame++;
        }
        if (stepsThisFrame == kMaxStepsPerFrame)
            state->Accumulator = 0.0f; // descarta o resto acumulado - preferimos desacelerar a travar ("espiral da morte")

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

        // --- Eventos de colisao -> ScriptEngine::OnCollisionEnter/Exit ---
        // TODO: ainda nao implementado. Registrar um
        // JPH::ContactListener (PhysicsSystem::SetContactListener) e
        // traduzir OnContactAdded/OnContactRemoved para CollisionEvent
        // (que fala de entt::entity), chamando os callbacks
        // OnCollisionEnter/OnCollisionExit do script correspondente.
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

    RaycastHit PhysicsEngine::Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const std::vector<entt::entity>& ignoreEntities) {
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
            auto it = state->EntityToBody.find((uint32_t)ignored);
            if (it != state->EntityToBody.end())
                ignoreFilter.IgnoreBody(it->second);
        }

        bool hit = state->PhysicsSystem->GetNarrowPhaseQuery().CastRay(ray, rayResult, {}, {}, ignoreFilter);

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

}
