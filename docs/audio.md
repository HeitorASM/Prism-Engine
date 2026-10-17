# Áudio

Backend: [miniaudio](https://github.com/mackron/miniaudio) (`Prism/src/Prism/Audio/AudioEngine`). A mixagem roda numa thread do próprio miniaudio; a engine só cria e atualiza sons pela thread principal. Sem dispositivo de áudio (ou em CI), a engine avisa no log e roda muda.

**Formatos:** `.wav`, `.mp3`, `.flac`. `.ogg` aparece no Content Browser como áudio, mas ainda **não toca** (exigiria compilar o `stb_vorbis` em C junto do miniaudio; veja o roadmap). Converta para `.wav`/`.flac`/`.mp3` por enquanto.

Arquivos maiores que 1 MB (música) são tocados em *streaming*; os menores são decodificados inteiros na memória (efeitos sonoros).

## Components

| Component | Campos |
|---|---|
| **Audio Source** | `Clip` (asset de áudio), `Volume` (0–2), `Pitch` (0,1–4), `Loop`, `PlayOnStart`, `Spatial` (3D), `MinDistance`, `MaxDistance` |
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
Audio.SetMasterVolume(0.8)
Audio.GetMasterVolume()
```

## Próximos passos

- Suporte a `.ogg` (stb_vorbis), grupos/canais de volume (música, efeitos, interface), reverb/efeitos.
