# Limitações e roadmap

A Prism é um projeto de hobby em desenvolvimento: o que está aqui pode mudar ou ficar para depois.

## O que já foi feito

- Importação de modelos com materiais e texturas.
- Áudio 2D e 3D (`.wav`, `.mp3`, `.flac`).
- Scripts em Lua (ciclo de vida, colisões, variáveis públicas, `Scene`, `Input`, raycast...) e em C++.
- Partículas.
- Fundo da cena: cor, gradiente ou panorama.
- UI in-game com botões e `OnClick`.
- Layout padrão do editor, Editor de Código com vários arquivos e esta documentação.

## Limitações conhecidas

- **Arquivos antigos**: sem migração entre versões de formato. Atualizar a engine pode tornar mapas e prefabs antigos ilegíveis.
- **Desfazer**: não cobre os campos de Light, Collider, Rigid Body e Camera, nem o menu Renderização. Desfazer a exclusão de uma entidade não restaura Material e Audio Source.
- **Linguagem principal**: o scripting está sendo trabalhado para que o **C++ vire a linguagem principal** da Prism e o Lua seja uma opção. O que o Lua tem e o C++ ainda não (os "—" da [API de scripting](06-api-de-scripting.md)) deve ser trazido para o C++.
- **Scripts**: sem recarga durante o Play, sem `Scene.Load`, sem gamepad, sem depurador. Variáveis públicas ainda não existem para C++.
- **Física**: sem juntas; sem camadas configuráveis; corpos físicos devem ser raízes (sem pai).
- **Sombras**: só uma luz Directional projeta sombra.
- **Iluminação**: o fundo (panorama) não ilumina a cena; não há IBL.
- **Áudio**: `.ogg` não toca; sem canais de volume nem efeitos.
- **UI**: sem escala por resolução, sem quebra automática de texto, só letras latinas.
- **Editor**: sem copiar/colar subárvores; janela de Play com tamanho fixo (1280×720).
- **Exportação**: ainda não dá para gerar o jogo como um programa independente do editor.

## Ideias para o futuro

- Pós-processamento (bloom).
- Animação esquelética.
- Exportar o jogo sem o editor.
- Iluminação por imagem (IBL) a partir do panorama.
- Suporte a `.ogg`, `Scene.Load`, `OnFixedUpdate`, gamepad.
- Recarga de scripts durante o Play.

A lista técnica e atualizada está no roadmap da documentação do GitHub (botão **Ver docs do GitHub**).
