#include "ExtensionHooks.h"
#include "../Core/Log.h"
#include "../Scene/ComponentRegistry.h"
#include "../Scene/Scene.h"

#include <algorithm>
#include <exception>

namespace Prism {

    const char* EngineStageName(EngineStage stage) {
        switch (stage) {
            case EngineStage::Physics:       return "Physics";
            case EngineStage::ScriptsLua:    return "ScriptsLua";
            case EngineStage::ScriptsNative: return "ScriptsNative";
            case EngineStage::Raycast:       return "Raycast";
            case EngineStage::Particles:     return "Particles";
            case EngineStage::Animation:     return "Animation";
            case EngineStage::Audio:         return "Audio";
            default:                         return "?";
        }
    }

    bool EngineStageAllowsReplace(EngineStage stage) {
        return stage == EngineStage::Raycast || stage == EngineStage::Particles
            || stage == EngineStage::Animation || stage == EngineStage::Audio;
    }

    namespace {
        struct ComponentHook {
            ExtensionComponentHookDesc Desc;
            bool Faulted = false;           // lifecycle desligado
            bool InspectorFaulted = false;
        };
        struct StageHook {
            ExtensionStageHookDesc Desc;
            bool Faulted = false;
        };

        std::vector<ComponentHook> s_ComponentHooks; // mantido ordenado por (Order, registro)
        std::vector<StageHook> s_StageHooks;

        void Fault(bool& flag, const std::string& source, const char* what, const char* detail) {
            if (flag) return;
            flag = true;
            PRISM_CORE_ERROR("Extensao '", source, "': ", what, " lancou excecao (", detail, "). O hook foi desligado ate fechar o programa.");
        }

        // Chama 'fn' protegendo contra excecao; devolve false se lancou.
        template<typename Fn>
        bool Guard(bool& faultFlag, const std::string& source, const char* what, Fn&& fn) {
            try { fn(); return true; }
            catch (const std::exception& e) { Fault(faultFlag, source, what, e.what()); }
            catch (...) { Fault(faultFlag, source, what, "excecao desconhecida"); }
            return false;
        }

        // Entidades da cena num snapshot: um hook pode criar/destruir entidades no meio da iteracao.
        std::vector<entt::entity> Snapshot(Scene& scene) {
            std::vector<entt::entity> handles;
            for (auto handle : scene.GetRegistry().view<TagComponent>())
                handles.push_back(handle);
            return handles;
        }

        // Para cada hook com 'selector' preenchido, roda nas entidades que tem o component.
        template<typename Selector, typename Call>
        void ForEachComponentHook(Scene& scene, Selector&& selector, Call&& call) {
            // Copia dos ponteiros resolvidos: um hook pode registrar/remover hooks (raro, mas nao pode invalidar o laco).
            struct Target { size_t Index; const ComponentTypeInfo* Info; };
            std::vector<Target> targets;
            for (size_t i = 0; i < s_ComponentHooks.size(); ++i) {
                const ComponentHook& hook = s_ComponentHooks[i];
                if (hook.Faulted || !selector(hook.Desc)) continue;
                const ComponentTypeInfo* info = ComponentRegistry::FindByName(hook.Desc.Component);
                if (info) targets.push_back({ i, info });
            }
            if (targets.empty()) return;

            const std::vector<entt::entity> handles = Snapshot(scene);
            for (const Target& target : targets) {
                for (entt::entity handle : handles) {
                    if (target.Index >= s_ComponentHooks.size()) return;
                    ComponentHook& hook = s_ComponentHooks[target.Index];
                    if (hook.Faulted) break;
                    if (!scene.GetRegistry().valid(handle)) continue;
                    Entity entity(handle, &scene);
                    if (!target.Info->Has(entity)) continue;
                    call(hook, entity);
                }
            }
        }

        const char* PhaseName(int phase) { return phase == 0 ? "Before" : phase == 1 ? "Replace" : "After"; }
    }

