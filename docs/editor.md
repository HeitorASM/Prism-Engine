# Editor

## Projetos

Um projeto é uma pasta com um arquivo `.prismproj` e esta estrutura:

```
MeuProjeto/
  MeuProjeto.prismproj
  Assets/
    Models/  Textures/  Audio/  Materials/  Prefabs/
  Scripts/
  Maps/
  Cache/        dados derivados; não deve ir para o controle de versão
```

Ao abrir o editor, a tela inicial permite criar ou abrir um projeto. O último mapa salvo é reaberto automaticamente.

## Painéis

- **Viewport**: renderização da cena com gizmos.
- **Hierarquia**: árvore de entidades. Arraste uma entidade sobre outra para torná-la filha, ou para a área vazia para torná-la raiz.
- **Propriedades**: uma seção por component, com botão "X" para remover e "+ Add Component" para adicionar. A seção Camera inclui uma preview do que a câmera vê.
- **Console**: lê o `LogBuffer` (últimas 2000 mensagens), com cor por nível, filtro de texto, toggles de verbosidade e auto-scroll.
- **Conteúdo do Projeto**: navega pelos arquivos do projeto. Duplo clique numa pasta entra nela; duplo clique num `.prismmap` carrega o mapa.
- **Editor de Script**: edição de `.lua` com syntax highlight.

## Atalhos

| Atalho | Ação |
|---|---|
| Ctrl+Z / Ctrl+Y | Desfazer / refazer |
| Ctrl+S | Salvar mapa |
| Ctrl+Shift+S | Salvar como |
| Ctrl+D | Duplicar entidade selecionada |
| Delete / Backspace | Excluir entidade selecionada |
| W / E / R | Gizmo: mover / rotacionar / escalar |
| Ctrl (durante o arraste) | Snapping (1 unidade; 15° na rotação) |

Ctrl+Z e Ctrl+Y não atuam enquanto um campo de texto do ImGui está em edição.

## Navegação na viewport

- **Botão direito (segurado)**: modo voar. Mouse olha ao redor, **WASD** move, **Q/E** desce/sobe.
- **Shift / Alt** durante o voo: velocidade 3× maior / menor.
- **Scroll durante o voo**: ajusta a velocidade base.
- **Scroll fora do voo**: dolly (zoom).
- **Clique esquerdo**: seleciona a entidade sob o cursor.

A viewport usa sempre a câmera livre do editor, nunca a câmera de jogo. A visão da câmera de jogo aparece na preview da seção Camera das Propriedades.

## Gizmos

- **Transform** (ImGuizmo): opera sempre em espaço de mundo, alternável entre Local e Mundo. Ao fim do gesto, o resultado é convertido de volta para o espaço local do pai. Gera um único `TransformCommand` por gesto.
- **Câmera**: frustum em wireframe (ciano para a `Primary`, cinza para as demais). O tamanho é indicativo, não usa o `FarClip` real.
- **Luz**: esfera de alcance para Point e cone para Spot.
- **Collider**: wireframe amarelo da entidade selecionada (caixa, esfera ou cápsula). Usa a matriz de mundo completa da entidade.
- **Raycast**: linha do `RaycastComponent`.

## Menu de contexto de entidade

Botão direito num node da Hierarquia abre **Duplicar**, **Criar Prefab...** e **Excluir**. O botão direito na viewport é reservado ao modo voar, então o menu só existe na Hierarquia.

## Mapas

- **Novo Mapa**: cena vazia sem arquivo associado.
- **Salvar Mapa** (Ctrl+S): grava em cima do arquivo atual; se não houver, funciona como Salvar Como.
- **Salvar Como** (Ctrl+Shift+S): pede um nome, mostra o caminho final e avisa se um mapa com esse nome já existe. Nunca sobrescreve sem avisar. O mapa salvo vira o mapa inicial do projeto.

Carregar outro mapa ou criar um novo descarta as alterações não salvas sem aviso (não há dirty flag ainda).

## Prefabs e materiais

- **Criar Prefab...** salva a entidade e toda a sua subárvore em `Assets/Prefabs/`.
- **Instanciar**: arraste um `.prismprefab` do Content Browser para a Hierarquia (vira raiz, ou filho se solto sobre um node) ou para a Viewport (vira raiz, na origem).
- **Materiais**: na seção Material da Propriedades, "Salvar como Asset..." grava um `.prismmat`; arrastar um `.prismmat` para o painel o carrega. Arrastar uma imagem para um slot de textura a atribui.

Instanciar um prefab ou carregar um material é uma **cópia pontual**: editar o arquivo depois não atualiza o que já foi instanciado.

## Modo Play

O botão **Play** na barra de menu abre uma **janela separada do sistema operacional** (1280×720, redimensionável) rodando uma cópia da cena. A cena de edição nunca é tocada por scripts ou física, então não há nada para restaurar ao parar.

- **Parar**, fechar a janela ou apertar **Esc** encerram o Play e descartam a cópia.
- A janela de Play compartilha o contexto OpenGL do editor, então meshes e shaders já carregados valem nas duas janelas.
- Só uma janela de Play por vez.
- Sem câmera `Primary` na cena, a janela mostra apenas a cor de fundo.
- Não há hot-reload de script durante o Play: edite o `.lua`, pare e rode de novo.

## Limitações conhecidas

- Os campos de Light, Collider, RigidBody, Camera e a troca de mesh não geram comandos de undo individuais. Geram undo: Transform, cor, criar/duplicar/excluir entidade, reparentar, instanciar prefab, carregar material e adicionar/remover component.
- Não há copiar/colar de subárvores.
- Não há resolução configurável para a janela de Play.
