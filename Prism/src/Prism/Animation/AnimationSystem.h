#pragma once

// ============================================================================
// AnimationSystem.h
// Roda os AnimatorComponent: avanca o tempo, mistura clipes (crossfade) e
// calcula a paleta de ossos que o Renderer envia ao shader (skinning.glsl).
//
//   - Scene::OnUpdate chama Update() todo frame. Durante o Play atualiza todos
//     os Animators; no editor (sem Play) so os que estao em PREVIA.
//   - Scene::OnScriptsStart chama OnSceneStart() (PlayOnStart).
//   - Os scripts (Lua/C++) usam Play/Stop/Pause/... pelas entidades.
//   - O Renderer pergunta a paleta com FindPalette().
//
// Estado de execucao fica no AnimatorComponent::Runtime (nao e salvo).
// ============================================================================

#include "../Scene/Entity.h"
#include "../Assets/AssetID.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Prism {

    class Scene;

    class AnimationSystem {
    public:
        // Inicio do Play: toca o 'Clip' dos Animators com PlayOnStart.
        static void OnSceneStart(Scene& scene);

        // Avanca os Animators. 'editing' = true (cena parada no editor): so
        // os que estao em previa (Playing) avancam.
        static void Update(Scene& scene, float deltaTime, bool editing);

        // Toca 'clip' (por nome). fadeSeconds > 0: mistura a partir da pose
        // atual. loop: -1 = usa AnimatorComponent::Loop, 0/1 = forca. Tocar o
        // clipe que ja esta tocando nao reinicia (retorna true). false = sem
        // Animator, sem modelo/rig ou clipe inexistente (avisa no log).
        static bool Play(Entity entity, const std::string& clip, float fadeSeconds = 0.0f, int loop = -1);

        // --- Maquina de estados (AnimatorComponent::Graph) ---
        // Entra no estado de entrada do grafo (retoma o grafo). false = sem grafo/modelo.
        static bool StartGraph(Entity entity);
        // Parametros por nome. false = sem Animator/grafo ou parametro inexistente (avisa 1x).
        static bool SetFloat(Entity entity, const std::string& param, float value);
        static bool SetBool(Entity entity, const std::string& param, bool value);
        static bool SetTrigger(Entity entity, const std::string& param);
        static float GetFloat(Entity entity, const std::string& param); // Float/Bool/Trigger como numero
        // Vai direto para o estado (por nome), com crossfade 'fadeSeconds'. Retoma o grafo.
        static bool SetState(Entity entity, const std::string& state, float fadeSeconds = 0.0f);
        static std::string GetState(Entity entity); // "" se o grafo nao esta ativo

        // Volta a pose de bind e para.
        static void Stop(Entity entity);

        static void SetPaused(Entity entity, bool paused);
        static void SetSpeed(Entity entity, float speed);

        static bool IsPlaying(Entity entity);               // tocando (nao pausado, nao terminado)
        static std::string GetCurrentClip(Entity entity);   // "" se nenhum
        static float GetTime(Entity entity);                // segundos dentro do clipe
        static float GetDuration(Entity entity);            // do clipe atual (0 se nenhum)
        static std::vector<std::string> GetClipNames(Entity entity); // clipes do modelo do Animator

        // Nomes dos clipes do modelo (para a UI do editor).
        static std::vector<std::string> GetClipNames(AssetID modelAsset);

        // Paleta do Animator da entidade 'entity' ou de um ancestral, desde que
        // seja do modelo 'modelAsset'. nullptr = sem Animator (usa pose de bind).
        static const std::vector<glm::mat4>* FindPalette(Scene& scene, entt::entity entity, AssetID modelAsset);
    };

}
