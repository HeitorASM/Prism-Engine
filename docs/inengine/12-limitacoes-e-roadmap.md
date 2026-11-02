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

- **Arquivos antigos**: mapas e prefabs gravados em blocos (mapa v21, prefab v9 em diante) continuam abrindo quando a engine ganha components ou campos novos, e components desconhecidos são preservados ao salvar. Formatos anteriores a isso (mapa antes da v19, prefab antes da v7) são recusados com uma mensagem. Antes de sobrescrever um mapa, o editor guarda uma cópia em `Cache/Backups/Maps/` (os 20 mais recentes) e, a cada 2 minutos, um autosave que é oferecido se o editor fechar de forma inesperada.
- **Desfazer**: cobre transform, os campos dos components (Light, Collider, Rigid Body, Camera, Material...) e criar/duplicar/colar/excluir entidades. Desfazer uma exclusão restaura a subárvore inteira, mas as entidades voltam com identidade nova: passos mais antigos do histórico que mexiam nela deixam de ter efeito.
- **Linguagem principal**: o scripting está sendo trabalhado para que o **C++ vire a linguagem principal** da Prism e o Lua seja uma opção. O que o Lua tem e o C++ ainda não (os "—" da [API de scripting](06-api-de-scripting.md)) deve ser trazido para o C++.
- **Scripts**: sem gamepad e sem depurador. O hot reload de C++ recria os scripts (o estado dos membros recomeça); o de Lua preserva as variáveis globais. `Scene.Load` não carrega nada entre cenas (sem persistência).
- **Física**: corpos físicos devem ser raízes (sem pai). Camadas, máscaras de colisão, juntas e *character controller* já existem (ver `docs/fisica.md`).
- **Animação**: até 128 ossos por modelo e 4 ossos por vértice; sem *morph targets*, sem *blend trees*, sem editor de keyframes (há máquina de estados, eventos e root motion). Detalhes em `docs/animacao.md` do projeto.
- **Sombras**: 1 luz Directional (4 cascatas), até 4 Spot e 2 Point por vez.
- **Iluminação**: com panorama no ambiente há IBL (difuso e reflexos); sem ele vale o gradiente de 3 cores. Não há reflexos de objetos da cena nem *light probes*.
- **Áudio**: sem efeitos (reverb, filtros).
- **UI**: sem escala por resolução, sem quebra automática de texto, só letras latinas.
- **Editor**: copiar/colar só dentro do editor (sem usar a área de transferência do sistema); janela de Play com tamanho fixo (1280×720); a aba Game precisa ficar docada na janela do editor para o mouse bater com a imagem.
- **Exportação**: **Arquivo → Exportar Jogo** gera uma pasta com o jogo e o Runtime (sem instalador, sem ícone próprio, assets sem empacotar). Para distribuir, use um build Release do editor.

## Ideias para o futuro

- Máquina de estados de animação, *morph targets*, retargeting/IK e root motion (a animação esquelética em si já existe).
- Exportação: ícone do executável, empacotar assets em um arquivo, só levar o que o jogo usa.
- `Scene.Load`, gamepad.

A lista técnica e atualizada está no roadmap da documentação do GitHub (botão **Ver docs do GitHub**).
