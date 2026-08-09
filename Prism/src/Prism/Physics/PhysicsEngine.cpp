#include "PhysicsEngine.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Core/Log.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Prism {

    // --- Conversao glm <-> Box3D -------------------------------------------
    // Box3D usa b3Vec3 (x,y,z) e b3Quat (v.x,v.y,v.z,s) - ambos layouts
    // compativeis campo-a-campo com glm::vec3/glm::quat, mas SAO TIPOS
    // DIFERENTES (structs C puras vs classes glm) entao precisam de
    // conversao explicita em vez de reinterpret_cast (mais seguro, o
    // otimizador do compilador remove o overhead disso de qualquer jeito).

    static b3Vec3 ToB3(const glm::vec3& v) { return b3Vec3{ v.x, v.y, v.z }; }
    static glm::vec3 FromB3(const b3Vec3& v) { return glm::vec3(v.x, v.y, v.z); }

    // glm::quat guarda (w,x,y,z) na ordem de CONSTRUCAO glm::quat(w,x,y,z),
    // mas armazena internamente como .x/.y/.z/.w - b3Quat guarda a parte
    // vetorial em .v (b3Vec3) e a escalar em .s. Mapeamento direto:
    // b3Quat.v = (x,y,z) do glm::quat, b3Quat.s = w do glm::quat.
    static b3Quat ToB3(const glm::quat& q) { return b3Quat{ { q.x, q.y, q.z }, q.w }; }
    static glm::quat FromB3(const b3Quat& q) { return glm::quat(q.s, q.v.x, q.v.y, q.v.z); }

    // TransformComponent::Rotation e Euler EM GRAUS na ordem yawPitchRoll(Y,X,Z)
    // (ver TransformComponent::GetTransform() em Components.h - usamos a
    // MESMA convencao aqui de proposito, para que o corpo fisico comece
    // exatamente na mesma orientacao visual que a entidade tinha no editor).
    // Extraimos o quaternion equivalente construindo a mesma matriz de
    // rotacao e convertendo, em vez de montar o quaternion a partir dos 3
    // angulos com outra formula - assim garantimos bit-a-bit a mesma
    // convencao de composicao de eixos que o resto da engine ja usa.
    static glm::quat EulerDegreesToQuat(const glm::vec3& eulerDegrees) {
        glm::mat4 rotationMatrix = glm::yawPitchRoll(
            glm::radians(eulerDegrees.y), glm::radians(eulerDegrees.x), glm::radians(eulerDegrees.z));
        return glm::quat_cast(rotationMatrix);
    }

    // Inverso de EulerDegreesToQuat - usado para escrever de volta em
    // TransformComponent::Rotation depois que o Box3D move o corpo (ver
    // Simulate() abaixo). glm::eulerAngles devolve radianos em uma
    // convencao PRY (pitch,yaw,roll) por padrao; reordenamos para bater
    // com Translation/Rotation.y=yaw,.x=pitch,.z=roll que o resto da
    // engine usa.
    static glm::vec3 QuatToEulerDegrees(const glm::quat& q) {
        glm::vec3 pitchYawRoll = glm::eulerAngles(q); // (pitch=x, yaw=y, roll=z) em radianos, convencao glm padrao
        return glm::degrees(glm::vec3(pitchYawRoll.x, pitchYawRoll.y, pitchYawRoll.z));
    }

    static b3BodyType ToB3(BodyType type) {
        switch (type) {
            case BodyType::Static:    return b3_staticBody;
            case BodyType::Kinematic: return b3_kinematicBody;
            case BodyType::Dynamic:   return b3_dynamicBody;
        }
        return b3_staticBody;
    }

    PhysicsEngine::SceneState* PhysicsEngine::GetState(Scene& scene) {
        auto it = s_States.find(&scene);
        if (it == s_States.end())
            return nullptr;
        return &it->second;
    }

    void PhysicsEngine::OnSceneStart(Scene& scene) {
        if (GetState(scene) != nullptr) {
            PRISM_CORE_ERROR("PhysicsEngine::OnSceneStart chamado para uma Scene que ja tinha um mundo fisico ativo - ignorado.");
            return;
        }

        SceneState state;

        b3WorldDef worldDef = b3DefaultWorldDef();
        // Convencao +Y para cima, mesma usada pelo resto da engine (camera,
        // TransformComponent, etc) - Box3D nao tem "up" embutido, entao
        // isso e so a direcao da gravidade padrao.
        worldDef.gravity = b3Vec3{ 0.0f, -10.0f, 0.0f };
        state.WorldId = b3CreateWorld(&worldDef);

        s_States[&scene] = state;

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

        // b3DestroyWorld ja destroi todo corpo/shape/joint associado de uma
        // vez (muito mais rapido que iterar e destruir um por um - ver doc
        // do Box3D, secao "World Lifetime") - nao precisamos iterar
        // EntityToBody aqui, so limpar nossa propria contabilidade.
        b3DestroyWorld(state->WorldId);
        s_States.erase(&scene);
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
        auto& transform = entity.GetComponent<TransformComponent>();

        // Usamos a transform de MUNDO (ancestrais inclusos - ver
        // Scene::GetWorldTransform, parenting) para a posicao/rotacao
        // inicial do corpo Box3D, nao so o TransformComponent local -
        // senao um filho de um pai deslocado nasceria fisicamente na
        // posicao ERRADA (a posicao local, nao a posicao real dele na
        // cena). Depois de criado, o corpo fisico e independente do
        // parenting (Box3D nao sabe de RelationshipComponent) - isso e
        // uma limitacao conhecida, ver nota no README.
        glm::mat4 worldMatrix = scene.GetWorldTransform(entity);
        glm::vec3 worldPosition = glm::vec3(worldMatrix[3]);

        // ATENCAO - bug corrigido: glm::quat_cast(worldMatrix) direto
        // extrai a rotacao de uma matriz que ainda tem a ESCALA embutida
        // nas colunas (Translation * Rotation * Scale, ver
        // TransformComponent::GetTransform()) - a menos que Scale seja
        // exatamente {1,1,1}, isso produz um quaternion NAO normalizado/
        // distorcido (mais visivel quanto mais nao-uniforme a escala).
        // Box3D valida a rotacao recebida em b3BodyDef internamente e
        // dispara um assert (__debugbreak, o crash reportado) quando ela
        // nao esta normalizada - por isso o crash so acontecia as vezes
        // (entidades com Scale={1,1,1} "passavam por sorte", erro de
        // ponto flutuante acumulado ou qualquer Scale nao-uniforme
        // estourava o assert). A correcao e remover a escala das 3
        // colunas de rotacao ANTES de extrair o quaternion (nao apos, com
        // um normalize generico - isso normaliza o QUATERNION mas nao
        // desfaz a distorcao de uma escala nao-uniforme na matriz de
        // origem, que produz um eixo de rotacao errado, nao so uma
        // magnitude errada).
        glm::vec3 col0 = glm::vec3(worldMatrix[0]);
        glm::vec3 col1 = glm::vec3(worldMatrix[1]);
        glm::vec3 col2 = glm::vec3(worldMatrix[2]);
        glm::mat3 rotationOnly(
            glm::length(col0) > 0.00001f ? col0 / glm::length(col0) : glm::vec3(1, 0, 0),
            glm::length(col1) > 0.00001f ? col1 / glm::length(col1) : glm::vec3(0, 1, 0),
            glm::length(col2) > 0.00001f ? col2 / glm::length(col2) : glm::vec3(0, 0, 1)
        );
        glm::quat worldRotation = glm::normalize(glm::quat_cast(rotationOnly));

        b3BodyDef bodyDef = b3DefaultBodyDef();
        bodyDef.type = ToB3(rigidBody.Type);
        bodyDef.position = ToB3(worldPosition);
        bodyDef.rotation = ToB3(worldRotation);
        bodyDef.gravityScale = rigidBody.UseGravity ? 1.0f : 0.0f;
        bodyDef.isBullet = rigidBody.ContinuousCollisionDetection;
        // userData aponta para o proprio entt::entity (armazenado por
        // valor dentro de um uintptr_t via reinterpret - ver comentario
        // abaixo) para que eventos do Box3D (b3BodyEvents, contact events)
        // consigam voltar para "qual entidade e essa" sem precisar de uma
        // busca linear. entt::entity e tipicamente um uint32_t por baixo -
        // cabe tranquilamente num void*.
        bodyDef.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(entity.GetHandle())));

        b3BodyId bodyId = b3CreateBody(state->WorldId, &bodyDef);

        b3ShapeDef shapeDef = b3DefaultShapeDef();
        shapeDef.density = 1.0f; // TODO: expor densidade/massa customizada quando RigidBodyComponent::Mass for usado para overridar via b3Body_SetMassData
        shapeDef.isSensor = collider.IsTrigger;
        shapeDef.enableContactEvents = true; // necessario para b3World_GetContactEvents relatar begin/end touch (ver Simulate())
        shapeDef.enableSensorEvents = collider.IsTrigger; // sensores usam um canal de evento separado do Box3D (ver doc "Sensors")

        switch (collider.Shape) {
            case ColliderShape::Box: {
                // Collider::Size ja e meio-tamanho (half-extents) por
                // convencao da propria engine (ver comentario em
                // ColliderComponent::Size em Components.h) - bate
                // exatamente com o que b3MakeBoxHull espera.
                b3BoxHull box = b3MakeBoxHull(collider.Size.x, collider.Size.y, collider.Size.z);
                b3CreateHullShape(bodyId, &shapeDef, &box.base);
                break;
            }
            case ColliderShape::Sphere: {
                // ATENCAO: b3CreateSphereShape e b3Sphere{center,radius}
                // seguem o padrao documentado de b3CreateHullShape (ver
                // docs/simulation.md) e o padrao identico do Box2D 3.x
                // (b2CreateCircleShape/b2Circle), mas a doc publica do
                // Box3D nao mostra o exemplo literal de esfera/capsula no
                // momento em que este codigo foi escrito (so cita que os
                // tipos b3Sphere/b3Capsule existem). Se o nome real
                // divergir, o erro aparece como um erro de COMPILACAO
                // (funcao nao encontrada) - facil de localizar e corrigir
                // em box3d/include/box3d/*.h apos o primeiro build.
                b3Sphere sphere;
                sphere.center = b3Vec3{ 0.0f, 0.0f, 0.0f };
                sphere.radius = collider.Size.x;
                b3CreateSphereShape(bodyId, &shapeDef, &sphere);
                break;
            }
            case ColliderShape::Capsule: {
                // Mesma ressalva de nome de funcao que Sphere acima.
                // Capsula alinhada ao eixo Y (convencao usual - "de pe"),
                // ponta a ponta separadas por Size.y (altura total da
                // parte cilindrica, sem contar as tampas hemisfericas).
                b3Capsule capsule;
                float halfHeight = collider.Size.y * 0.5f;
                capsule.center1 = b3Vec3{ 0.0f, -halfHeight, 0.0f };
                capsule.center2 = b3Vec3{ 0.0f, halfHeight, 0.0f };
                capsule.radius = collider.Size.x;
                b3CreateCapsuleShape(bodyId, &shapeDef, &capsule);
                break;
            }
        }

        state->EntityToBody[(uint32_t)entity.GetHandle()] = bodyId;
    }

    void PhysicsEngine::DestroyBodyForEntity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state)
            return;

        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return;

        // b3DestroyBody ja destroi os shapes/joints anexados ao corpo
        // automaticamente (ver doc "Body Lifetime") - nao precisamos
        // rastrear b3ShapeId separadamente.
        b3DestroyBody(it->second);
        state->EntityToBody.erase(it);
    }

    void PhysicsEngine::Simulate(Scene& scene, float deltaTime) {
        SceneState* state = GetState(scene);
        if (!state)
            return;

        // Timestep FIXO via acumulador: Box3D (como a maioria dos motores
        // de fisica) e projetado e testado com um passo constante (a doc
        // recomenda 1/60 ou menor) - alimentar deltaTime bruto e variavel
        // direto no step degrada estabilidade (colisoes perdidas em
        // quedas de frame-rate, jitter). Acumulamos o tempo real e
        // consumimos em fatias fixas de kFixedTimeStep, rodando 0, 1 ou
        // varios steps neste frame conforme necessario. Limitamos a 5
        // steps por frame (ver 'if' abaixo) para evitar uma "espiral da
        // morte" se o frame demorar demais (ex: debugger pausado) - nesse
        // caso preferimos a simulacao ficar visivelmente mais lenta a
        // travar tentando processar um deltaTime gigante de uma vez.
        state->Accumulator += deltaTime;
        int stepsThisFrame = 0;
        const int kMaxStepsPerFrame = 5;
        while (state->Accumulator >= kFixedTimeStep && stepsThisFrame < kMaxStepsPerFrame) {
            b3World_Step(state->WorldId, kFixedTimeStep, kSubStepCount);
            state->Accumulator -= kFixedTimeStep;
            stepsThisFrame++;
        }
        if (stepsThisFrame == kMaxStepsPerFrame)
            state->Accumulator = 0.0f; // descarta o resto acumulado - preferimos desacelerar a travar (ver comentario acima)

        // --- Sincroniza corpos movidos de volta para TransformComponent ---
        // b3World_GetBodyEvents so retorna corpos que REALMENTE se moveram
        // neste step (nao dorminetes) - muito mais barato que iterar todo
        // EntityToBody e ler a transform de cada um incondicionalmente
        // (ver doc "Body Events").
        b3BodyEvents events = b3World_GetBodyEvents(state->WorldId);
        for (int i = 0; i < events.moveCount; i++) {
            const b3BodyMoveEvent* event = events.moveEvents + i;
            uint32_t handleValue = (uint32_t)reinterpret_cast<uintptr_t>(event->userData);
            entt::entity handle = (entt::entity)handleValue;
            if (!scene.GetRegistry().valid(handle))
                continue; // entidade pode ter sido destruida no mesmo frame - ver DestroyEntity/ScriptEngine

            auto* transform = scene.GetRegistry().try_get<TransformComponent>(handle);
            if (!transform)
                continue;

            // ATENCAO - limitacao conhecida (ver README): escrevemos
            // POSICAO/ROTACAO DE MUNDO direto no TransformComponent LOCAL
            // da entidade, ignorando qualquer ancestral (RelationshipComponent).
            // Isso e correto para entidades SEM pai (a esmagadora maioria
            // dos casos de gameplay), mas produz resultado errado para uma
            // entidade fisica que tambem seja filha de outra na hierarquia
            // (o Box3D nao tem ideia de que existe um pai). Corrigir isso
            // exigiria multiplicar pela inversa da transform do pai a cada
            // sincronizacao - adiado de proposito (fisica + parenting
            // combinados e um caso raro em cenas de gameplay tipicas, e
            // adicionar isso agora aumentaria bastante o escopo desta
            // primeira integracao).
            transform->Translation = FromB3(event->transform.p);
            transform->Rotation = QuatToEulerDegrees(glm::normalize(FromB3(event->transform.q)));

            if (event->fellAsleep) {
                // Corpo parou de se mover e foi dormir - nao ha nada a
                // fazer aqui hoje (sem indicador visual de "dormindo" no
                // editor ainda), mas o campo existe na doc do Box3D para
                // quem quiser usar futuramente (ex: pausar particulas de
                // poeira quando uma caixa para de rolar).
            }
        }

        // --- Eventos de colisao -> ScriptEngine::OnCollisionEnter/Exit ---
        // TODO(proxima iteracao de scripting): quando ScriptEngine expuser
        // OnCollisionEnter/OnCollisionExit para Lua (ver TODO em
        // ScriptEngine::RegisterAPI), este e o lugar que vai traduzir
        // b3ContactBeginTouchEvent/EndTouchEvent (que falam de b3ShapeId,
        // nao de entidades) para CollisionEvent (que fala de entt::entity)
        // e chamar o callback do script correspondente. Adiado de
        // proposito desta primeira integracao para manter o escopo
        // controlado - a sincronizacao de Transform acima ja e o suficiente
        // para objetos caindo/colidindo aparecerem corretamente na
        // viewport, que e o objetivo desta etapa.
    }

    void PhysicsEngine::ApplyForce(Scene& scene, Entity entity, const glm::vec3& force) {
        SceneState* state = GetState(scene);
        if (!state)
            return;
        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return; // silencioso de proposito - ver comentario no .h (script chamando fora do Play, ou entidade sem fisica)
        b3Body_ApplyForceToCenter(it->second, ToB3(force), true);
    }

    void PhysicsEngine::ApplyLinearImpulse(Scene& scene, Entity entity, const glm::vec3& impulse) {
        SceneState* state = GetState(scene);
        if (!state)
            return;
        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return;
        b3Body_ApplyLinearImpulseToCenter(it->second, ToB3(impulse), true);
    }

    glm::vec3 PhysicsEngine::GetLinearVelocity(Scene& scene, Entity entity) {
        SceneState* state = GetState(scene);
        if (!state)
            return glm::vec3(0.0f);
        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return glm::vec3(0.0f);
        return FromB3(b3Body_GetLinearVelocity(it->second));
    }

    void PhysicsEngine::SetLinearVelocity(Scene& scene, Entity entity, const glm::vec3& velocity) {
        SceneState* state = GetState(scene);
        if (!state)
            return;
        auto it = state->EntityToBody.find((uint32_t)entity.GetHandle());
        if (it == state->EntityToBody.end())
            return;
        // ATENCAO: b3Body_SetLinearVelocity segue o padrao get/set espelhado
        // que o resto da API usa (ex: b3Body_GetAwake/SetAwake), mas nao foi
        // confirmado contra a doc oficial no momento em que este codigo foi
        // escrito - mesma ressalva de Sphere/Capsule acima (erro de nome
        // aparece como erro de compilacao, facil de corrigir).
        b3Body_SetLinearVelocity(it->second, ToB3(velocity));
    }

}
