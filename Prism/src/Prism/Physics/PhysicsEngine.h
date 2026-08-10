#pragma once

// ============================================================================
// PhysicsEngine.h
// Ponte entre a engine e o Box3D (github.com/erincatto/box3d, fixado em
// v0.1.0 - ver comentario de ATENCAO em vendor/CMakeLists.txt sobre essa
// lib ainda estar em alpha).
//
// Design geral (mesmo espirito do ScriptEngine - ver Scripting/ScriptEngine.h):
// um b3WorldId por Scene "rodando", criado em Scene::OnScriptsStart() (o
// mesmo Play que liga scripts tambem liga fisica - conceitualmente sao a
// mesma coisa: "a simulacao esta rodando ou nao") e destruido em
// Scene::OnScriptsStop(). Corpos Box3D sao criados para toda entidade que
// tenha AMBOS RigidBodyComponent E ColliderComponent (RigidBody sozinho,
// sem forma de colisao, nao tem shape pra criar; Collider sozinho, sem
// RigidBody, e tratado como um trigger/sensor estatico - ver comentario em
// CreateBodyForEntity no .cpp).
//
// Convencao de eixos/unidades: Box3D nao tem conceito de "up" embutido -
// usamos +Y para cima (mesma convencao ja usada pelo resto da engine:
// TransformComponent, camera, etc) e gravidade padrao {0, -10, 0}. 1 unidade
// de engine = 1 metro (convencao Box3D/a maioria dos motores fisicos).
// ============================================================================

#include "../Core/Base.h"
#include <box3d/box3d.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <unordered_map>
#include <cstdint>

namespace Prism {

    class Scene;
    class Entity;

    // Um evento de colisao "traduzido" para o vocabulario da engine - a
    // camada de scripting (ver ScriptEngine::RegisterAPI, TODO de fisica)
    // consome isto, nao os tipos b3ContactBeginTouchEvent/EndTouchEvent
    // crus do Box3D diretamente, para nao vazar a lib de fisica escolhida
    // para dentro da API Lua (se um dia trocarmos de Box3D para outra
    // coisa, so este arquivo muda, no maximo).
    struct CollisionEvent {
        entt::entity OtherEntity = entt::null; // a OUTRA entidade envolvida na colisao (nao a dona do callback)
    };

    // Resultado de um Raycast - mesmo espirito de CollisionEvent acima:
    // vocabulario da engine (glm::vec3, entt::entity), nunca tipos crus do
    // Box3D (b3RayResult/b3ShapeId/b3Pos), para a escolha de lib de fisica
    // nunca vazar para quem consome isto (ScriptEngine, editor).
    struct RaycastHit {
        // false = o raio nao acertou nada dentro de MaxDistance (todo o
        // resto dos campos abaixo fica em seus valores default/invalidos -
        // nao deve ser lido se Hit == false, mesmo padrao de
        // b3RayResult::hit na lib original).
        bool Hit = false;

        // A entidade dona do Collider atingido - entt::null se, por
        // algum motivo, o shape atingido nao tiver uma entidade associada
        // (nao deveria acontecer na pratica, ja que todo corpo criado por
        // CreateBodyForEntity grava a entidade dona em userData - ver
        // comentario la - mas verificado por seguranca em vez de assumido).
        entt::entity Entity = entt::null;

        glm::vec3 Point = { 0.0f, 0.0f, 0.0f };   // ponto de mundo onde o raio acertou a superficie
        glm::vec3 Normal = { 0.0f, 0.0f, 0.0f };  // normal da superficie no ponto de impacto (aponta para fora do shape)
        float Distance = 0.0f;                     // distancia do Origin ate Point (unidades de mundo/metros)
    };

    class PhysicsEngine {
    public:
        // Cria o mundo Box3D para esta Scene e um corpo fisico para toda
        // entidade com RigidBodyComponent+ColliderComponent (ver
        // CreateBodyForEntity). Chamado por Scene::OnScriptsStart() -
        // fisica e scripting ligam/desligam juntos (ver comentario no topo
        // do arquivo).
        static void OnSceneStart(Scene& scene);

        // Destroi o mundo Box3D inteiro (b3DestroyWorld ja destroi todo
        // corpo/shape/joint associado de uma vez - ver docs do Box3D,
        // muito mais rapido que destruir um por um). Chamado por
        // Scene::OnScriptsStop().
        static void OnSceneStop(Scene& scene);

        // Um passo de simulacao com timestep FIXO (nao deltaTime bruto -
        // ver comentario de acumulador em Simulate() no .cpp, o motivo de
        // usar passo fixo em vez de variavel). Deve ser chamado toda vez
        // que Scene::OnUpdate() for chamado enquanto a Scene esta rodando -
        // sincroniza b3World_GetBodyEvents() de volta para TransformComponent
        // de cada entidade dinamica/cinematica movida, e entrega eventos de
        // colisao (ver ConsumeCollisionEvents) para o ScriptEngine chamar
        // OnCollisionEnter/OnCollisionExit dos scripts relevantes.
        static void Simulate(Scene& scene, float deltaTime);

