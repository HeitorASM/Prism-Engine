#include "ParticleSystem.h"
#include "../Scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Prism {

    std::unordered_map<const Scene*, ParticleSystem::SceneStates> ParticleSystem::s_States;

    namespace {
        constexpr float kGravity = 9.81f;
        constexpr float kDegToRad = 3.14159265358979f / 180.0f;

        using ParticleMath::Random01;
        using ParticleMath::RandomRange;
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

        const bool local = emitter.SimulationSpace == ParticleSimulationSpace::Local;
        glm::mat4 world = scene.GetWorldTransform(Entity(entity, &scene));
        glm::vec3 fallbackAxis = glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        fallbackAxis = glm::length(fallbackAxis) > 1e-6f ? glm::normalize(fallbackAxis) : glm::vec3(0.0f, 1.0f, 0.0f);

        ParticleMath::ShapeParams shape;
        shape.Type = emitter.Shape;
        shape.Radius = emitter.ShapeRadius;
        shape.Thickness = emitter.ShapeThickness;
        shape.Box[0] = emitter.ShapeBox.x;
        shape.Box[1] = emitter.ShapeBox.y;
        shape.Box[2] = emitter.ShapeBox.z;
        shape.SpreadAngleDeg = emitter.SpreadAngle;
        shape.RadialBlend = emitter.ShapeRadialBlend;

        uint32_t limit = std::min<uint32_t>(std::max<uint32_t>(emitter.MaxParticles, 1u), 10000u);

        for (uint32_t i = 0; i < count; i++) {
            if (state.Particles.size() >= limit)
                break;

            // Forma e direcao no espaco LOCAL do emissor (eixo de emissao = +Y).
            ParticleMath::ShapeSample sample = ParticleMath::SampleShape(shape, state.Seed);
            glm::vec3 localPosition(sample.Position[0], sample.Position[1], sample.Position[2]);
            glm::vec3 localDirection(sample.Direction[0], sample.Direction[1], sample.Direction[2]);

            Particle particle;
            glm::vec3 direction;
            if (local) {
                particle.Position = localPosition;
                direction = localDirection;
            }
            else {
                particle.Position = glm::vec3(world * glm::vec4(localPosition, 1.0f));
                direction = glm::vec3(world * glm::vec4(localDirection, 0.0f));
                direction = glm::length(direction) > 1e-6f ? glm::normalize(direction) : fallbackAxis;
            }
            particle.Velocity = direction * RandomRange(state.Seed, emitter.SpeedMin, emitter.SpeedMax);
            particle.Lifetime = std::max(0.01f, RandomRange(state.Seed, emitter.LifetimeMin, emitter.LifetimeMax));
            particle.Rotation = RandomRange(state.Seed, emitter.RotationMin, emitter.RotationMax) * kDegToRad;
            particle.AngularVelocity = RandomRange(state.Seed, emitter.AngularVelocityMin, emitter.AngularVelocityMax) * kDegToRad;
            particle.SizeScale = 1.0f - std::min(std::max(emitter.SizeVariation, 0.0f), 1.0f) * Random01(state.Seed);
            particle.Random = Random01(state.Seed);
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

        // Gravidade e SEMPRE a do mundo; num emissor Local ela e levada para o espaco local
        // do emissor (inversa da matriz de mundo), uma vez por chamada.
        glm::vec3 gravity(0.0f, -kGravity * emitter.GravityScale, 0.0f);
        if (emitter.SimulationSpace == ParticleSimulationSpace::Local && gravity.y != 0.0f && !state.Particles.empty()) {
            glm::mat4 world = scene.GetWorldTransform(Entity(entity, &scene));
            if (std::abs(glm::determinant(glm::mat3(world))) > 1e-12f)
                gravity = glm::vec3(glm::inverse(world) * glm::vec4(gravity, 0.0f));
            else
                gravity = glm::vec3(0.0f);
        }
        const float dragFactor = emitter.Drag > 0.0f ? std::exp(-emitter.Drag * deltaTime) : 1.0f;

        // Integracao de Euler (+ atrito, curva de velocidade e rotacao); remove as mortas (swap-erase).
        for (size_t i = 0; i < state.Particles.size();) {
            Particle& particle = state.Particles[i];
            particle.Age += deltaTime;
            if (particle.Age >= particle.Lifetime) {
                particle = state.Particles.back();
                state.Particles.pop_back();
                continue;
            }
            const float t = particle.Age / particle.Lifetime;
            particle.Velocity += gravity * deltaTime;
            particle.Velocity *= dragFactor;
            particle.Position += particle.Velocity * (emitter.SpeedOverLife.Evaluate(t) * deltaTime);
            particle.Rotation += particle.AngularVelocity * deltaTime;
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
