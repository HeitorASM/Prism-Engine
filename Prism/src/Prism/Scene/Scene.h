#pragma once

// ============================================================================
// Scene.h
// Uma Scene e o container de tudo que existe no mundo: as Entities e seus
// Components. Ela e a dona do entt::registry por baixo - Entity (ver
// Entity.h) e so um "handle" leve (ID + ponteiro pra Scene dona), nunca dono
// de dado nenhum.
//
// Isto substitui o cubo hardcoded que o EditorLayer desenhava diretamente
// via uma chamada fixa ao Renderer (fase anterior do projeto): agora esse
// mesmo cubo e uma Entity real dentro de uma Scene, com TagComponent +
// TransformComponent + MeshRendererComponent - exatamente os components
// que qualquer outra entidade adicionada no editor tambem vai ter.
// ============================================================================

#include "../Core/Base.h"
#include "Components.h"
#include "Raycast.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace Prism {

    class Entity;

    class Scene {
    public:
        Scene(const std::string& name = "Untitled Scene");
        ~Scene() = default;

        // Cria uma entidade nova, ja com TagComponent + TransformComponent
        // (toda entidade da engine tem essas duas por definicao - ver
        // Components.h). O nome e so um rotulo de exibicao, pode repetir.
        Entity CreateEntity(const std::string& name = "Entity");

        // Destroi a entidade E toda a sua subarvore de filhos (recursivo) -
        // e o comportamento padrao em Unity/Godot: excluir um pai sem
        // filhos "orfaos" soltos na cena. Tambem remove a entidade da
        // lista de Children do proprio pai, se houver um.
        void DestroyEntity(Entity entity);

        // Reparenta 'child' para debaixo de 'newParent' (entt::null =
        // vira raiz). Cuida dos DOIS lados do RelationshipComponent
        // (Parent do filho + Children do pai antigo/novo) - nunca mexer
        // nesses campos direto fora daqui. Recusa (retorna false, sem
        // mudar nada) operacoes que criariam um ciclo (um ancestral nao
        // pode virar filho do proprio descendente) ou newParent == child.
        bool SetParent(Entity child, Entity newParent);

        // Matriz de mundo de uma entidade, ja combinando o TransformComponent
        // local dela com o de todos os ancestrais (via RelationshipComponent).
        // Entidades sem RelationshipComponent (ou sem Parent valido) usam so
        // o proprio TransformComponent::GetTransform() - identico ao
        // comportamento de antes do parenting existir. Isto e o unico lugar
        // que deveria ser usado para desenhar/posicionar uma entidade no
        // espaco do mundo (viewport, gizmos, fisica futura) - o
        // TransformComponent sozinho so tem o espaco LOCAL (relativo ao pai).
        glm::mat4 GetWorldTransform(Entity entity);

        // true se 'possibleAncestor' e o proprio 'entity' ou um ancestral
        // dele (pai, avo, etc). Usado por SetParent para recusar ciclos e
        // pela Hierarchy panel para recusar um drag-and-drop invalido antes
        // mesmo de chamar SetParent.
        bool IsAncestorOf(entt::entity possibleAncestor, entt::entity entity);

        // Testa 'ray' contra a bounding box (AABB) VISUAL de toda entidade
        // da cena que tenha MeshRendererComponent, transformada pela world
        // transform de cada uma (ver GetWorldTransform acima - cobre
        // parenting/escala/rotacao corretamente, nao so a TransformComponent
        // local). Retorna a entidade cuja intersecao esta MAIS PERTO da
        // origem do raio (VisualRaycastHit::Distance minimo) - entidades
        // atras da mais proxima (mesmo que o raio tambem passe pela
        // bounding box delas) nunca sao retornadas, igual um raycast de
        // qualquer engine 3D. VisualRaycastHit::Hit fica false se o raio
        // nao acertar nenhuma entidade.
        //
        // NAO CONFUNDIR com PhysicsEngine::Raycast - ver comentario em
        // Raycast.h sobre a diferenca (este aqui e visual/AABB, sempre
        // disponivel; aquele e fisico/Jolt, so com a Scene rodando).
        //
        // Usado hoje pelo picking da viewport do editor (ver
        // EditorLayer::RenderViewportPanel) - funciona tambem fora do modo
        // Play, ja que nao depende de nenhum corpo fisico existir.
        //
        // O teste em si e contra a AABB (PrimitiveMeshFactory::GetLocalBounds),
        // nao contra a geometria exata do mesh (triangulo a triangulo) -
        // suficiente para picking tipico, mas significa que clicar num
        // canto vazio da bounding box de uma esfera (fora da esfera de
        // verdade, mas dentro do cubo que a envolve) ainda conta como acerto.
        // Preciso o bastante pela imensa maioria dos casos; um teste exato
        // por triangulo fica para se/quando fizer falta na pratica.
        VisualRaycastHit VisualRaycast(const VisualRay& ray, float maxDistance = 1000.0f);

        // Chamado uma vez por frame pelo dono da Scene (hoje, EditorLayer).
        // So chama scripts, fisica e RaycastComponent (ver
        // OnScriptsStart/Stop abaixo, UpdateRaycastComponents) enquanto a
        // Scene esta "rodando" (m_IsRunning) - fora disso (a viewport
        // normal do editor, fora do modo Play) a Scene fica estatica, so
        // exibindo o estado editado. Fisica (PhysicsEngine::Simulate) roda
        // ANTES dos scripts E de UpdateRaycastComponents (ver Scene.cpp)
        // para que um script (ou o proprio RaycastComponent) sempre veja
        // a posicao ja atualizada pela fisica deste frame - senao um
        // raycast/leitura de posicao ficaria sistematicamente um frame
        // atrasado em relacao ao que e desenhado.
        void OnUpdate(float deltaTime);

        // Liga o modo "rodando": cria o mundo fisico (PhysicsEngine::
        // OnSceneStart) e carrega os scripts (ScriptEngine::LoadScript,
        // que chama OnCreate()) de toda entidade com ScriptComponent que
        // tenha um caminho de arquivo preenchido. Depois disso, OnUpdate()
        // passa a chamar PhysicsEngine::Simulate() e
        // ScriptEngine::UpdateScript() por frame. E o que o futuro modo
        // Play vai chamar ao abrir a janela separada (ver README "Nota
        // sobre modo Play") - tambem pode ser chamado isoladamente pelo
        // editor so para testar scripts/fisica sem UI de Play completa
        // ainda existir.
        void OnScriptsStart();

        // Desliga o modo "rodando": destroi o mundo fisico
        // (PhysicsEngine::OnSceneStop) e chama ScriptEngine::UnloadScript
        // (que chama OnDestroy()) para toda entidade com script carregado,
        // e para de atualiza-los em OnUpdate(). Idempotente - chamar sem
        // estar rodando nao faz nada.
        void OnScriptsStop();

        bool IsRunning() const { return m_IsRunning; }

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        // Acesso direto ao registro EnTT - usado pelo Renderer/EditorLayer
        // para iterar sobre grupos de components (ex: todas as entidades
        // com MeshRendererComponent, para desenhar a cena inteira).
        entt::registry& GetRegistry() { return m_Registry; }

        // Percorre f(EntityID, TagComponent&) para toda entidade da cena -
        // usado pela Hierarchy panel do editor para listar entidades sem
        // que o painel precise conhecer entt diretamente.
        template<typename Fn>
        void ForEachEntity(Fn&& f) {
            m_Registry.view<TagComponent>().each([&](auto entityHandle, TagComponent& tag) {
                f(entityHandle, tag);
            });
        }

        // Igual ForEachEntity, mas so entidades SEM pai (raizes da arvore -
        // sem RelationshipComponent, ou com Parent == entt::null). Usado
        // pela Hierarchy panel para comecar a desenhar a arvore a partir do
        // topo; cada nivel abaixo e desenhado recursivamente seguindo
        // RelationshipComponent::Children (ver EditorLayer::RenderHierarchyPanel).
        template<typename Fn>
        void ForEachRootEntity(Fn&& f) {
            m_Registry.view<TagComponent>().each([&](auto entityHandle, TagComponent& tag) {
                auto* rel = m_Registry.try_get<RelationshipComponent>(entityHandle);
                if (!rel || rel->Parent == entt::null)
                    f(entityHandle, tag);
            });
        }

        static Ref<Scene> Create(const std::string& name = "Untitled Scene");

    private:
        // Testa (via PhysicsEngine::Raycast, ver Physics/PhysicsEngine.h)
        // o raio de todo RaycastComponent::Enabled==true da cena, gravando
        // o resultado direto nos campos Hit/HitEntity/HitPoint/HitNormal/
        // HitDistance do proprio component (ver comentario de
        // RaycastComponent, Components.h) - assim tanto a Properties panel
        // quanto um script Lua (via entity:GetComponent... quando/se isso
        // for exposto) podem ler o ultimo resultado sem precisar disparar
        // o raio de novo manualmente. Chamado por OnUpdate() so enquanto
        // m_IsRunning (ver comentario la) - o raio e FISICO (contra
        // ColliderComponent), entao so faz sentido com o mundo Jolt
        // ativo, igual PhysicsEngine::Raycast em si (ver GetState no .cpp,
        // que retorna cedo sem a Scene rodando fisica).
        void UpdateRaycastComponents();

        std::string m_Name;
        entt::registry m_Registry;
        bool m_IsRunning = false;

        friend class Entity;
    };

}
