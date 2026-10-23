# Limitações e roadmap

A Prism é um projeto de hobby em desenvolvimento: o que está aqui pode mudar ou ficar para depois.

## O que já foi feito

- Importação de modelos com materiais e texturas.
- Áudio 2D e 3D (`.wav`, `.mp3`, `.ogg`, `.flac`), com canais de volume e cache de clipes.
- Scripts em Lua (ciclo de vida, colisões, variáveis públicas, `Scene`, `Input`, raycast...) e em C++.
- Partículas.
- Fundo da cena: cor, gradiente ou panorama.
- UI in-game com botões e `OnClick`.
- Layout padrão do editor, Editor de Código com vários arquivos e esta documentação.

## Limitações conhecidas

- **Arquivos antigos**: sem migração entre versões de formato. Atualizar a engine pode tornar mapas e prefabs antigos ilegíveis.
- **Desfazer**: não cobre os campos de Light, Collider, Rigid Body e Camera, . Desfazer a exclusão de uma entidade não restaura Material e Audio Source.
- **Linguagem principal**: o scripting está sendo trabalhado para que o **C++ vire a linguagem principal** da Prism e o Lua seja uma opção. O que o Lua tem e o C++ ainda não (os "—" da [API de scripting](06-api-de-scripting.md)) deve ser trazido para o C++.
- **Scripts**: hot reload só para Lua (C++ exige parar o Play e compilar), sem `Scene.Load`, sem gamepad, sem depurador. Variáveis públicas ainda não existem para C++.
- **Física**: sem juntas; sem camadas configuráveis; corpos físicos devem ser raízes (sem pai).
- **Sombras**: 1 luz Directional (4 cascatas), até 4 Spot e 2 Point por vez.
- **Iluminação**: com panorama no ambiente há IBL (difuso e reflexos); sem ele vale o gradiente de 3 cores. Não há reflexos de objetos da cena nem *light probes*.
- **Áudio**: sem efeitos (reverb, filtros).
- **UI**: sem escala por resolução, sem quebra automática de texto, só letras latinas.
- **Editor**: sem copiar/colar subárvores; janela de Play com tamanho fixo (1280×720); a aba Game precisa ficar docada na janela do editor para o mouse bater com a imagem.
- **Exportação**: ainda não dá para gerar o jogo como um programa independente do editor.

## Ideias para o futuro

- Animação esquelética.
- Exportar o jogo sem o editor.
- `Scene.Load`, gamepad.
- Variáveis públicas e hot reload para scripts C++.

A lista técnica e atualizada está no roadmap da documentação do GitHub (botão **Ver docs do GitHub**).
