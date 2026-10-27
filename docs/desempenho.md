# Desempenho e profiler

**Janela → Desempenho (profiler)** abre o painel. Enquanto ele está aberto o medidor fica ligado; ao fechar, desliga (fora disso cada zona custa só a leitura de um `bool`).

## O que mostra

- **Tempo de quadro**: FPS, média, último e pior recente, com o gráfico dos últimos 240 quadros. O tempo inclui a espera do V-Sync (zona `Window.Swap`), então um jogo "limitado a 60" mostra ~16,7 ms sem estar lento.
- **Contadores** do último quadro: chamadas de desenho e triângulos (cena e sombras), e quantas malhas foram desenhadas, descartadas pelo frustum ou escondidas por camada.
- **Zonas de CPU**: `Layers.OnUpdate`, `ImGui`, `Window.Swap`, `Scene.Physics`, `Scene.ScriptsLua`, `Scene.ScriptsCpp`, `Scene.Particles`, `Scene.Animation`, `Renderer.DrawScene` (com `Renderer.Shadows`, `Renderer.SSAO`, `Renderer.Particles` dentro).
- **Zonas de GPU** (`GPU.DrawScene`, `GPU.Shadows`, `GPU.SSAO`, `GPU.Particles`): medidas com consultas de timestamp do OpenGL e lidas só quando prontas, por isso chegam com ~3 quadros de atraso e nunca travam a CPU. Medem só a janela principal do editor (a janela de Play tem outro contexto GL).

Zonas aninhadas se sobrepõem: o tempo de `Renderer.Shadows` já está dentro de `Renderer.DrawScene`. CPU e GPU rodam em paralelo, então não se somam.

## Otimizações do passe principal

- **Cache de uniforms**: o `Shader` guarda o local de cada uniform (antes cada objeto fazia dezenas de `glGetUniformLocation`).
- **Estado em três camadas**: ambiente, luzes, sombras e SSAO são enviados **uma vez por quadro**; texturas e fatores do material só quando o material muda; só o modelo, a matriz normal e os ossos vão a cada objeto.
- **Instancing**: objetos visíveis com a **mesma malha e o mesmo material** (texturas e fatores iguais; a cor pode variar) são desenhados numa chamada só, até 1024 por vez. Malhas animadas (com ossos) não são instanciadas. Os objetos são ordenados por material e malha, o que não muda a imagem (a cena opaca usa teste de profundidade). O checkbox **Instancing** do painel liga/desliga para comparar "Desenhos" e `Renderer.DrawScene` antes e depois.
- Ainda **não** instanciados: o pré-passe do SSAO e os mapas de sombra (cada um desenha objeto a objeto).

## Como ler

- `Window.Swap` alto e o resto baixo: você está limitado pelo V-Sync ou pelo monitor.
- `Renderer.DrawScene` alto na CPU e `GPU.DrawScene` baixo: o gargalo é a CPU (muitas chamadas de desenho). Veja "Desenhos" nos contadores.
- `GPU.DrawScene` alto: o gargalo é a GPU (pixels, sombras, SSAO). Teste desligar SSAO na câmera ou reduzir a resolução do viewport.

## Para quem mexe na engine

`Prism/Core/Profiler.h` (só `std`, testado por `PrismProfilerTests` com relógio falso):

```cpp
PRISM_PROFILE_SCOPE("Meu.Sistema");          // mede o resto do bloco
Profiler::Scope z("Outra"); ... z.Stop();    // ou encerra antes do fim do bloco
Profiler::CountDraw(indexCount);             // conta uma chamada de desenho
```

Nomes de zona devem ser literais. `Renderer/GpuTimer.h` faz o mesmo para a GPU (`GpuTimer::Scope`). Limites: 48 zonas, 16 zonas de GPU por quadro.
