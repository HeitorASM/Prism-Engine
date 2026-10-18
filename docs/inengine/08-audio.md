# Áudio

O áudio usa a biblioteca **miniaudio**. Formatos que tocam: **`.wav`, `.mp3` e `.flac`**. O `.ogg` aparece no Conteúdo do Projeto, mas ainda **não toca**: converta antes. Sem placa de som, a engine avisa no console e roda muda.

Arquivos grandes (música) tocam em *streaming*; os pequenos (efeitos) são carregados inteiros na memória.

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
Audio.SetMasterVolume(0.8)
```

## Ainda não existe

Suporte a `.ogg`, canais de volume (música, efeitos, interface) e efeitos como reverb.
