# O editor

## Layout

Na primeira abertura os painéis já vêm organizados:

- **Barra superior**: Salvar, Desfazer, Refazer, **Play/Parar** (centro), **Documentação (F1)** e **Restaurar layout**.
- **Esquerda**: Hierarquia.
- **Direita**: Propriedades.
- **Centro**: Viewport, Editor de Código e Documentação, em abas.
- **Embaixo**: Conteúdo do Projeto e Console, em abas.

Arraste as abas para reorganizar; o editor lembra a disposição. Se ficar bagunçado, use **Janela → Restaurar layout padrão**.

## Painéis

| Painel | Para que serve |
|---|---|
| **Viewport** | Mostra a cena com gizmos. Clique para selecionar uma entidade. |
| **Hierarquia** | Árvore de entidades. Arraste uma entidade sobre outra para torná-la filha, ou para o espaço vazio para torná-la raiz. Botão direito: Duplicar, Criar Prefab, Excluir. |
| **Propriedades** | Uma seção por component da entidade selecionada, com **X** para remover e **+ Add Component** para adicionar. |
| **Console** | Mensagens da engine e dos scripts, com cor por nível, filtro de texto e auto-scroll. |
| **Conteúdo do Projeto** | Arquivos do projeto. Duplo clique entra numa pasta, abre um mapa ou abre um arquivo de texto no Editor de Código. |
| **Editor de Código** | Edita scripts e outros arquivos de texto (veja abaixo). |
| **Documentação** | Esta documentação. |

## Editor de Código

Abre `.lua`, `.cpp`/`.h` (com realce de sintaxe) e qualquer outro arquivo de texto (`.md`, `.txt`...), sem realce. Pode ter **vários arquivos abertos**: a lista deles fica na lateral esquerda do painel. Clique para trocar de arquivo, no **x** (ou com o botão do meio) para fechar. Um `*` marca alterações não salvas; ao fechar um arquivo assim o editor pergunta se salva. **Ctrl+S** salva o arquivo ativo e **Salvar tudo** salva todos. Arquivos binários ou maiores que 4 MB não abrem.

## Atalhos

| Atalho | Ação |
|---|---|
| Ctrl+Z / Ctrl+Y | Desfazer / refazer |
| Ctrl+S | Salvar o mapa (ou o arquivo, se o foco está no Editor de Código) |
| Ctrl+Shift+S | Salvar como |
| Ctrl+D | Duplicar a entidade selecionada |
| Delete / Backspace | Excluir a entidade selecionada |
| W / E / R | Gizmo: mover / rotacionar / escalar |
| Ctrl (arrastando) | Snapping (1 unidade; 15° na rotação) |
| F1 | Abrir a documentação |

Ctrl+Z e Ctrl+Y não atuam enquanto você digita num campo de texto.

## Navegar na viewport

- **Botão direito (segurado)**: modo voar. O mouse olha ao redor, **WASD** move, **Q/E** desce/sobe.
- **Shift / Alt** durante o voo: velocidade maior / menor. A roda do mouse ajusta a velocidade.
- **Roda do mouse** fora do voo: aproxima e afasta.

A viewport usa sempre a câmera livre do editor. O que a câmera do jogo enxerga aparece na prévia da seção **Camera** das Propriedades.

## Mapas

- **Novo Mapa**: cena vazia.
- **Salvar Mapa** (Ctrl+S): grava no arquivo atual; se não houver, pede um nome.
- **Salvar Como**: nunca sobrescreve sem avisar.
- Trocar de mapa, criar um novo ou fechar o editor com alterações não salvas abre uma pergunta: **Salvar**, **Não salvar** ou **Cancelar**.

## Prefabs

Um **prefab** é uma entidade (com filhos) guardada num arquivo `.prismprefab`, para reaproveitar. **Criar Prefab...** (botão direito na Hierarquia) salva em `Assets/Prefabs/`. Arraste o arquivo para a Hierarquia ou para a Viewport para instanciar. Também dá para instanciar por script (`Scene.Instantiate`).

Instâncias continuam ligadas ao arquivo: a seção **Prefab** das Propriedades mostra as divergências, e **Aplicar ao Prefab** grava a mudança de volta no arquivo (valendo para todas as outras cenas).

## Materiais

Na seção **Material**, **Salvar como Asset...** grava um `.prismmat`. Arraste uma imagem do Conteúdo do Projeto para um slot de textura para atribuí-la, e um `.prismmat` para carregar um material.

## Menu Renderização

Ajustes visuais **do projeto** (não da cena), com efeito ao vivo: exposição, intensidade do ambiente, as três cores do ambiente e o fundo (cor, gradiente ou panorama). Ficam no arquivo `.prismproj` e não entram no desfazer. Veja [Visual e renderização](09-visual-e-renderizacao.md).

## Modo Play

O **Play** abre uma janela separada (1280×720) com uma cópia da cena, onde rodam scripts, física, áudio, partículas e UI. A cena de edição nunca é tocada. Não há recarga de script durante o Play: edite o arquivo, pare e rode de novo.

## O que o Desfazer cobre

Desfaz: mover/rotacionar/escalar, cor, criar/duplicar/excluir entidade, trocar de pai, instanciar prefab, carregar material e adicionar/remover component. **Não** desfaz (ainda) a edição dos campos de Light, Collider, Rigid Body e Camera, nem os ajustes do menu Renderização.
