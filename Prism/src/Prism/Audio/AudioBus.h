#pragma once

// ============================================================================
// AudioBus.h
// Canais ("buses") de volume do audio: cada som toca em UM bus, e cada bus tem
// volume e mudo proprios (AudioEngine::SetBusVolume / SetBusMuted). O volume
// final de um som = Volume do som x volume do bus x volume global.
// Header leve (sem miniaudio) para poder ser usado em Components.h.
// ============================================================================

#include <cstdint>
#include <string>

namespace Prism {

    enum class AudioBus : int32_t {
        SFX = 0,     // efeitos (padrao)
        Music = 1,
        UI = 2,
        Ambient = 3, // ambiente / loops de fundo
    };

    constexpr int kAudioBusCount = 4;

    inline const char* AudioBusName(AudioBus bus) {
        switch (bus) {
            case AudioBus::SFX: return "sfx";
            case AudioBus::Music: return "music";
            case AudioBus::UI: return "ui";
            case AudioBus::Ambient: return "ambient";
        }
        return "sfx";
    }

    // Nomes aceitos (sem diferenciar maiusculas): sfx, music, ui, ambient.
    inline bool AudioBusFromName(const std::string& name, AudioBus& out) {
        std::string lower;
        lower.reserve(name.size());
        for (char c : name)
            lower.push_back((c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c);
        for (int i = 0; i < kAudioBusCount; ++i) {
            if (lower == AudioBusName((AudioBus)i)) {
                out = (AudioBus)i;
                return true;
            }
        }
        return false;
    }

    inline bool IsValidAudioBus(int32_t value) {
        return value >= 0 && value < kAudioBusCount;
    }

}
