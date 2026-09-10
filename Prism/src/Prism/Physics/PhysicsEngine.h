#pragma once

// ============================================================================
// PhysicsEngine.h
// Ponte entre a engine e o Jolt Physics (github.com/jrouwe/JoltPhysics).
//
// MIGRADO de Box3D (erincatto/box3d) para Jolt Motivo resumido: Box3D era v0.1.0
// alpha (unica release existente no momento da integracao original), com
// o proprio autor pedindo para nao mandar PRs ainda (API instavel por
// design). Jolt e usado em producao (Horizon Forbidden West) e tem
// releases semanticas estaveis - ver comentario em vendor/CMakeLists.txt.
//
// A API PUBLICA desta classe (assinaturas de todos os metodos abaixo, e
// os tipos CollisionEvent/RaycastHit) foi mantida IDENTICA a versao
// Box3D de proposito - ScriptEngine.cpp e qualquer outro consumidor
// (editor, futuros sistemas) NAO precisaram mudar uma linha por causa
// desta migracao. So este arquivo e o .cpp mudam.
//
// Design geral (mesmo espirito do ScriptEngine - ver Scripting/ScriptEngine.h):
// um mundo fisico Jolt (PhysicsSystem) por Scene "rodando", criado em
// Scene::OnScriptsStart() (o mesmo Play que liga scripts tambem liga
// fisica) e destruido em Scene::OnScriptsStop(). Corpos Jolt sao criados
// para toda entidade que tenha AMBOS RigidBodyComponent E
// ColliderComponent (mesma regra de antes - ver CreateBodyForEntity no
// .cpp).
//
// Convencao de eixos/unidades: +Y para cima (mesma convencao do resto da
// engine: TransformComponent, camera, etc) e gravidade padrao {0, -10, 0}.
// 1 unidade de engine = 1 metro (convencao Jolt tambem, ver Jolt docs
// "Units").
// ============================================================================

#include "../Core/Base.h"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <vector>

// Jolt exige varios headers "de infraestrutura" (job system, allocator,
// broad phase layers) alem do header principal - isolados aqui para nao
// espalhar includes do Jolt pelo resto do arquivo. Ver comentario extenso
// em PhysicsEngine.cpp sobre cada um desses conceitos (Jolt, diferente de
// Box3D, exige que o CONSUMIDOR da lib defina layers de colisao e job
// system - nao vem com um default pronto).
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace JPH {
    class TempAllocatorImpl;
    class JobSystemThreadPool;
}

namespace Prism {

    class Scene;
    class Entity;

    // Um evento de colisao "traduzido" para o vocabulario da engine - a
    // camada de scripting (ver ScriptEngine::RegisterAPI, TODO de fisica)
    // consome isto, nao os tipos crus do Jolt (JPH::ContactManifold etc)
    // diretamente, para nao vazar a lib de fisica escolhida para dentro
    // da API Lua (se um dia trocarmos de Jolt para outra coisa, so este
    // arquivo muda, no maximo - mesma razao de design de quando isto
    // isolava Box3D).
    struct CollisionEvent {
        entt::entity OtherEntity = entt::null; // a OUTRA entidade envolvida na colisao (nao a dona do callback)
    };

    // Resultado de um Raycast - mesmo espirito de CollisionEvent acima:
    // vocabulario da engine (glm::vec3, entt::entity), nunca tipos crus do
    // Jolt, para a escolha de lib de fisica nunca vazar para quem consome
    // isto (ScriptEngine, editor).
    struct RaycastHit {
        // false = o raio nao acertou nada dentro de MaxDistance (todo o
        // resto dos campos abaixo fica em seus valores default/invalidos -
        // nao deve ser lido se Hit == false).
        bool Hit = false;

        // A entidade dona do Collider atingido - entt::null se, por
        // algum motivo, o shape atingido nao tiver uma entidade associada
        // (nao deveria acontecer na pratica, ja que todo corpo criado por
        // CreateBodyForEntity grava a entidade dona no user data do Jolt -
        // ver comentario no .cpp - mas verificado por seguranca em vez de
        // assumido).
        entt::entity Entity = entt::null;

        glm::vec3 Point = { 0.0f, 0.0f, 0.0f };   // ponto de mundo onde o raio acertou a superficie
        glm::vec3 Normal = { 0.0f, 0.0f, 0.0f };  // normal da superficie no ponto de impacto (aponta para fora do shape)
        float Distance = 0.0f;                     // distancia do Origin ate Point (unidades de mundo/metros)
    };