    bool ExtensionHooks::RegisterComponentHook(ExtensionComponentHookDesc desc, const std::string& source) {
        if (desc.Component.empty()) {
            PRISM_CORE_WARN("Extensao '", source, "': hook de component sem nome do component - ignorado.");
            return false;
        }
        if (!desc.OnAdded && !desc.OnSceneStart && !desc.OnSceneStop && !desc.OnUpdate && !desc.OnFixedUpdate && !desc.DrawInspector) {
            PRISM_CORE_WARN("Extensao '", source, "': hook de '", desc.Component, "' sem nenhuma funcao - ignorado.");
            return false;
        }
        if (desc.ReplaceInspector && !desc.DrawInspector) {
            PRISM_CORE_WARN("Extensao '", source, "': ReplaceInspector em '", desc.Component, "' exige DrawInspector - ignorado.");
            return false;
        }
        if (desc.ReplaceInspector) {
            for (const ComponentHook& other : s_ComponentHooks) {
                if (other.Desc.Component == desc.Component && other.Desc.ReplaceInspector && other.Desc.Source != source) {
                    PRISM_CORE_WARN("Extensao '", source, "': o inspetor de '", desc.Component, "' ja e substituido pela extensao '",
                                    other.Desc.Source, "' - ReplaceInspector recusado.");
                    return false;
                }
            }
        }
        desc.Source = source;
        ComponentHook hook;
        hook.Desc = std::move(desc);
        // Insercao estavel por Order (menor primeiro; empate = ordem de registro).
        auto position = std::upper_bound(s_ComponentHooks.begin(), s_ComponentHooks.end(), hook.Desc.Order,
            [](int order, const ComponentHook& h) { return order < h.Desc.Order; });
        s_ComponentHooks.insert(position, std::move(hook));
        return true;
    }

    bool ExtensionHooks::RegisterStageHook(ExtensionStageHookDesc desc, const std::string& source) {
        if ((uint32_t)desc.Stage >= (uint32_t)EngineStage::Count) {
            PRISM_CORE_WARN("Extensao '", source, "': hook de estagio com estagio invalido - ignorado.");
            return false;
        }
        if (!desc.Before && !desc.After && !desc.Replace) {
            PRISM_CORE_WARN("Extensao '", source, "': hook do estagio ", EngineStageName(desc.Stage), " sem nenhuma funcao - ignorado.");
            return false;
        }
        if (desc.Replace) {
            if (!EngineStageAllowsReplace(desc.Stage)) {
                PRISM_CORE_WARN("Extensao '", source, "': o estagio ", EngineStageName(desc.Stage),
                                " nao pode ser substituido (so Before/After) - hook recusado.");
                return false;
            }
            const std::string current = GetStageReplacer(desc.Stage);
            if (!current.empty() && current != source) {
                PRISM_CORE_WARN("Extensao '", source, "': o estagio ", EngineStageName(desc.Stage), " ja e substituido pela extensao '",
                                current, "' - hook recusado.");
                return false;
            }
        }
        desc.Source = source;
        StageHook hook;
        hook.Desc = std::move(desc);
        s_StageHooks.push_back(std::move(hook));
        return true;
    }

    void ExtensionHooks::UnregisterSource(const std::string& source) {
        s_ComponentHooks.erase(std::remove_if(s_ComponentHooks.begin(), s_ComponentHooks.end(),
            [&](const ComponentHook& h) { return h.Desc.Source == source; }), s_ComponentHooks.end());
        s_StageHooks.erase(std::remove_if(s_StageHooks.begin(), s_StageHooks.end(),
            [&](const StageHook& h) { return h.Desc.Source == source; }), s_StageHooks.end());
    }

    // --- Component: ciclo de vida -----------------------------------------------------------------
    void ExtensionHooks::ComponentSceneStart(Scene& scene) {
        ForEachComponentHook(scene, [](const ExtensionComponentHookDesc& d) { return (bool)d.OnSceneStart; },
            [](ComponentHook& hook, Entity entity) {
                Guard(hook.Faulted, hook.Desc.Source, "OnSceneStart", [&] { hook.Desc.OnSceneStart(entity); });
            });
    }

    void ExtensionHooks::ComponentSceneStop(Scene& scene) {
        ForEachComponentHook(scene, [](const ExtensionComponentHookDesc& d) { return (bool)d.OnSceneStop; },
            [](ComponentHook& hook, Entity entity) {
                Guard(hook.Faulted, hook.Desc.Source, "OnSceneStop", [&] { hook.Desc.OnSceneStop(entity); });
            });
    }

    void ExtensionHooks::ComponentUpdate(Scene& scene, float deltaTime) {
        ForEachComponentHook(scene, [](const ExtensionComponentHookDesc& d) { return (bool)d.OnUpdate; },
            [deltaTime](ComponentHook& hook, Entity entity) {
                Guard(hook.Faulted, hook.Desc.Source, "OnUpdate", [&] { hook.Desc.OnUpdate(entity, deltaTime); });
            });
    }

