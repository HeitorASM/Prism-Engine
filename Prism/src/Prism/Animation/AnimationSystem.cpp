#include "AnimationSystem.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"
#include "../Renderer/Renderer.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cmath>

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

        void Evaluate(AnimatorRuntime& rt) {
            const ModelRig& rig = *rt.Rig;
            if (rt.Clip < 0) {
                rt.Palette = rig.BindPalette;
                return;
            }
            rig.SamplePose(rt.Clip, rt.Time, rt.PoseA);
            if (rt.FadeDuration > 0.0f && rt.PrevClip >= 0) {
                rig.SamplePose(rt.PrevClip, rt.PrevTime, rt.PoseB);
                float w = std::clamp(rt.FadeElapsed / rt.FadeDuration, 0.0f, 1.0f);
                w = w * w * (3.0f - 2.0f * w); // smoothstep
                ModelRig::BlendPoses(rt.PoseB, rt.PoseA, w, rt.PoseA);
            }
            rig.ComputePalette(rt.PoseA, rt.Palette);
        }

        void Step(AnimatorComponent& animator, AnimatorRuntime& rt, float dt) {
            if (!rt.Playing || rt.Paused || rt.Clip < 0)
                return;
            const ModelRig& rig = *rt.Rig;
            float delta = dt * std::max(animator.Speed, 0.0f);

            bool finished = false;
            rt.Time = AdvanceTime(rt.Time, delta, rig.Clips[rt.Clip].Duration, rt.Loop, finished);
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
            Evaluate(rt);
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
            if (animator.PlayOnStart && !animator.Clip.empty())
                Play(Entity(handle, &scene), animator.Clip, 0.0f, -1);
        }
    }

    void AnimationSystem::Update(Scene& scene, float deltaTime, bool editing) {
        auto view = scene.GetRegistry().view<AnimatorComponent>();
        for (auto handle : view) {
            AnimatorComponent& animator = view.get<AnimatorComponent>(handle);
            if (editing && (!animator.Runtime.Ptr || !animator.Runtime.Ptr->Playing))
                continue; // editor parado: so previas ativas
            AnimatorRuntime* rt = EnsureRuntime(animator);
            if (!rt)
                continue;
            Step(animator, *rt, deltaTime);
        }
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

        const bool wantLoop = loop < 0 ? animator->Loop : (loop != 0);
        if (rt->Clip == index && rt->Playing && !rt->Paused && rt->Loop == wantLoop)
            return true; // ja tocando: scripts podem chamar todo frame

        if (fadeSeconds > 0.0f && rt->Clip >= 0) {
            rt->PrevClip = rt->Clip;
            rt->PrevTime = rt->Time;
            rt->PrevLoop = rt->Loop;
            rt->FadeDuration = fadeSeconds;
            rt->FadeElapsed = 0.0f;
        }
        else {
            rt->PrevClip = -1;
            rt->FadeDuration = 0.0f;
        }
        rt->Clip = index;
        rt->Time = 0.0f;
        rt->Loop = wantLoop;
        rt->Playing = true;
        rt->Paused = false;
        rt->Finished = false;
        Evaluate(*rt);
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
        rt.PrevClip = -1;
        rt.FadeDuration = 0.0f;
        rt.Time = 0.0f;
        if (rt.Rig)
            rt.Palette = rt.Rig->BindPalette;
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
        // Sobe a hierarquia (limite defensivo contra ciclos).
        for (int depth = 0; depth < 64 && entity != entt::null && registry.valid(entity); depth++) {
            if (auto* animator = registry.try_get<AnimatorComponent>(entity)) {
                if (animator->ModelAsset == modelAsset && animator->Runtime.Ptr && !animator->Runtime.Ptr->Palette.empty())
                    return &animator->Runtime.Ptr->Palette;
            }
            auto* relationship = registry.try_get<RelationshipComponent>(entity);
            if (!relationship)
                break;
            entity = relationship->Parent;
        }
        return nullptr;
    }

}