    class PhysicsEngine {
    public:
        // Cria o mundo Jolt (PhysicsSystem + infraestrutura associada) para
        // esta Scene e um corpo fisico para toda entidade com
        // RigidBodyComponent+ColliderComponent (ver CreateBodyForEntity).
        // Chamado por Scene::OnScriptsStart() - fisica e scripting
        // ligam/desligam juntos (ver comentario no topo do arquivo).
        static void OnSceneStart(Scene& scene);

        // Destroi o mundo Jolt inteiro (todos os corpos sao removidos e
        // destruidos - ver OnSceneStop no .cpp). Chamado por
        // Scene::OnScriptsStop().
        static void OnSceneStop(Scene& scene);

        // Um passo de simulacao com timestep FIXO (nao deltaTime bruto -
        // ver comentario de acumulador em Simulate() no .cpp, o motivo de
        // usar passo fixo em vez de variavel - Jolt, como a maioria dos
        // motores fisicos, e mais estavel com timestep fixo). Deve ser
        // chamado toda vez que Scene::OnUpdate() for chamado enquanto a
        // Scene esta rodando - sincroniza a posicao/rotacao dos corpos de
        // volta para TransformComponent de cada entidade dinamica/
        // cinematica movida, e entrega eventos de colisao (ver
        // ConsumeCollisionEvents) para o ScriptEngine chamar
        // OnCollisionEnter/OnCollisionExit dos scripts relevantes.
        static void Simulate(Scene& scene, float deltaTime);

        // Cria (ou recria, se ja existia) o corpo Jolt de UMA entidade a
        // partir do RigidBodyComponent+ColliderComponent dela. Chamado
        // automaticamente para toda entidade relevante em OnSceneStart, e
        // tambem exposto aqui para o editor poder chamar isoladamente
        // quando um Collider/RigidBody e adicionado/editado enquanto a
        // cena ja esta rodando (evita precisar parar/reiniciar o Play so
        // por causa de um ajuste de collider).
        static void CreateBodyForEntity(Scene& scene, Entity entity);
        static void DestroyBodyForEntity(Scene& scene, Entity entity);

        // Aplica uma forca/impulso no CENTRO DE MASSA da entidade - versao
        // simplificada exposta ao Lua (ver ScriptEngine::RegisterAPI). Nao
        // faz nada (retorna silenciosamente) se a entidade nao tiver um
        // corpo fisico ativo - por exemplo, chamado fora do modo Play, ou
        // numa entidade sem RigidBodyComponent.
        static void ApplyForce(Scene& scene, Entity entity, const glm::vec3& force);
        static void ApplyLinearImpulse(Scene& scene, Entity entity, const glm::vec3& impulse);
        static glm::vec3 GetLinearVelocity(Scene& scene, Entity entity);
        static void SetLinearVelocity(Scene& scene, Entity entity, const glm::vec3& velocity);

        // Lanca um raio a partir de 'origin' na direcao 'direction' (NAO
        // precisa vir normalizada - normalizada internamente, ver .cpp)
        // ate 'maxDistance' unidades de mundo, e retorna o hit MAIS
        // PROXIMO (equivalente ao antigo b3World_CastRayClosest - convem
        // para o uso mais comum: "o que esta na minha frente?", "aponte a
        // arma para X"). Nao ha suporte a filtro de camada/mascara ainda -
        // mesma limitacao que ja existia na versao Box3D, preservada aqui
        // de proposito para manter o escopo desta migracao contido so a
        // troca de motor fisico (nao adicionar feature nova).
        //
        // 'ignoreEntities' (opcional, vazio por padrao) - corpos DESSAS
        // entidades nunca sao considerados um "hit", mesmo que o raio
        // atravesse eles geometricamente. Existe para o caso classico de
        // "o personagem esta tocando o chao?"/"o que esta na minha
        // frente?" NAO deveria acertar o proprio corpo do personagem -
        // ver RaycastComponent::IgnoreParentAndSiblings (Components.h) e
        // Scene::UpdateRaycastComponents, que monta esta lista
        // automaticamente a partir da hierarquia (Entity::GetParent()).
        // Entidades sem corpo fisico ativo neste 'ignoreEntities' sao
        // simplesmente ignoradas (nao e erro).
        //
        // Retorna um RaycastHit com Hit=false (sem crash/excecao) se: a
        // Scene nao estiver rodando fisica (fora do modo Play), ou o raio
        // simplesmente nao acertar nada dentro de maxDistance (ou so
        // acertar corpos em 'ignoreEntities') - chamador sempre deve
        // checar .Hit antes de usar os outros campos.
        static RaycastHit Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1000.0f, const std::vector<entt::entity>& ignoreEntities = {});

