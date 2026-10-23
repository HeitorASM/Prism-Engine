# Áudio

Backend: [miniaudio](https://github.com/mackron/miniaudio) (`Prism/src/Prism/Audio/AudioEngine`). A mixagem roda numa thread do próprio miniaudio; a engine só cria e atualiza sons pela thread principal. Sem dispositivo de áudio (ou em CI), a engine avisa no log e roda muda.

**Formatos:** `.wav`, `.mp3`, `.flac` e `.ogg` (Vorbis, via `stb_vorbis` em `vendor/stb`, compilado como C em `Audio/StbVorbisImpl.c`).

Arquivos maiores que 1 MB (música) são tocados em *streaming*. Os menores (efeitos) passam por um **cache de recursos**: `AudioEngine::Preload` registra o clipe no `ma_resource_manager`, que o decodifica uma única vez e o compartilha entre todos os sons. `OnSceneStart` precarrega todos os clipes da cena; o cache compara a data do arquivo (mudou, recarrega) e guarda no máximo 128 clipes (`AudioEngine::ClearCache()` esvazia).

## Canais (buses)

Cada som toca num de quatro canais (`AudioBus`: `SFX`, `Music`, `UI`, `Ambient`), implementados como `ma_sound_group`. `AudioEngine::SetBusVolume(bus, v)` (0 a 2) e `SetBusMuted(bus, muted)` valem para tudo que toca no canal. Volume final = volume do som × volume do canal × volume global. No component, o campo `Bus` (bloco v2 da cena; cenas antigas leem como SFX).

## Components

| Component | Campos |
|---|---|
| **Audio Source** | `Clip` (asset de áudio), `Volume` (0–2), `Pitch` (0,1–4), `Loop`, `PlayOnStart`, `Spatial` (3D), `MinDistance`, `MaxDistance`, `Bus` (canal) |
| **Audio Listener** | `Active` |

- No editor: arraste o áudio do painel *Conteúdo do Projeto* para o campo **Clip**. O botão **Ouvir** toca o arquivo sem entrar no Play.
- Som 3D: o volume cai **linearmente** entre `MinDistance` (volume cheio) e `MaxDistance` (mudo). Sem `Spatial`, o som é 2D (música, interface).
- O "ouvido" é a entidade com **Audio Listener** ativo; sem nenhuma, vale a câmera primária.
- O som só toca durante o **Play**: `Scene::OnScriptsStart/OnUpdate/OnScriptsStop` chamam `AudioEngine::OnSceneStart/OnSceneUpdate/OnSceneStop`. Cada Scene tem seus próprios sons.

## API Lua

```lua
-- Entidade com Audio Source
entity:PlaySound()            -- (re)começa do início
entity:StopSound()
entity:IsSoundPlaying()
entity:SetSoundVolume(0.5)
entity:SetSoundPitch(1.2)

-- Sons soltos (caminho relativo à pasta do projeto)
Audio.Play("Assets/Sounds/tiro.wav")
Audio.Play("Assets/Sounds/tiro.wav", 0.5, 1.2)           -- volume, pitch
Audio.Play3D("Assets/Sounds/boom.wav", x, y, z, 1.0)     -- numa posição do mundo
Audio.Play("Assets/Music/tema.ogg", 0.8, 1.0, "music")   -- 4º argumento: canal
Audio.Preload("Assets/Sounds/tiro.wav")
Audio.SetMasterVolume(0.8)
Audio.GetMasterVolume()

-- canais "sfx" (padrão), "music", "ui", "ambient"
Audio.SetBusVolume("music", 0.5)
Audio.GetBusVolume("music")
Audio.SetBusMuted("sfx", true)
Audio.IsBusMuted("sfx")
entity:GetAudioSource().Bus = "ambient"
```

## Próximos passos

- Reverb e filtros; salvar o volume dos canais nas configurações do projeto.