    void ExtensionHooks::ComponentFixedUpdate(Scene& scene, float fixedDeltaTime) {
        ForEachComponentHook(scene, [](const ExtensionComponentHookDesc& d) { return (bool)d.OnFixedUpdate; },
            [fixedDeltaTime](ComponentHook& hook, Entity entity) {
                Guard(hook.Faulted, hook.Desc.Source, "OnFixedUpdate", [&] { hook.Desc.OnFixedUpdate(entity, fixedDeltaTime); });
            });
    }

    void ExtensionHooks::ComponentAdded(const std::string& component, Entity entity) {
        // Copia: um OnAdded pode mexer na lista de hooks.
        std::vector<size_t> indices;
        for (size_t i = 0; i < s_ComponentHooks.size(); ++i)
            if (!s_ComponentHooks[i].Faulted && s_ComponentHooks[i].Desc.OnAdded && s_ComponentHooks[i].Desc.Component == component)
                indices.push_back(i);
        for (size_t i : indices) {
            if (i >= s_ComponentHooks.size() || !entity.IsValid()) return;
            ComponentHook& hook = s_ComponentHooks[i];
            Guard(hook.Faulted, hook.Desc.Source, "OnAdded", [&] { hook.Desc.OnAdded(entity); });
        }
    }

    // --- Estagios -------------------------------------------------------------------------------
    void ExtensionHooks::StageBefore(EngineStage stage, Scene& scene, float deltaTime) {
        for (size_t i = 0; i < s_StageHooks.size(); ++i) {
            StageHook& hook = s_StageHooks[i];
            if (hook.Faulted || hook.Desc.Stage != stage || !hook.Desc.Before) continue;
            Guard(hook.Faulted, hook.Desc.Source, PhaseName(0), [&] { hook.Desc.Before(scene, deltaTime); });
        }
    }

    bool ExtensionHooks::StageReplace(EngineStage stage, Scene& scene, float deltaTime) {
        for (size_t i = 0; i < s_StageHooks.size(); ++i) {
            StageHook& hook = s_StageHooks[i];
            if (hook.Faulted || hook.Desc.Stage != stage || !hook.Desc.Replace) continue;
            // Se a substituta lancar excecao, o hook e desligado e a engine assume este mesmo frame
            // (melhor um frame duplicado de trabalho do que um estagio que some).
            if (Guard(hook.Faulted, hook.Desc.Source, PhaseName(1), [&] { hook.Desc.Replace(scene, deltaTime); }))
                return true;
            return false;
        }
        return false;
    }

    void ExtensionHooks::StageAfter(EngineStage stage, Scene& scene, float deltaTime) {
        for (size_t i = 0; i < s_StageHooks.size(); ++i) {
            StageHook& hook = s_StageHooks[i];
            if (hook.Faulted || hook.Desc.Stage != stage || !hook.Desc.After) continue;
            Guard(hook.Faulted, hook.Desc.Source, PhaseName(2), [&] { hook.Desc.After(scene, deltaTime); });
        }
    }

    // --- Inspetor -------------------------------------------------------------------------------
    bool ExtensionHooks::HasInspectorReplacement(const std::string& component) {
        for (const ComponentHook& hook : s_ComponentHooks)
            if (!hook.InspectorFaulted && hook.Desc.ReplaceInspector && hook.Desc.DrawInspector && hook.Desc.Component == component)
                return true;
        return false;
    }

    bool ExtensionHooks::DrawInspectors(const std::string& component, Entity entity, bool replacement) {
        bool changed = false;
        for (size_t i = 0; i < s_ComponentHooks.size(); ++i) {
            ComponentHook& hook = s_ComponentHooks[i];
            if (hook.InspectorFaulted || !hook.Desc.DrawInspector || hook.Desc.Component != component) continue;
            if (hook.Desc.ReplaceInspector != replacement) continue;
            bool result = false;
            if (Guard(hook.InspectorFaulted, hook.Desc.Source, "DrawInspector", [&] { result = hook.Desc.DrawInspector(entity); }))
                changed = changed || result;
        }
        return changed;
    }

    std::string ExtensionHooks::GetStageReplacer(EngineStage stage) {
        for (const StageHook& hook : s_StageHooks)
            if (hook.Desc.Stage == stage && hook.Desc.Replace && !hook.Faulted) return hook.Desc.Source;
        return {};
    }

    size_t ExtensionHooks::GetHookCount() { return s_ComponentHooks.size() + s_StageHooks.size(); }

    void ExtensionHooks::Clear() {
        s_ComponentHooks.clear();
        s_StageHooks.clear();
    }

} // namespace Prism
