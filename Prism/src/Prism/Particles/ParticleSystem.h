#pragma once

// ============================================================================
// ParticleSystem.h
// Simulacao (CPU) dos emissores de particulas. O estado das particulas e
// RUNTIME: nunca vai para o arquivo de cena, e vive aqui, por Scene, ligado a
// entidade dona do ParticleEmitterComponent. A configuracao do emissor mora
// no component (ver Components.h).
//
//  - Play: Scene::OnScriptsStart -> OnSceneStart; Scene::OnUpdate -> Update;
//    Scene::OnScriptsStop -> OnSceneStop.
//  - Editor (fora do Play): UpdatePreview anima so a entidade selecionada,
//    para ajustar o emissor vendo o resultado.
//  - Desenho: Renderer::DrawParticles le as particulas por GetParticles.
// ============================================================================

#include "../Scene/Entity.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace Prism {

    struct Particle {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        float Age = 0.0f;
        float Lifetime = 1.0f;
    };

    class ParticleSystem {
    public:
        // Zera o estado da cena e comeca a emitir os emissores com PlayOnStart.
        static void OnSceneStart(Scene& scene);
        // Libera o estado da cena.
        static void OnSceneStop(Scene& scene);

        // Simula todos os emissores em reproducao (modo Play).
        static void Update(Scene& scene, float deltaTime);

        // Editor: anima SO 'selected' (se tiver emissor habilitado); qualquer
        // outro estado da cena e descartado. Entidade invalida = limpa tudo.
        static void UpdatePreview(Scene& scene, float deltaTime, Entity selected);

        // Controle por script: Play reinicia (e emite o burst); Stop para de
        // emitir mas deixa as particulas vivas terminarem; Clear remove tudo.
        static void Play(Entity entity);
        static void Stop(Entity entity);
        static void Clear(Entity entity);
        static bool IsPlaying(Entity entity);
        static uint32_t GetAliveCount(Entity entity);

        // Particulas vivas do emissor (nullptr se nao houver estado).
        static const std::vector<Particle>* GetParticles(const Scene& scene, entt::entity entity);

    private:
        struct EmitterState {
            std::vector<Particle> Particles;
            float Accumulator = 0.0f;
            float Time = 0.0f;
            bool Playing = false;
            uint32_t Seed = 0x9E3779B9u;
        };
        using SceneStates = std::unordered_map<entt::entity, EmitterState>;

        static void Start(Scene& scene, entt::entity entity, EmitterState& state);
        static void Simulate(Scene& scene, entt::entity entity, EmitterState& state, float deltaTime);
        static void Emit(Scene& scene, entt::entity entity, EmitterState& state, uint32_t count);

        static std::unordered_map<const Scene*, SceneStates> s_States;
    };

}
