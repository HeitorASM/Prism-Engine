#pragma once

// ============================================================================
// AudioEngine.h
// Audio da engine, em cima do miniaudio (ma_engine): mixagem e saida pelo
// dispositivo padrao ficam em uma thread propria do miniaudio; a engine so
// cria/atualiza/destroi sons a partir da thread principal.
//
// Tres usos:
//   1. AudioSourceComponent (por entidade) - tocado durante o modo Play.
//      Scene::OnScriptsStart/OnUpdate/OnScriptsStop chamam OnSceneStart/
//      OnSceneUpdate/OnSceneStop. Cada Scene (a de edicao e a clone do Play)
//      tem os seus proprios sons, identificados por (Scene*, entidade).
//   2. Sons soltos (PlayOneShot) - efeitos disparados por script (Audio.Play).
//   3. Preview (PlayPreview) - o botao "Ouvir" do editor, fora do Play.
//
// Sem dispositivo de audio (maquina sem placa de som, CI), Init() so avisa e
// todas as chamadas viram no-ops - o resto da engine nao precisa checar nada.
// ============================================================================

#include "AudioBus.h"

#include <glm/glm.hpp>
#include <filesystem>

namespace Prism {

    class Scene;
    class Entity;

    struct AudioPlayOptions {
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        bool Spatial = false;                 // true: usa Position/MinDistance/MaxDistance
        glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
        float MinDistance = 1.0f;
        float MaxDistance = 25.0f;
        AudioBus Bus = AudioBus::SFX;
    };

    class AudioEngine {
    public:
        // Chamados por Application (Init junto do resto da fundacao da engine).
        static void Init();
        static void Shutdown();
        static bool IsInitialized();

        // Volume global (0..2), aplicado a tudo.
        static void SetMasterVolume(float volume);
        static float GetMasterVolume();

        // --- Buses (canais de volume) ---------------------------------------
        // Volume final = volume do som x volume do bus x volume global.
        static void SetBusVolume(AudioBus bus, float volume); // 0..2
        static float GetBusVolume(AudioBus bus);
        static void SetBusMuted(AudioBus bus, bool muted);
        static bool IsBusMuted(AudioBus bus);

        // --- Cache de recursos ----------------------------------------------
        // Efeitos curtos (ate 1 MB) sao decodificados UMA vez e compartilhados por
        // todos os sons que usam o arquivo (sem ler/decodificar a cada disparo).
        // Preload evita o engasgo do primeiro disparo; o cache percebe arquivo
        // alterado em disco (recarrega) e e limitado a poucas centenas de entradas.
        static void Preload(const std::filesystem::path& absolutePath);
        static void ClearCache();
        static size_t GetCachedClipCount();

        // --- Sons soltos ----------------------------------------------------
        // Toca o arquivo (caminho ABSOLUTO) uma vez e libera sozinho ao terminar.
        static void PlayOneShot(const std::filesystem::path& absolutePath, const AudioPlayOptions& options = {});

        // --- Preview do editor (um por vez) -------------------------------
        static void PlayPreview(const std::filesystem::path& absolutePath);
        static void StopPreview();
        static bool IsPreviewPlaying();

        // --- AudioSourceComponent ------------------------------------------
        // Ciclo de vida acompanha Scene::OnScriptsStart/OnUpdate/OnScriptsStop.
        static void OnSceneStart(Scene& scene);   // toca as fontes com PlayOnStart
        static void OnSceneUpdate(Scene& scene);  // listener, posicao/volume/pitch, limpeza
        static void OnSceneStop(Scene& scene);    // para e libera tudo da cena

        // Controle individual (usado pelo Lua: entity:PlaySound() etc).
        static void PlayEntity(Entity entity);    // (re)comeca do inicio
        static void StopEntity(Entity entity);
        static bool IsEntityPlaying(Entity entity);
    };

}
