#pragma once

// ============================================================================
// ExtensionHooks.h
// Como uma extensao MODIFICA o funcionamento de algo que ja existe na engine (API v2), sem
// editar a engine. Dois mecanismos, do mais cirurgico ao mais amplo:
//
//  1) HOOK DE COMPONENT - por tipo de component (os da engine, como "Light" ou "Rigid Body", ou
//     de outra extensao, pelo nome de ExtensionComponentDesc/ComponentTypeInfo):
//       - ciclo de vida: OnAdded (adicionado no editor), OnSceneStart/OnSceneStop (Play),
//         OnUpdate/OnFixedUpdate (por entidade que TEM o component, todo frame/passo);
//       - inspetor: DrawInspector desenha campos EXTRAS logo abaixo da secao do component, ou a
//         substitui por completo (ReplaceInspector).
//     Os hooks de component rodam ANTES dos sistemas registrados (SystemRegistry) e depois dos
//     scripts, no mesmo ponto do frame.
//
//  2) HOOK DE ESTAGIO - em volta de uma etapa inteira do frame da cena (fisica, particulas,
//     animacao, audio, scripts...): Before, After e - em alguns estagios - Replace, que PULA a
//     implementacao da engine para aquele estagio. So UMA extensao pode substituir cada estagio
//     (a segunda e recusada com aviso, para duas extensoes nao brigarem pelo mesmo trabalho).
//
// Ordem entre extensoes: a de carga (dependencias primeiro); dentro dela, a de registro. 'Order'
// no hook de component reordena (menor primeiro).
//
// Excecao lancada por um hook: o hook e DESLIGADO para o resto da sessao (log de erro com o nome da
// extensao) e a engine segue normalmente - nunca derruba o jogo/editor.
//
// Lembrete (vale para toda extensao): a dll so enxerga o que e inline/template. entity.GetComponent<T>()
// e scene.GetRegistry() funcionam; metodos nao-inline de Scene/PhysicsEngine NAO sao visiveis a ela.
// ============================================================================

#include "../Scene/Entity.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Prism {

    class Scene;

    enum class EngineStage : uint32_t {
        Physics = 0,       // simulacao (PhysicsEngine::Simulate, inclui os FixedUpdate dos scripts) - sem Replace
        ScriptsLua,        // OnUpdate dos scripts Lua - sem Replace
        ScriptsNative,     // OnUpdate dos scripts C++ do projeto - sem Replace
        Raycast,           // RaycastComponent - pode ser substituido
        Particles,         // ParticleSystem - pode ser substituido
        Animation,         // AnimationSystem (so no Play; as previas do editor nao passam por aqui) - pode ser substituido
        Audio,             // AudioEngine::OnSceneUpdate - pode ser substituido
        Count
    };

    const char* EngineStageName(EngineStage stage);   // "Physics", "Particles"...
    bool EngineStageAllowsReplace(EngineStage stage); // Raycast, Particles, Animation, Audio

    struct ExtensionComponentHookDesc {
        std::string Component;                        // nome do component no ComponentRegistry (DisplayName)
        int Order = 0;                                // menor roda primeiro
        std::function<void(Entity)> OnAdded;          // editor: logo apos adicionar pelo menu/undo (bom para valores padrao)
        std::function<void(Entity)> OnSceneStart;     // Play: ao iniciar (antes dos sistemas registrados)
        std::function<void(Entity)> OnSceneStop;      // Play: ao parar
        std::function<void(Entity, float)> OnUpdate;      // por frame, so durante o Play
        std::function<void(Entity, float)> OnFixedUpdate; // por passo fixo, so durante o Play
        // Editor: desenha campos com host.UiEx/ImGui e devolve true se ALGO MUDOU (o editor entao
        // registra o desfazer da edicao do component inteiro).
        std::function<bool(Entity)> DrawInspector;
        bool ReplaceInspector = false;                // true: DrawInspector toma o lugar da secao da engine
        std::string Source;                           // preenchido pela engine
    };

    struct ExtensionStageHookDesc {
        EngineStage Stage = EngineStage::Count;
        std::function<void(Scene&, float)> Before;
        std::function<void(Scene&, float)> After;
        std::function<void(Scene&, float)> Replace;   // so em estagios que permitem (EngineStageAllowsReplace)
        std::string Source;
    };

    class ExtensionHooks {
    public:
        // false (com aviso no log) se invalido: Component vazio/sem hook algum, estagio invalido,
        // Replace em estagio que nao permite, ou Replace de um estagio que outra extensao ja substituiu.
        static bool RegisterComponentHook(ExtensionComponentHookDesc desc, const std::string& source);
        static bool RegisterStageHook(ExtensionStageHookDesc desc, const std::string& source);
        static void UnregisterSource(const std::string& source);

        // --- Chamados pela engine (Scene.cpp / editor). Extensoes nao chamam. ---------------------
        static void ComponentSceneStart(Scene& scene);
        static void ComponentSceneStop(Scene& scene);
        static void ComponentUpdate(Scene& scene, float deltaTime);
        static void ComponentFixedUpdate(Scene& scene, float fixedDeltaTime);
        static void ComponentAdded(const std::string& component, Entity entity);

        // Estagios: Before(s) -> Replace OU engine -> After(s). Uso em Scene:
        //     ExtensionHooks::StageBefore(stage, *this, dt);
        //     if (!ExtensionHooks::StageReplace(stage, *this, dt)) { ...implementacao da engine... }
        //     ExtensionHooks::StageAfter(stage, *this, dt);
        static void StageBefore(EngineStage stage, Scene& scene, float deltaTime);
        static bool StageReplace(EngineStage stage, Scene& scene, float deltaTime); // true = alguem substituiu (pule a engine)
        static void StageAfter(EngineStage stage, Scene& scene, float deltaTime);

        // Inspetor (editor): hooks com DrawInspector para 'component'.
        static bool HasInspectorReplacement(const std::string& component);
        // Desenha os extras (ou a substituicao) e devolve true se algo mudou. 'replacement' escolhe quais:
        // false = campos extras abaixo da secao da engine; true = os que substituem a secao.
        static bool DrawInspectors(const std::string& component, Entity entity, bool replacement);

        // Quem substitui o estagio (nome da extensao), "" se ninguem. Para a aba Extensoes.
        static std::string GetStageReplacer(EngineStage stage);
        static size_t GetHookCount(); // componente + estagio (diagnostico/testes)

        static void Clear(); // testes
    };

} // namespace Prism