    private:
        // Estado de fisica de UMA Scene "rodando" - guardado fora da Scene
        // em si (Scene.h nao inclui headers do Jolt, para nao vazar um
        // header C++ de terceiros para todo arquivo que inclui Scene.h -
        // o mesmo motivo por tras de ScriptEngine viver separado). Uma
        // entrada por Scene* que esteja atualmente com IsRunning() == true.
        struct SceneState {
            // PhysicsSystem e o "mundo" do Jolt (equivalente ao b3WorldId
            // antigo) - mas, diferente de Box3D, o Jolt exige que o
            // consumidor tambem gerencie um TempAllocator (memoria
            // temporaria usada durante um Update()) e um JobSystem
            // (paralelizacao do solver entre threads) por conta propria -
            // ver comentario extenso em OnSceneStart no .cpp. Guardados
            // por unique_ptr porque JPH::PhysicsSystem e as classes de
            // allocator/job system nao sao copiaveis/movíveis com
            // seguranca, e SceneState precisa viver dentro do
            // unordered_map s_States (definido no .cpp).
            std::unique_ptr<JPH::PhysicsSystem> PhysicsSystem;
            std::unique_ptr<JPH::TempAllocatorImpl> TempAllocator;
            std::unique_ptr<JPH::JobSystemThreadPool> JobSystem;

            // accumulator de tempo para o passo FIXO de simulacao (ver
            // Simulate() no .cpp) - mesmo mecanismo que a versao Box3D ja
            // usava.
            float Accumulator = 0.0f;

            // Mapeia JPH::BodyID de volta para o entt::entity dono -
            // necessario porque eventos/queries do Jolt devolvem BodyID,
            // nao "a entidade que criou aquilo". Jolt tambem suporta um
            // "user data" de 64 bits por corpo (equivalente ao userData
            // void* que Box3D usava) - guardamos o entt::entity la
            // TAMBEM (ver CreateBodyForEntity no .cpp), mas mantemos este
            // mapa inverso (entidade -> BodyID) para
            // CreateBodyForEntity(recarregar)/DestroyBodyForEntity
            // encontrarem o corpo de uma entidade especifica sem
            // precisar de uma busca linear.
            std::unordered_map<uint32_t, JPH::BodyID> EntityToBody;

            SceneState();
            ~SceneState();
            SceneState(SceneState&&) noexcept;
            SceneState& operator=(SceneState&&) noexcept;
            SceneState(const SceneState&) = delete;
            SceneState& operator=(const SceneState&) = delete;
            // Construtor/destrutor/move DECLARADOS aqui mas DEFINIDOS no
            // .cpp (= default la, nao aqui) de proposito: JPH::PhysicsSystem,
            // TempAllocatorImpl e JobSystemThreadPool sao apenas
            // forward-declarados neste header (ver "namespace JPH {...}"
            // logo abaixo dos includes) - fora deste header, o resto da
            // engine (Scene.h e qualquer .cpp que inclua PhysicsEngine.h)
            // NAO ve as definicoes completas dessas classes, e nao
            // precisa ver. Se o destrutor/move de SceneState fossem
            // gerados implicitamente AQUI (= default no proprio .h, ou
            // omitidos), o compilador precisaria instanciar o destrutor
            // de std::unique_ptr<JPH::JobSystemThreadPool> etc bem aqui,
            // o que exige um tipo COMPLETO nesse ponto - e falha com
            // "nao e possivel excluir um tipo incompleto" (C2027/C2338)
            // em qualquer arquivo que so tenha a forward declaration.
            // Definindo os 4 metodos como "= default" dentro de
            // PhysicsEngine.cpp (que INCLUI os headers completos do
            // Jolt), o compilador so precisa gerar esse codigo la, onde
            // os tipos ja sao conhecidos por completo.
        };

        static SceneState* GetState(Scene& scene);

        // Definido no .cpp (nao inline aqui) pelo mesmo motivo do
        // comentario acima em SceneState: um unordered_map<Scene*,
        // SceneState> por si so ja exigiria instanciar os
        // destrutores/movs de SceneState em CADA arquivo que inclui este
        // header, se ficasse "inline static" aqui. Ver definicao real em
        // PhysicsEngine.cpp.
        static std::unordered_map<Scene*, SceneState>& GetStates();

        static constexpr float kFixedTimeStep = 1.0f / 60.0f;
        static constexpr int kMaxStepsPerFrame = 5; // mesma protecao de "espiral da morte" que a versao Box3D ja tinha
        static constexpr int kCollisionSteps = 1;    // sub-steps do solver do Jolt por chamada de Update() - ver comentario no .cpp
    };

}
