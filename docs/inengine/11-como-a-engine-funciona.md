# Como a engine funciona

Uma visão do que acontece por baixo. Não é preciso saber nada disso para usar a engine, mas ajuda a entender o comportamento.

## Estrutura

- A **engine** (`Prism`) é uma biblioteca em C++. O **editor** (`PrismEditor`) é um programa que usa a engine; ele próprio é uma "camada" (*Layer*) empilhada no laço principal.
- Bibliotecas usadas: OpenGL 4.5 (desenho), GLFW (janela e entrada), Dear ImGui (interface do editor), EnTT (entidades), Lua + sol2 (scripts), Jolt (física), Assimp (modelos), miniaudio (áudio) e stb (imagens e fontes).

## A cena

Uma cena é um conjunto de **entidades** com **components** (dados puros: posição, malha, luz...). Não há "classes de objeto" com comportamento embutido: o comportamento vem dos **sistemas**, que percorrem as entidades que têm certos components (a física percorre quem tem Collider + Rigid Body; o renderizador, quem tem Transform + Mesh Renderer).

Cada tipo de component é registrado num **registro** central. Salvar/carregar mapas, o menu **+ Add Component**, duplicar entidades e copiar a cena para o Play usam todos o mesmo registro, e é por isso que adicionar um component novo na engine é um trabalho pequeno.

## Editar x jogar

No editor a cena está **parada**: scripts, física, áudio e partículas não rodam. Ao apertar **Play**, a engine:

1. **Clona** a cena de edição (nada do que acontece no Play volta para ela).
2. Abre uma **janela separada**, compartilhando o contexto OpenGL do editor.
3. Inicia a física, carrega os scripts (Lua e C++), prepara o áudio e as partículas, e chama `OnCreate` de cada script.

Ao parar, a janela e a cópia da cena são descartadas.

## O que acontece em cada frame do Play

Nesta ordem:

1. **Física**: avança a simulação em passos fixos de 1/60 s (no máximo 5 por frame; se o computador não aguenta, o jogo desacelera em vez de travar) e escreve as novas posições nos Transforms.
2. **Raycasts** dos components Raycast.
3. **Scripts Lua**: `OnUpdate(dt)` de cada entidade, depois os **eventos de colisão** que a física coletou.
4. **Scripts C++**.
5. **Destruições** pedidas pelos scripts (`entity:Destroy()` só vale aqui, no fim, para não bagunçar a iteração).
6. **Partículas** e **áudio**.
7. **Desenho** da cena e, por cima, a **UI**; depois a interface lê o mouse e dispara os `OnClick`.

Por isso um script enxerga a posição que a física acabou de calcular no mesmo frame.

## Como a cena é desenhada

1. **Sombra** (se há uma luz Directional com sombra): a cena é desenhada do ponto de vista da luz.
2. **Pré-passe**: normais e profundidade da câmera.
3. **SSAO**: oclusão ambiente calculada a partir do pré-passe.
4. **Fundo** (cor, gradiente ou panorama).
5. **Cor**: cada entidade com malha, com iluminação PBR de até 16 luzes.
6. **Partículas** e, depois de tudo, a **UI**.

Os dois lados (a viewport do editor e a janela de Play) usam o mesmo desenho.

## Scripts: Lua x C++

- **Lua** roda numa máquina virtual embutida; cada script tem seu ambiente isolado. É lido do arquivo ao iniciar o Play.
- **C++** é um módulo compilado à parte (uma biblioteca carregada em tempo de execução). Como o módulo não linka com a engine, ele conversa com ela por uma **tabela de funções** entregue ao carregar. Por isso a versão dessa API é conferida e módulos antigos são recusados.

## Arquivos

- **Mapas** (`.prismmap`), **prefabs** (`.prismprefab`) e **materiais** (`.prismmat`) são arquivos binários com número de versão. Quando um component novo entra na engine, a versão sobe e arquivos antigos **não são migrados** (por enquanto): guarde cópias dos seus projetos ao atualizar a engine.
- O projeto (`.prismproj`) é texto simples e guarda os ajustes visuais do menu Renderização.
- Cada asset ganha um arquivo `.meta` com um identificador, para que mover um arquivo não quebre as referências.
