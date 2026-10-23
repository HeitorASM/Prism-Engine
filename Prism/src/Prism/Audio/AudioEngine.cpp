// NOMINMAX antes de qualquer include: no Windows o miniaudio puxa windows.h,
// cujas macros min/max quebram std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Implementacao do miniaudio: EXATAMENTE neste .cpp do projeto inteiro (ver
// vendor/CMakeLists.txt). Encoding e geracao de ondas ficam de fora - a
// engine so toca arquivos. Decoders embutidos: WAV, MP3 e FLAC; OGG Vorbis vem
// do stb_vorbis (vendor/stb): aqui entram so as DECLARACOES (o miniaudio detecta
// STB_VORBIS_INCLUDE_STB_VORBIS_H); a implementacao e compilada como C em
// StbVorbisImpl.c.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "AudioEngine.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Project/Project.h"
#include "../Core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace Prism {

    namespace {

        // Chave de uma fonte tocando: a Scene (a de edicao e a clone do Play
        // podem coexistir) + o handle da entidade.
        struct SourceKey {
            const Scene* SceneKey;
            uint32_t EntityHandle;
            bool operator==(const SourceKey& other) const {
                return SceneKey == other.SceneKey && EntityHandle == other.EntityHandle;
            }
        };
        struct SourceKeyHash {
            size_t operator()(const SourceKey& key) const {
                return std::hash<const void*>()(key.SceneKey) ^ (std::hash<uint32_t>()(key.EntityHandle) << 1);
            }
        };

        ma_engine s_Engine;
        bool s_Initialized = false;
        float s_MasterVolume = 1.0f;

        // Um ma_sound_group por bus: todo som toca dentro do grupo do seu bus.
        ma_sound_group s_BusGroups[kAudioBusCount];
        bool s_BusGroupReady[kAudioBusCount] = {};
        float s_BusVolume[kAudioBusCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool s_BusMuted[kAudioBusCount] = {};

        int BusIndex(AudioBus bus) {
            return IsValidAudioBus((int32_t)bus) ? (int)bus : 0;
        }

        void ApplyBusVolume(int index) {
            if (s_BusGroupReady[index])
                ma_sound_group_set_volume(&s_BusGroups[index], s_BusMuted[index] ? 0.0f : s_BusVolume[index]);
        }

        // Cache de clipes curtos (ver AudioEngine::Preload): o proprio
        // ma_resource_manager guarda o PCM decodificado; aqui so lembramos o que
        // registramos (e a data do arquivo) para recarregar quando mudar.
        struct CacheEntry {
            std::filesystem::path Path;
            std::filesystem::file_time_type WriteTime;
        };
        std::vector<CacheEntry> s_Cache; // do mais antigo ao mais novo
        constexpr size_t kMaxCacheEntries = 128;

        void UnregisterFromManager(const std::filesystem::path& path) {
#ifdef _WIN32
            ma_resource_manager_unregister_file_w(ma_engine_get_resource_manager(&s_Engine), path.c_str());
#else
            ma_resource_manager_unregister_file(ma_engine_get_resource_manager(&s_Engine), path.c_str());
#endif
        }

        // Garante o clipe curto decodificado no cache e atualizado. false = nao entrou no cache.
        bool EnsureCached(const std::filesystem::path& path) {
            std::error_code ec;
            auto writeTime = std::filesystem::last_write_time(path, ec);
            if (ec)
                return false;

            for (size_t i = 0; i < s_Cache.size(); ++i) {
                if (s_Cache[i].Path != path)
                    continue;
                if (s_Cache[i].WriteTime == writeTime)
                    return true;
                UnregisterFromManager(path); // arquivo mudou: descarta e recarrega abaixo
                s_Cache.erase(s_Cache.begin() + (std::ptrdiff_t)i);
                break;
            }

            if (s_Cache.size() >= kMaxCacheEntries) {
                UnregisterFromManager(s_Cache.front().Path);
                s_Cache.erase(s_Cache.begin());
            }

#ifdef _WIN32
            ma_result result = ma_resource_manager_register_file_w(ma_engine_get_resource_manager(&s_Engine), path.c_str(), MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
#else
            ma_result result = ma_resource_manager_register_file(ma_engine_get_resource_manager(&s_Engine), path.c_str(), MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
#endif
            if (result != MA_SUCCESS)
                return false;
            s_Cache.push_back({ path, writeTime });
            return true;
        }

        std::unordered_map<SourceKey, std::unique_ptr<ma_sound>, SourceKeyHash> s_Sources;
        std::vector<std::unique_ptr<ma_sound>> s_OneShots;
        std::unique_ptr<ma_sound> s_Preview;

        SourceKey MakeKey(const Scene* scene, entt::entity handle) {
            return SourceKey{ scene, (uint32_t)handle };
        }

        // Arquivos grandes (musica) sao decodificados em streaming; curtos
        // (efeitos) inteiros na memoria, para nao ler o disco a cada loop.
        constexpr std::uintmax_t kStreamThresholdBytes = 1024 * 1024;

        // Cria e comeca a tocar um som. Devolve nullptr em falha (erro logado).
        std::unique_ptr<ma_sound> StartSound(const std::filesystem::path& path, const AudioPlayOptions& options) {
            if (!s_Initialized)
                return nullptr;

            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                PRISM_CORE_WARN("AudioEngine: arquivo de audio nao encontrado: ", path.string());
                return nullptr;
            }

            ma_uint32 flags = 0;
            std::uintmax_t size = std::filesystem::file_size(path, ec);
            const bool stream = (!ec && size > kStreamThresholdBytes);
            flags |= stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
            if (!stream)
                EnsureCached(path); // clipe curto: decodificado uma vez e compartilhado
            if (!options.Spatial)
                flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

            const int busIndex = BusIndex(options.Bus);
            ma_sound_group* group = s_BusGroupReady[busIndex] ? &s_BusGroups[busIndex] : nullptr;
            auto sound = std::make_unique<ma_sound>();
#ifdef _WIN32
            ma_result result = ma_sound_init_from_file_w(&s_Engine, path.c_str(), flags, group, nullptr, sound.get());
#else
            ma_result result = ma_sound_init_from_file(&s_Engine, path.c_str(), flags, group, nullptr, sound.get());
#endif
            if (result != MA_SUCCESS) {
                PRISM_CORE_WARN("AudioEngine: nao foi possivel carregar '", path.string(), "' (", ma_result_description(result),
                    "). Formatos suportados: .wav, .mp3, .flac, .ogg.");
                return nullptr;
            }

            ma_sound_set_looping(sound.get(), options.Loop ? MA_TRUE : MA_FALSE);
            ma_sound_set_volume(sound.get(), std::clamp(options.Volume, 0.0f, 2.0f));
            ma_sound_set_pitch(sound.get(), std::clamp(options.Pitch, 0.1f, 4.0f));
            if (options.Spatial) {
                float minDistance = (std::max)(options.MinDistance, 0.01f);
                float maxDistance = (std::max)(options.MaxDistance, minDistance + 0.01f);
                ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_linear);
                ma_sound_set_min_distance(sound.get(), minDistance);
                ma_sound_set_max_distance(sound.get(), maxDistance);
                ma_sound_set_position(sound.get(), options.Position.x, options.Position.y, options.Position.z);
            }
            ma_sound_start(sound.get());
            return sound;
        }

        void DestroySound(std::unique_ptr<ma_sound>& sound) {
            if (!sound)
                return;
            ma_sound_uninit(sound.get());
            sound.reset();
        }

        std::filesystem::path ResolveClip(AssetID clip) {
            if (!clip.IsValid())
                return {};
            auto project = Project::GetActive();
            if (!project)
                return {};
            return project->GetAssetRegistry().AbsolutePath(clip);
        }

        AudioPlayOptions OptionsFor(const AudioSourceComponent& source, const glm::vec3& position) {
            AudioPlayOptions options;
            options.Volume = source.Volume;
            options.Pitch = source.Pitch;
            options.Loop = source.Loop;
            options.Spatial = source.Spatial;
            options.Position = position;
            options.MinDistance = source.MinDistance;
            options.MaxDistance = source.MaxDistance;
            options.Bus = source.Bus;
            return options;
        }

        glm::vec3 WorldPosition(Scene& scene, Entity entity) {
            return glm::vec3(scene.GetWorldTransform(entity)[3]);
        }

        void StartSource(Scene& scene, Entity entity) {
            auto& source = entity.GetComponent<AudioSourceComponent>();
            SourceKey key = MakeKey(&scene, entity.GetHandle());

            auto existing = s_Sources.find(key);
            if (existing != s_Sources.end()) {
                DestroySound(existing->second);
                s_Sources.erase(existing);
            }

            std::filesystem::path path = ResolveClip(source.Clip);
            if (path.empty())
                return; // sem clip (ou asset sumiu): nada a tocar

            auto sound = StartSound(path, OptionsFor(source, WorldPosition(scene, entity)));
            if (sound)
                s_Sources[key] = std::move(sound);
        }

        void UpdateListener(Scene& scene) {
            Entity listener;
            for (auto handle : scene.GetRegistry().view<AudioListenerComponent>()) {
                if (scene.GetRegistry().get<AudioListenerComponent>(handle).Active) {
                    listener = Entity(handle, &scene);
                    break;
                }
            }
            if (!listener) {
                for (auto handle : scene.GetRegistry().view<CameraComponent>()) {
                    if (scene.GetRegistry().get<CameraComponent>(handle).Primary) {
                        listener = Entity(handle, &scene);
                        break;
                    }
                }
            }

            glm::vec3 position(0.0f), forward(0.0f, 0.0f, -1.0f), up(0.0f, 1.0f, 0.0f);
            if (listener) {
                glm::mat4 world = scene.GetWorldTransform(listener);
                glm::mat3 rotation(world);
                position = glm::vec3(world[3]);
                forward = glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
                up = glm::normalize(rotation * glm::vec3(0.0f, 1.0f, 0.0f));
            }
            ma_engine_listener_set_position(&s_Engine, 0, position.x, position.y, position.z);
            ma_engine_listener_set_direction(&s_Engine, 0, forward.x, forward.y, forward.z);
            ma_engine_listener_set_world_up(&s_Engine, 0, up.x, up.y, up.z);
        }

    }

    void AudioEngine::Init() {
        if (s_Initialized)
            return;
        ma_engine_config config = ma_engine_config_init();
        ma_result result = ma_engine_init(&config, &s_Engine);
        if (result != MA_SUCCESS) {
            PRISM_CORE_WARN("AudioEngine: nao foi possivel abrir um dispositivo de audio (", ma_result_description(result),
                ") - o jogo roda sem som.");
            return;
        }
        s_Initialized = true;
        ma_engine_set_volume(&s_Engine, s_MasterVolume);
        for (int i = 0; i < kAudioBusCount; ++i) {
            s_BusGroupReady[i] = ma_sound_group_init(&s_Engine, 0, nullptr, &s_BusGroups[i]) == MA_SUCCESS;
            ApplyBusVolume(i);
        }
        PRISM_CORE_INFO("AudioEngine: miniaudio inicializado (", ma_engine_get_sample_rate(&s_Engine), " Hz).");
    }

    void AudioEngine::Shutdown() {
        if (!s_Initialized)
            return;
        for (auto& [key, sound] : s_Sources)
            DestroySound(sound);
        s_Sources.clear();
        for (auto& sound : s_OneShots)
            DestroySound(sound);
        s_OneShots.clear();
        DestroySound(s_Preview);
        for (int i = 0; i < kAudioBusCount; ++i) {
            if (s_BusGroupReady[i])
                ma_sound_group_uninit(&s_BusGroups[i]);
            s_BusGroupReady[i] = false;
        }
        s_Cache.clear(); // o resource manager libera tudo junto com o engine
        ma_engine_uninit(&s_Engine);
        s_Initialized = false;
    }

    bool AudioEngine::IsInitialized() {
        return s_Initialized;
    }

    void AudioEngine::SetMasterVolume(float volume) {
        s_MasterVolume = std::clamp(volume, 0.0f, 2.0f);
        if (s_Initialized)
            ma_engine_set_volume(&s_Engine, s_MasterVolume);
    }

    float AudioEngine::GetMasterVolume() {
        return s_MasterVolume;
    }

    void AudioEngine::SetBusVolume(AudioBus bus, float volume) {
        const int index = BusIndex(bus);
        s_BusVolume[index] = std::clamp(std::isfinite(volume) ? volume : 1.0f, 0.0f, 2.0f);
        ApplyBusVolume(index);
    }

    float AudioEngine::GetBusVolume(AudioBus bus) {
        return s_BusVolume[BusIndex(bus)];
    }

    void AudioEngine::SetBusMuted(AudioBus bus, bool muted) {
        const int index = BusIndex(bus);
        s_BusMuted[index] = muted;
        ApplyBusVolume(index);
    }

    bool AudioEngine::IsBusMuted(AudioBus bus) {
        return s_BusMuted[BusIndex(bus)];
    }

    void AudioEngine::Preload(const std::filesystem::path& absolutePath) {
        if (!s_Initialized || absolutePath.empty())
            return;
        std::error_code ec;
        std::uintmax_t size = std::filesystem::file_size(absolutePath, ec);
        if (ec || size > kStreamThresholdBytes)
            return; // musica grande: toca em streaming, nao entra no cache
        EnsureCached(absolutePath);
    }

    void AudioEngine::ClearCache() {
        if (!s_Initialized)
            return;
        for (const CacheEntry& entry : s_Cache)
            UnregisterFromManager(entry.Path);
        s_Cache.clear();
    }

    size_t AudioEngine::GetCachedClipCount() {
        return s_Cache.size();
    }

    void AudioEngine::PlayOneShot(const std::filesystem::path& absolutePath, const AudioPlayOptions& options) {
        auto sound = StartSound(absolutePath, options);
        if (sound)
            s_OneShots.push_back(std::move(sound));
    }

    void AudioEngine::PlayPreview(const std::filesystem::path& absolutePath) {
        StopPreview();
        s_Preview = StartSound(absolutePath, AudioPlayOptions{});
    }

    void AudioEngine::StopPreview() {
        DestroySound(s_Preview);
    }

    bool AudioEngine::IsPreviewPlaying() {
        if (!s_Preview)
            return false;
        if (ma_sound_at_end(s_Preview.get())) {
            DestroySound(s_Preview); // terminou sozinho
            return false;
        }
        return true;
    }

    void AudioEngine::OnSceneStart(Scene& scene) {
        if (!s_Initialized)
            return;
        UpdateListener(scene);
        auto view = scene.GetRegistry().view<AudioSourceComponent>();
        // Carrega antes todos os clipes curtos da cena: o primeiro disparo no meio do
        // jogo (tiro, passo) nao decodifica nada.
        for (auto handle : view)
            Preload(ResolveClip(view.get<AudioSourceComponent>(handle).Clip));
        for (auto handle : view) {
            if (view.get<AudioSourceComponent>(handle).PlayOnStart)
                StartSource(scene, Entity(handle, &scene));
        }
    }

    void AudioEngine::OnSceneUpdate(Scene& scene) {
        if (!s_Initialized)
            return;

        UpdateListener(scene);

        // Fontes desta Scene: aplica os valores atuais do component (o script
        // pode ter mudado volume/pitch/posicao) e descarta as que acabaram ou
        // cuja entidade/component sumiu.
        for (auto it = s_Sources.begin(); it != s_Sources.end();) {
            if (it->first.SceneKey != &scene) {
                ++it;
                continue;
            }
            entt::entity handle = (entt::entity)it->first.EntityHandle;
            auto& registry = scene.GetRegistry();
            bool alive = registry.valid(handle) && registry.all_of<AudioSourceComponent>(handle);
            if (alive) {
                ma_sound* sound = it->second.get();
                auto& source = registry.get<AudioSourceComponent>(handle);
                ma_sound_set_volume(sound, std::clamp(source.Volume, 0.0f, 2.0f));
                ma_sound_set_pitch(sound, std::clamp(source.Pitch, 0.1f, 4.0f));
                ma_sound_set_looping(sound, source.Loop ? MA_TRUE : MA_FALSE);
                if (source.Spatial) {
                    glm::vec3 position = WorldPosition(scene, Entity(handle, &scene));
                    ma_sound_set_position(sound, position.x, position.y, position.z);
                }
                alive = !ma_sound_at_end(sound);
            }
            if (!alive) {
                DestroySound(it->second);
                it = s_Sources.erase(it);
            }
            else {
                ++it;
            }
        }

        // Sons soltos que terminaram.
        s_OneShots.erase(std::remove_if(s_OneShots.begin(), s_OneShots.end(), [](std::unique_ptr<ma_sound>& sound) {
            if (!ma_sound_at_end(sound.get()))
                return false;
            DestroySound(sound);
            return true;
        }), s_OneShots.end());
    }

    void AudioEngine::OnSceneStop(Scene& scene) {
        for (auto it = s_Sources.begin(); it != s_Sources.end();) {
            if (it->first.SceneKey == &scene) {
                DestroySound(it->second);
                it = s_Sources.erase(it);
            }
            else {
                ++it;
            }
        }
        // Sons soltos nao pertencem a Scene nenhuma; quando nao ha mais nada
        // rodando eles terminam sozinhos (ou param no Shutdown).
    }

    void AudioEngine::PlayEntity(Entity entity) {
        if (!s_Initialized || !entity || !entity.HasComponent<AudioSourceComponent>())
            return;
        StartSource(*entity.GetScene(), entity);
    }

    void AudioEngine::StopEntity(Entity entity) {
        if (!entity || !entity.GetScene())
            return;
        auto it = s_Sources.find(MakeKey(entity.GetScene(), entity.GetHandle()));
        if (it == s_Sources.end())
            return;
        DestroySound(it->second);
        s_Sources.erase(it);
    }

    bool AudioEngine::IsEntityPlaying(Entity entity) {
        if (!entity || !entity.GetScene())
            return false;
        auto it = s_Sources.find(MakeKey(entity.GetScene(), entity.GetHandle()));
        return it != s_Sources.end() && !ma_sound_at_end(it->second.get());
    }

}