        // Cria (ou recria, se ja existia) o corpo Box3D de UMA entidade a
        // partir do RigidBodyComponent+ColliderComponent dela. Chamado
        // automaticamente para toda entidade relevante em OnSceneStart, e
        // tambem exposto aqui para o editor poder chamar isoladamente
        // quando um Collider/RigidBody e adicionado/editado enquanto a
        // cena ja esta rodando (evita precisar parar/reiniciar o Play so
        // por causa de um ajuste de collider).
        static void CreateBodyForEntity(Scene& scene, Entity entity);
        static void DestroyBodyForEntity(Scene& scene, Entity entity);

        // Aplica uma forca/impulso no CENTRO DE MASSA da entidade (ver
        // b3Body_ApplyForceToCenter/ApplyLinearImpulseToCenter na doc do
        // Box3D) - versao simplificada exposta ao Lua (ver
        // ScriptEngine::RegisterAPI). Nao faz nada (retorna silenciosamente)
        // se a entidade nao tiver um corpo fisico ativo - por exemplo,
        // chamado fora do modo Play, ou numa entidade sem RigidBodyComponent.
        static void ApplyForce(Scene& scene, Entity entity, const glm::vec3& force);
        static void ApplyLinearImpulse(Scene& scene, Entity entity, const glm::vec3& impulse);
        static glm::vec3 GetLinearVelocity(Scene& scene, Entity entity);
        static void SetLinearVelocity(Scene& scene, Entity entity, const glm::vec3& velocity);

        // Lanca um raio a partir de 'origin' na direcao 'direction'
        // (NAO precisa vir normalizada - normalizada internamente, ver
        // .cpp) ate 'maxDistance' unidades de mundo, e retorna o hit MAIS
        // PROXIMO (ver b3World_CastRayClosest na doc do Box3D - convem
        // para o uso mais comum: "o que esta na minha frente?", "aponte a
        // arma para X", "o personagem esta tocando o chao?"). Nao ha
        // suporte a filtro de camada/mascara ainda (b3World_CastRayClosest
        // nao aceita customizacao fina, apenas um filtro default - ver
        // comentario no .cpp) - se um dia for necessario ignorar
        // seletivamente certas entidades/camadas, use b3World_CastRay (com
        // callback) em vez de b3World_CastRayClosest, o que exigiria uma
        // nova sobrecarga aqui.
        //
        // Retorna um RaycastHit com Hit=false (sem crash/excecao) se: a
        // Scene nao estiver rodando fisica (fora do modo Play), ou o raio
        // simplesmente nao acertar nada dentro de maxDistance - chamador
        // sempre deve checar .Hit antes de usar os outros campos.
        static RaycastHit Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1000.0f);

    private:
        // Estado de fisica de UMA Scene "rodando" - guardado fora da Scene
        // em si (Scene.h nao inclui box3d/box3d.h, para nao vazar um header
        // C de terceiros para todo arquivo que inclui Scene.h - o mesmo
        // motivo por tras de ScriptEngine viver separado). Uma entrada por
        // Scene* que esteja atualmente com IsRunning() == true.
        struct SceneState {
            b3WorldId WorldId = b3_nullWorldId;
            // accumulator de tempo para o passo FIXO de simulacao (ver
            // Simulate() no .cpp) - Box3D (como a maioria dos motores de
            // fisica) e mais estavel com timestep fixo do que com o
            // deltaTime variavel que a engine recebe do sistema.
            float Accumulator = 0.0f;
            // Mapeia b3BodyId (que carrega um indice interno do Box3D) de
            // volta para o entt::entity dono - necessario porque
            // b3BodyEvents/b3ContactEvents devolvem b3BodyId/b3ShapeId, nao
            // "a entidade que criou aquilo". Usamos o ponteiro void*
            // userData do proprio Box3D (ver b3BodyDef::userData na doc)
            // para isso em vez de um mapa - mais direto - mas o mapa
            // inverso (entidade -> BodyId) ainda precisa existir para
            // CreateBodyForEntity(recarregar)/DestroyBodyForEntity
            // encontrarem o corpo de uma entidade especifica.
            std::unordered_map<uint32_t, b3BodyId> EntityToBody;
        };

        static SceneState* GetState(Scene& scene);

        inline static std::unordered_map<Scene*, SceneState> s_States;

        static constexpr float kFixedTimeStep = 1.0f / 60.0f;
        static constexpr int kSubStepCount = 4; // recomendado pela doc oficial do Box3D
    };

}
