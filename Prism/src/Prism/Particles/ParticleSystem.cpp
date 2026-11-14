#include "ParticleSystem.h"
#include "../Scene/Scene.h"
#include <algorithm>
#include <cmath>

namespace Prism {

    std::unordered_map<const Scene*, ParticleSystem::SceneStates> ParticleSystem::s_States;

    namespace {
        constexpr float kPi = 3.14159265358979f;
        constexpr float kGravity = 9.81f;

        // xorshift32: barato e deterministico por emissor (sem estado global).
        float Random01(uint32_t& seed) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return (float)(seed & 0xFFFFFF) / (float)0x1000000;
        }

        float RandomRange(uint32_t& seed, float a, float b) {
            if (b < a)
                std::swap(a, b);
            return a + (b - a) * Random01(seed);
        }
    }

    void ParticleSystem::Start(Scene& scene, entt::entity entity, EmitterState& state) {
        (void)scene;
        state.Particles.clear();
        state.Accumulator = 0.0f;
        state.Time = 0.0f;
        state.Playing = true;
        state.Seed = 0x9E3779B9u ^ ((uint32_t)entity * 2654435761u);
        if (state.Seed == 0)
            state.Seed = 1;
    }

    void ParticleSystem::OnSceneStart(Scene& scene) {
        SceneStates& states = s_States[&scene];
        states.clear();
        auto view = scene.GetRegistry().view<ParticleEmitterComponent>();
        for (auto handle : view) {
            const auto& emitter = view.get<ParticleEmitterComponent>(handle);
            if (!emitter.Enabled || !emitter.PlayOnStart)
                continue;
            EmitterState& state = states[handle];
            Start(scene, handle, state);
            Emit(scene, handle, state, emitter.BurstCount);
        }
    }

    void ParticleSystem::OnSceneStop(Scene& scene) {
        s_States.erase(&scene);
    }

    void ParticleSystem::Emit(Scene& scene, entt::entity entity, EmitterState& state, uint32_t count) {
        const auto& emitter = scene.GetRegistry().get<ParticleEmitterComponent>(entity);
        if (count == 0)
            return;

        glm::mat4 world = scene.GetWorldTransform(Entity(entity, &scene));
        glm::vec3 origin = glm::vec3(world[3]);
        glm::vec3 axis = glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        axis = glm::length(axis) > 1e-6f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);

        // Base ortonormal (axis, tangent, bitangent) para o cone.
        glm::vec3 helper = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 tangent = glm::normalize(glm::cross(helper, axis));
        glm::vec3 bitangent = glm::cross(axis, tangent);

        float spread = std::min(std::max(emitter.SpreadAngle, 0.0f), 180.0f) * kPi / 180.0f;
        float cosMax = std::cos(spread);
        uint32_t limit = std::min<uint32_t>(std::max<uint32_t>(emitter.MaxParticles, 1u), 10000u);

        for (uint32_t i = 0; i < count; i++) {
            if (state.Particles.size() >= limit)
                break;

            // cos(theta) uniforme entre cosMax e 1 => distribuicao uniforme na calota esferica.
            float cosTheta = 1.0f - Random01(state.Seed) * (1.0f - cosMax);
            float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
            float phi = Random01(state.Seed) * 2.0f * kPi;
            glm::vec3 direction = axis * cosTheta + (tangent * std::cos(phi) + bitangent * std::sin(phi)) * sinTheta;

            Particle particle;
            particle.Position = origin;
            particle.Velocity = direction * RandomRange(state.Seed, emitter.SpeedMin, emitter.SpeedMax);
            particle.Lifetime = std::max(0.01f, RandomRange(state.Seed, emitter.LifetimeMin, emitter.LifetimeMax));
            state.Particles.push_back(particle);
        }
    }

    void ParticleSystem::Simulate(Scene& scene, entt::entity entity, EmitterState& state, float deltaTime) {
        const auto& emitter = scene.GetRegistry().get<ParticleEmitterComponent>(entity);

        // Emissao.
        if (state.Playing && emitter.Enabled) {
            state.Time += deltaTime;
            bool emitting = emitter.Loop || state.Time <= emitter.Duration;
            if (emitting) {
                state.Accumulator += std::max(emitter.EmissionRate, 0.0f) * deltaTime;
                uint32_t count = (uint32_t)state.Accumulator;
                state.Accumulator -= (float)count;
                // Teto por frame: um dt gigante (ex: janela parada) nao pode estourar a memoria.
                Emit(scene, entity, state, std::min<uint32_t>(count, 10000u));
            }
            else if (state.Particles.empty()) {
                state.Playing = false; // acabou a duracao e as ultimas particulas morreram
            }
        }

        // Fisica simples: gravidade + integracao de Euler; remove as mortas (swap-erase).
        float gravity = -kGravity * emitter.GravityScale;
        for (size_t i = 0; i < state.Particles.size();) {
            Particle& particle = state.Particles[i];
            particle.Age += deltaTime;
            if (particle.Age >= particle.Lifetime) {
                particle = state.Particles.back();
                state.Particles.pop_back();
                continue;
            }
            particle.Velocity.y += gravity * deltaTime;
            particle.Position += particle.Velocity * deltaTime;
            i++;
        }
    }

    void ParticleSystem::Update(Scene& scene, float deltaTime) {
        auto sceneIt = s_States.find(&scene);
        if (sceneIt == s_States.end())
            return;

        SceneStates& states = sceneIt->second;

        // Emissores criados durante o Play (Scene.Instantiate etc.) entram em jogo aqui.
        auto view = scene.GetRegistry().view<ParticleEmitterComponent>();
        for (auto handle : view) {
            if (states.find(handle) == states.end()) {
                const auto& emitter = view.get<ParticleEmitterComponent>(handle);
                EmitterState& state = states[handle];
                if (emitter.Enabled && emitter.PlayOnStart) {
                    Start(scene, handle, state);
                    Emit(scene, handle, state, emitter.BurstCount);
                }
            }
        }

        for (auto it = states.begin(); it != states.end();) {
            entt::entity handle = it->first;
            if (!scene.GetRegistry().valid(handle) || !scene.GetRegistry().all_of<ParticleEmitterComponent>(handle)) {
                it = states.erase(it); // entidade destruida (ou component removido)
                continue;
            }
            Simulate(scene, handle, it->second, deltaTime);
            ++it;
        }
    }

    void ParticleSystem::UpdatePreview(Scene& scene, float deltaTime, Entity selected) {
        bool valid = selected.IsValid() && selected.HasComponent<ParticleEmitterComponent>()
            && selected.GetComponent<ParticleEmitterComponent>().Enabled;
        if (!valid) {
            s_States.erase(&scene);
            return;
        }

        SceneStates& states = s_States[&scene];
        entt::entity handle = selected.GetHandle();
        for (auto it = states.begin(); it != states.end();) {
            if (it->first != handle)
                it = states.erase(it); // selecao mudou
            else
                ++it;
        }

        auto found = states.find(handle);
        if (found == states.end()) {
            EmitterState& state = states[handle];
            Start(scene, handle, state);
            Emit(scene, handle, state, selected.GetComponent<ParticleEmitterComponent>().BurstCount);
            found = states.find(handle);
        }

        EmitterState& state = found->second;
        const auto& emitter = selected.GetComponent<ParticleEmitterComponent>();
        // Emissor sem Loop: reinicia quando termina, para o preview nao ficar vazio.
        if (!state.Playing && state.Particles.empty()) {
            Start(scene, handle, state);
            Emit(scene, handle, state, emitter.BurstCount);
        }
        Simulate(scene, handle, state, std::min(deltaTime, 0.1f));
    }

    void ParticleSystem::Play(Entity entity) {
        if (!entity.IsValid() || !entity.HasComponent<ParticleEmitterComponent>())
            return;
        Scene& scene = *entity.GetScene();
        EmitterState& state = s_States[&scene][entity.GetHandle()];
        Start(scene, entity.GetHandle(), state);
        Emit(scene, entity.GetHandle(), state, entity.GetComponent<ParticleEmitterComponent>().BurstCount);
    }

    void ParticleSystem::Stop(Entity entity) {
        auto sceneIt = s_States.find(entity.GetScene());
        if (sceneIt == s_States.end())
            return;
        auto it = sceneIt->second.find(entity.GetHandle());
        if (it != sceneIt->second.end())
            it->second.Playing = false;
    }

    void ParticleSystem::Clear(Entity entity) {
        auto sceneIt = s_States.find(entity.GetScene());
        if (sceneIt == s_States.end())
            return;
        auto it = sceneIt->second.find(entity.GetHandle());
        if (it != sceneIt->second.end()) {
            it->second.Particles.clear();
            it->second.Playing = false;
        }
    }

    bool ParticleSystem::IsPlaying(Entity entity) {
        auto sceneIt = s_States.find(entity.GetScene());
        if (sceneIt == s_States.end())
            return false;
        auto it = sceneIt->second.find(entity.GetHandle());
        return it != sceneIt->second.end() && it->second.Playing;
    }

    uint32_t ParticleSystem::GetAliveCount(Entity entity) {
        auto sceneIt = s_States.find(entity.GetScene());
        if (sceneIt == s_States.end())
            return 0;
        auto it = sceneIt->second.find(entity.GetHandle());
        return it == sceneIt->second.end() ? 0u : (uint32_t)it->second.Particles.size();
    }

    const std::vector<Particle>* ParticleSystem::GetParticles(const Scene& scene, entt::entity entity) {
        auto sceneIt = s_States.find(&scene);
        if (sceneIt == s_States.end())
            return nullptr;
        auto it = sceneIt->second.find(entity);
        return it == sceneIt->second.end() ? nullptr : &it->second.Particles;
    }

}
