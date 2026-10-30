#include "AnimationSystem.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"
#include "../Renderer/Renderer.h"
#include "../Core/Log.h"
#include "../Scripting/ScriptEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Prism {

    namespace {

        // Garante o runtime do Animator (cria / refaz se o rig mudou). nullptr se
        // nao ha modelo ou ele nao tem esqueleto/animacao.
        AnimatorRuntime* EnsureRuntime(AnimatorComponent& animator) {
            if (!animator.ModelAsset.IsValid())
                return nullptr;
            std::shared_ptr<const ModelRig> rig = Renderer::GetOrLoadModelRig(animator.ModelAsset);
            if (!rig)
                return nullptr;

            if (!animator.Runtime.Ptr)
                animator.Runtime.Ptr = std::make_shared<AnimatorRuntime>();
            AnimatorRuntime& rt = *animator.Runtime.Ptr;
            if (rt.Rig != rig) {
                // Primeiro uso ou reimportacao do modelo: recomeca na pose de bind.
                rt = AnimatorRuntime();
                rt.Rig = rig;
                rt.Palette = rig->BindPalette;
                rt.RootNode = rig->FindRootBoneNode();
            }
            return &rt;
        }

        float AdvanceTime(float time, float delta, float duration, bool loop, bool& finished) {
            finished = false;
            if (duration <= 1e-5f)
                return 0.0f;
            time += delta;
            if (loop) {
                time = std::fmod(time, duration);
                if (time < 0.0f)
                    time += duration;
            }
            else if (time >= duration) {
                time = duration;
                finished = true;
            }
            else if (time < 0.0f) {
                time = 0.0f;
                finished = true;
            }
            return time;
        }

        // Root motion: o XZ do osso raiz fica travado no valor do inicio do clipe (o
        // movimento vai para a entidade em vez de ficar na pose).
        void LockRoot(const ModelRig& rig, const AnimatorRuntime& rt, int clip, std::vector<NodeTRS>& pose) {
            if (rt.RootNode < 0 || rt.RootNode >= (int)pose.size())
                return;
            glm::vec3 start = rig.SampleNodeTranslation(clip, 0.0f, rt.RootNode);
            pose[rt.RootNode].Translation.x = start.x;
            pose[rt.RootNode].Translation.z = start.z;
        }

        void Evaluate(AnimatorRuntime& rt, bool rootMotion) {
            const ModelRig& rig = *rt.Rig;
            if (rt.Clip < 0) {
                rt.Palette = rig.BindPalette;
                return;
            }
            rig.SamplePose(rt.Clip, rt.Time, rt.PoseA);
            if (rootMotion)
                LockRoot(rig, rt, rt.Clip, rt.PoseA);
            if (rt.FadeDuration > 0.0f && rt.PrevClip >= 0) {
                rig.SamplePose(rt.PrevClip, rt.PrevTime, rt.PoseB);
                if (rootMotion)
                    LockRoot(rig, rt, rt.PrevClip, rt.PoseB);
                float w = std::clamp(rt.FadeElapsed / rt.FadeDuration, 0.0f, 1.0f);
                w = w * w * (3.0f - 2.0f * w); // smoothstep
                ModelRig::BlendPoses(rt.PoseB, rt.PoseA, w, rt.PoseA);
            }
            rig.ComputePalette(rt.PoseA, rt.Palette);
        }

        using PendingEvent = std::pair<Entity, std::string>;

        // Reinicia um clipe (crossfade opcional a partir da pose atual).
        void StartClip(AnimatorRuntime& rt, int index, float fadeSeconds, bool loop) {
            if (fadeSeconds > 0.0f && rt.Clip >= 0) {
                rt.PrevClip = rt.Clip;
                rt.PrevTime = rt.Time;
                rt.PrevLoop = rt.Loop;
                rt.FadeDuration = fadeSeconds;
                rt.FadeElapsed = 0.0f;
            }
            else {
                rt.PrevClip = -1;
                rt.FadeDuration = 0.0f;
            }
            rt.Clip = index;
            rt.Time = 0.0f;
            rt.Loop = loop;
            rt.Playing = true;
            rt.Paused = false;
            rt.Finished = false;
            rt.EventPrevTime = -1.0f; // eventos em t=0 disparam
        }

        // Valores dos parametros acompanham a lista do grafo (editar a lista no Play
        // recomeca os valores nos padroes).
        void SyncGraph(const AnimatorComponent& animator, AnimatorRuntime& rt) {
            const auto& params = animator.Graph.Parameters;
            if (rt.ParamValues.size() == params.size() && rt.GraphParamCount == params.size())
                return;
            rt.ParamValues.assign(params.size(), 0.0f);
            for (size_t i = 0; i < params.size(); i++)
                rt.ParamValues[i] = params[i].Type == AnimParamType::Trigger ? 0.0f : params[i].Default;
            rt.GraphParamCount = params.size();
        }

        int FindParam(const AnimGraph& graph, const std::string& name) {
            for (size_t i = 0; i < graph.Parameters.size(); i++)
                if (graph.Parameters[i].Name == name)
                    return (int)i;
            return -1;
        }

        bool EnterState(const AnimatorComponent& animator, AnimatorRuntime& rt, int index, float fadeSeconds) {
            const AnimGraph& graph = animator.Graph;
            if (index < 0 || index >= (int)graph.States.size())
                return false;
            const AnimState& state = graph.States[index];
            int clip = rt.Rig->FindClip(state.Clip);
            rt.State = index;
            rt.StateSpeed = state.Speed;
            if (clip < 0) {
                if (!rt.WarnedMissingClip) {
                    PRISM_CORE_WARN("Animator: o estado '", state.Name, "' usa o clipe '", state.Clip, "', que nao existe neste modelo.");
                    rt.WarnedMissingClip = true;
                }
                return false;
            }
            StartClip(rt, clip, fadeSeconds, state.Loop);
            return true;
        }

        bool StartGraph(const AnimatorComponent& animator, AnimatorRuntime& rt) {
            SyncGraph(animator, rt);
            rt.WarnedMissingClip = false;
            int entry = animator.Graph.EntryState;
            if (entry < 0 || entry >= (int)animator.Graph.States.size())
                entry = 0;
            return EnterState(animator, rt, entry, 0.0f);
        }

        bool ConditionHolds(const AnimCondition& cond, const AnimatorComponent& animator, const AnimatorRuntime& rt) {
            int index = FindParam(animator.Graph, cond.Param);
            if (index < 0 || index >= (int)rt.ParamValues.size())
                return false;
            const float v = rt.ParamValues[index];
            switch (cond.Op) {
                case AnimCompare::Greater:   return v > cond.Value;
                case AnimCompare::Less:      return v < cond.Value;
                case AnimCompare::Equal:     return std::fabs(v - cond.Value) < 1e-4f;
                case AnimCompare::NotEqual:  return std::fabs(v - cond.Value) >= 1e-4f;
                case AnimCompare::IsTrue:
                case AnimCompare::Triggered: return v > 0.5f;
                case AnimCompare::IsFalse:   return v <= 0.5f;
            }
            return false;
        }

        // Primeira transicao valida: as do estado atual antes das de "qualquer estado".
        void EvaluateTransitions(const AnimatorComponent& animator, AnimatorRuntime& rt) {
            const AnimGraph& graph = animator.Graph;
            if (rt.State < 0 || rt.State >= (int)graph.States.size())
                return;
            SyncGraph(animator, rt);
            const float duration = rt.Clip >= 0 ? rt.Rig->Clips[rt.Clip].Duration : 0.0f;
            const float normalized = duration > 1e-5f ? rt.Time / duration : 1.0f;
            const int count = (int)graph.States.size();

            for (int pass = 0; pass < 2; pass++) {
                const int from = pass == 0 ? rt.State : -1;
                for (const AnimTransition& t : graph.Transitions) {
                    if (t.From != from || t.To < 0 || t.To >= count || t.To == rt.State)
                        continue;
                    if (!t.HasExitTime && t.Conditions.empty())
                        continue; // sem gatilho nenhum: nunca dispara
                    if (t.HasExitTime && normalized < t.ExitTime)
                        continue;
                    bool ok = true;
                    for (const AnimCondition& cond : t.Conditions)
                        if (!ConditionHolds(cond, animator, rt)) { ok = false; break; }
                    if (!ok)
                        continue;
                    for (const AnimCondition& cond : t.Conditions) { // consome os triggers usados
                        if (cond.Op != AnimCompare::Triggered)
                            continue;
                        int index = FindParam(graph, cond.Param);
                        if (index >= 0 && index < (int)rt.ParamValues.size())
                            rt.ParamValues[index] = 0.0f;
                    }
                    EnterState(animator, rt, t.To, t.Fade);
                    return;
                }
            }
        }

        // Eventos cujo tempo foi cruzado neste passo: prev < t <= cur (com volta do loop).
        void CollectEvents(Entity entity, const AnimatorComponent& animator, const AnimatorRuntime& rt,
                           float prev, float cur, bool wrapped, std::vector<PendingEvent>& out) {
            const float duration = rt.Rig->Clips[rt.Clip].Duration;
            for (const AnimEvent& ev : animator.Events) {
                if (ev.Name.empty() || rt.Rig->FindClip(ev.Clip) != rt.Clip)
                    continue;
                const float t = ev.Time;
                bool hit = wrapped ? ((t > prev && t <= duration) || t <= cur) : (t > prev && t <= cur);
                if (hit)
                    out.emplace_back(entity, ev.Name);
            }
        }

        void ApplyRootMotion(Entity entity, const AnimatorRuntime& rt, float prev, float cur, bool wrapped) {
            if (!entity.HasComponent<TransformComponent>())
                return;
            const ModelRig& rig = *rt.Rig;
            const float duration = rig.Clips[rt.Clip].Duration;
            const float from = prev < 0.0f ? 0.0f : prev;
            auto P = [&](float t) { return rig.SampleNodeTranslation(rt.Clip, t, rt.RootNode); };
            glm::vec3 delta = wrapped ? (P(duration) - P(from)) + (P(cur) - P(0.0f)) : (P(cur) - P(from));
            delta.y = 0.0f; // so o plano horizontal (a altura fica na pose)
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.Translation += glm::vec3(glm::mat3(transform.GetTransform()) * delta);
        }

        // 'live' = Play de verdade (eventos/root motion ativos); no editor so a previa da pose.
        void Step(Entity entity, AnimatorComponent& animator, AnimatorRuntime& rt, float dt, bool live, std::vector<PendingEvent>& events) {
            if (rt.Paused)
                return;
            if (rt.State >= 0 && !animator.Graph.Empty())
                EvaluateTransitions(animator, rt);
            if (!rt.Playing || rt.Clip < 0)
                return;
            const ModelRig& rig = *rt.Rig;
            const float stateSpeed = (rt.State >= 0 && !animator.Graph.Empty()) ? std::max(rt.StateSpeed, 0.0f) : 1.0f;
            const float delta = dt * std::max(animator.Speed, 0.0f) * stateSpeed;

            const float prevTime = rt.EventPrevTime;
            const float before = rt.Time;
            bool finished = false;
            rt.Time = AdvanceTime(rt.Time, delta, rig.Clips[rt.Clip].Duration, rt.Loop, finished);
            const bool wrapped = rt.Loop && rt.Time < before - 1e-6f;
            if (live) {
                CollectEvents(entity, animator, rt, prevTime, rt.Time, wrapped, events);
                if (animator.RootMotion && rt.RootNode >= 0)
                    ApplyRootMotion(entity, rt, prevTime, rt.Time, wrapped);
            }
            rt.EventPrevTime = rt.Time;

            if (finished) {
                rt.Finished = true;
                rt.Playing = false; // a ultima pose fica
            }

            if (rt.FadeDuration > 0.0f) {
                rt.FadeElapsed += dt; // a mistura nao depende da velocidade do clipe
                if (rt.PrevClip >= 0) {
                    bool prevFinished = false;
                    rt.PrevTime = AdvanceTime(rt.PrevTime, delta, rig.Clips[rt.PrevClip].Duration, rt.PrevLoop, prevFinished);
                }
                if (rt.FadeElapsed >= rt.FadeDuration) {
                    rt.FadeDuration = 0.0f;
                    rt.PrevClip = -1;
                }
            }
            Evaluate(rt, animator.RootMotion);
        }

        AnimatorComponent* GetAnimator(Entity entity) {
            if (!entity || !entity.HasComponent<AnimatorComponent>())
                return nullptr;
            return &entity.GetComponent<AnimatorComponent>();
        }

    }

    void AnimationSystem::OnSceneStart(Scene& scene) {
        auto view = scene.GetRegistry().view<AnimatorComponent>();
        for (auto handle : view) {
            AnimatorComponent& animator = view.get<AnimatorComponent>(handle);
            animator.Runtime.Ptr.reset(); // Play sempre comeca do zero
            AnimatorRuntime* rt = EnsureRuntime(animator);
            if (!rt)
                continue;
            if (!animator.PlayOnStart)
                continue;
            if (!animator.Graph.Empty())
                Prism::StartGraph(animator, *rt);
            else if (!animator.Clip.empty())
                Play(Entity(handle, &scene), animator.Clip, 0.0f, -1);
        }
    }

    void AnimationSystem::Update(Scene& scene, float deltaTime, bool editing) {
        std::vector<PendingEvent> events;
        auto view = scene.GetRegistry().view<AnimatorComponent>();
        for (auto handle : view) {
            AnimatorComponent& animator = view.get<AnimatorComponent>(handle);
            if (editing && (!animator.Runtime.Ptr || !animator.Runtime.Ptr->Playing))
                continue; // editor parado: so previas ativas
            AnimatorRuntime* rt = EnsureRuntime(animator);
            if (!rt)
                continue;
            Step(Entity(handle, &scene), animator, *rt, deltaTime, !editing, events);
        }
        // Os scripts rodam DEPOIS do laco (eles podem criar/destruir entidades e mudar Animators).
        for (PendingEvent& ev : events)
            if (ev.first.IsValid())
                ScriptEngine::DispatchAnimationEvent(ev.first, ev.second);
    }

    bool AnimationSystem::Play(Entity entity, const std::string& clip, float fadeSeconds, int loop) {
        AnimatorComponent* animator = GetAnimator(entity);
        if (!animator) {
            PRISM_CORE_WARN("PlayAnimation: a entidade nao tem Animator.");
            return false;
        }
        AnimatorRuntime* rt = EnsureRuntime(*animator);
        if (!rt) {
            PRISM_CORE_WARN("PlayAnimation: o Animator nao tem um modelo com esqueleto/animacao.");
            return false;
        }
        int index = rt->Rig->FindClip(clip);
        if (index < 0) {
            PRISM_CORE_WARN("PlayAnimation: clipe '", clip, "' nao existe neste modelo.");
            return false;
        }

        rt->State = -1; // Play direto suspende a maquina de estados (SetState/StartGraph a retomam)

        const bool wantLoop = loop < 0 ? animator->Loop : (loop != 0);
        if (rt->Clip == index && rt->Playing && !rt->Paused && rt->Loop == wantLoop)
            return true; // ja tocando: scripts podem chamar todo frame

        StartClip(*rt, index, fadeSeconds, wantLoop);
        Evaluate(*rt, animator->RootMotion);
        return true;
    }

    void AnimationSystem::Stop(Entity entity) {
        AnimatorComponent* animator = GetAnimator(entity);
        if (!animator || !animator->Runtime.Ptr)
            return;
        AnimatorRuntime& rt = *animator->Runtime.Ptr;
        rt.Playing = false;
        rt.Paused = false;
        rt.Finished = false;
        rt.Clip = -1;
        rt.State = -1;
        rt.PrevClip = -1;
        rt.FadeDuration = 0.0f;
        rt.Time = 0.0f;
        if (rt.Rig)
            rt.Palette = rt.Rig->BindPalette;
    }

    bool AnimationSystem::StartGraph(Entity entity) {
        AnimatorComponent* animator = GetAnimator(entity);
        AnimatorRuntime* rt = animator ? EnsureRuntime(*animator) : nullptr;
        if (!rt || animator->Graph.Empty())
            return false;
        return Prism::StartGraph(*animator, *rt);
    }

    namespace {
        // Acha o parametro (cria o runtime se preciso). nullptr = nao ha.
        float* ParamSlot(Entity entity, const std::string& name, const char* caller) {
            AnimatorComponent* animator = GetAnimator(entity);
            AnimatorRuntime* rt = animator ? EnsureRuntime(*animator) : nullptr;
            if (!rt) {
                PRISM_CORE_WARN(caller, ": a entidade nao tem um Animator com modelo/esqueleto.");
                return nullptr;
            }
            SyncGraph(*animator, *rt);
            int index = FindParam(animator->Graph, name);
            if (index < 0 || index >= (int)rt->ParamValues.size()) {
                PRISM_CORE_WARN(caller, ": parametro '", name, "' nao existe no grafo do Animator.");
                return nullptr;
            }
            return &rt->ParamValues[index];
        }
    }

    bool AnimationSystem::SetFloat(Entity entity, const std::string& param, float value) {
        float* slot = ParamSlot(entity, param, "SetAnimFloat");
        if (slot)
            *slot = std::isfinite(value) ? value : 0.0f;
        return slot != nullptr;
    }

    bool AnimationSystem::SetBool(Entity entity, const std::string& param, bool value) {
        float* slot = ParamSlot(entity, param, "SetAnimBool");
        if (slot)
            *slot = value ? 1.0f : 0.0f;
        return slot != nullptr;
    }

    bool AnimationSystem::SetTrigger(Entity entity, const std::string& param) {
        float* slot = ParamSlot(entity, param, "SetAnimTrigger");
        if (slot)
            *slot = 1.0f;
        return slot != nullptr;
    }

    float AnimationSystem::GetFloat(Entity entity, const std::string& param) {
        float* slot = ParamSlot(entity, param, "GetAnimFloat");
        return slot ? *slot : 0.0f;
    }

    bool AnimationSystem::SetState(Entity entity, const std::string& state, float fadeSeconds) {
        AnimatorComponent* animator = GetAnimator(entity);
        AnimatorRuntime* rt = animator ? EnsureRuntime(*animator) : nullptr;
        if (!rt) {
            PRISM_CORE_WARN("SetAnimState: a entidade nao tem um Animator com modelo/esqueleto.");
            return false;
        }
        SyncGraph(*animator, *rt);
        const auto& states = animator->Graph.States;
        for (size_t i = 0; i < states.size(); i++) {
            if (states[i].Name != state)
                continue;
            if ((int)i == rt->State && rt->Playing)
                return true;
            return EnterState(*animator, *rt, (int)i, std::max(fadeSeconds, 0.0f));
        }
        PRISM_CORE_WARN("SetAnimState: estado '", state, "' nao existe no grafo do Animator.");
        return false;
    }

    std::string AnimationSystem::GetState(Entity entity) {
        AnimatorComponent* animator = GetAnimator(entity);
        if (!animator || !animator->Runtime.Ptr)
            return {};
        const int state = animator->Runtime.Ptr->State;
        return (state >= 0 && state < (int)animator->Graph.States.size()) ? animator->Graph.States[state].Name : std::string();
    }

    void AnimationSystem::SetPaused(Entity entity, bool paused) {
        if (AnimatorComponent* animator = GetAnimator(entity))
            if (animator->Runtime.Ptr)
                animator->Runtime.Ptr->Paused = paused;
    }

    void AnimationSystem::SetSpeed(Entity entity, float speed) {
        if (AnimatorComponent* animator = GetAnimator(entity))
            animator->Speed = std::clamp(std::isfinite(speed) ? speed : 1.0f, 0.0f, 20.0f);
    }

    bool AnimationSystem::IsPlaying(Entity entity) {
        AnimatorComponent* animator = GetAnimator(entity);
        return animator && animator->Runtime.Ptr && animator->Runtime.Ptr->Playing && !animator->Runtime.Ptr->Paused;
    }

    std::string AnimationSystem::GetCurrentClip(Entity entity) {
        AnimatorComponent* animator = GetAnimator(entity);
        if (!animator || !animator->Runtime.Ptr || !animator->Runtime.Ptr->Rig)
            return {};
        const AnimatorRuntime& rt = *animator->Runtime.Ptr;
        return rt.Clip >= 0 ? rt.Rig->Clips[rt.Clip].Name : std::string();
    }

    float AnimationSystem::GetTime(Entity entity) {
        AnimatorComponent* animator = GetAnimator(entity);
        return (animator && animator->Runtime.Ptr) ? animator->Runtime.Ptr->Time : 0.0f;
    }

    float AnimationSystem::GetDuration(Entity entity) {
        AnimatorComponent* animator = GetAnimator(entity);
        if (!animator || !animator->Runtime.Ptr || !animator->Runtime.Ptr->Rig)
            return 0.0f;
        const AnimatorRuntime& rt = *animator->Runtime.Ptr;
        return rt.Clip >= 0 ? rt.Rig->Clips[rt.Clip].Duration : 0.0f;
    }

    std::vector<std::string> AnimationSystem::GetClipNames(Entity entity) {
        AnimatorComponent* animator = GetAnimator(entity);
        return animator ? GetClipNames(animator->ModelAsset) : std::vector<std::string>();
    }

    std::vector<std::string> AnimationSystem::GetClipNames(AssetID modelAsset) {
        std::vector<std::string> names;
        if (auto rig = Renderer::GetOrLoadModelRig(modelAsset))
            for (const AnimClip& clip : rig->Clips)
                names.push_back(clip.Name);
        return names;
    }

    const std::vector<glm::mat4>* AnimationSystem::FindPalette(Scene& scene, entt::entity entity, AssetID modelAsset) {
        auto& registry = scene.GetRegistry();
        // Sobe a hierarquia (limite defensivo contra ciclos). Se houver mais de um Animator
        // do mesmo modelo na cadeia (tipico: o Animator que a importacao poe na raiz do modelo
        // + o do Player que o controla), vence o MAIS EXTERNO: quem controla o personagem fica
        // acima do modelo. Sem isso o Animator interno (tocando o 1o clipe, geralmente T-pose)
        // esconderia o do Player.
        const std::vector<glm::mat4>* found = nullptr;
        for (int depth = 0; depth < 64 && entity != entt::null && registry.valid(entity); depth++) {
            if (auto* animator = registry.try_get<AnimatorComponent>(entity)) {
                if (animator->ModelAsset == modelAsset && animator->Runtime.Ptr && !animator->Runtime.Ptr->Palette.empty())
                    found = &animator->Runtime.Ptr->Palette;
            }
            auto* relationship = registry.try_get<RelationshipComponent>(entity);
            if (!relationship)
                break;
            entity = relationship->Parent;
        }
        return found;
    }

}
