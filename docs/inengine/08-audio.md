# Áudio

O áudio usa a biblioteca **miniaudio**. Formatos que tocam: **`.wav`, `.mp3`, `.ogg` (Vorbis) e `.flac`**. Sem placa de som, a engine avisa no console e roda muda.

Arquivos grandes (mais de 1 MB, música) tocam em *streaming*. Os pequenos (efeitos) são decodificados **uma vez** e ficam em cache: todo som que usa o arquivo compartilha os mesmos dados, e ao dar Play a engine já carrega os clipes da cena (o primeiro tiro não engasga). Se você regravar o arquivo, o cache percebe e recarrega.

## Components

**Audio Source** (uma fonte de som):

| Campo | O que é |
|---|---|
| **Clip** | O arquivo de áudio. Arraste do Conteúdo do Projeto para o campo |
| **Volume** | 0 a 2 |
| **Pitch** | 0,1 a 4 (velocidade/tom) |
| **Loop** | Repete |
| **PlayOnStart** | Toca sozinho ao iniciar o Play |
| **Spatial** | Som 3D: o volume cai conforme a distância |
| **MinDistance / MaxDistance** | Volume cheio até `Min`, caindo linearmente até mudo em `Max` |
| **Canal (bus)** | Efeitos (SFX), Música, Interface (UI) ou Ambiente. Cada canal tem volume e mudo próprios |

O botão **Ouvir** toca o arquivo no editor, sem precisar dar Play.

**Audio Listener**: marca quem "ouve". Sem nenhum ativo, vale a câmera primária.

Sem **Spatial**, o som é 2D (música, interface). O áudio só toca durante o **Play**.

## Por script

```lua
-- numa entidade com Audio Source
entity:PlaySound()                 -- (re)começa do início
entity:StopSound()
entity:SetSoundVolume(0.5)
entity:SetSoundPitch(1.2)

-- sons soltos (caminho relativo ao projeto)
Audio.Play("Assets/Audio/tiro.wav")
Audio.Play("Assets/Audio/tiro.wav", 0.5, 1.2)           -- volume, pitch
Audio.Play3D("Assets/Audio/boom.wav", x, y, z, 1.0)     -- numa posição do mundo
Audio.Play("Assets/Audio/tema.ogg", 0.8, 1.0, "music") -- no canal "music"
Audio.Preload("Assets/Audio/tiro.wav")                  -- carrega antes (sem engasgo)
Audio.SetMasterVolume(0.8)

-- canais: "sfx" (padrão), "music", "ui", "ambient"
Audio.SetBusVolume("music", 0.5)     -- 0 a 2
Audio.GetBusVolume("music")
Audio.SetBusMuted("sfx", true)
Audio.IsBusMuted("sfx")
entity:GetAudioSource().Bus = "ambient"   -- vale na próxima vez que o som começar
```

Volume final = volume do som × volume do canal × volume geral.

## Ainda não existe

Efeitos como reverb e filtros.
