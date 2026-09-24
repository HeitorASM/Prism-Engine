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

### Menu Renderização

Na barra de menus, ao lado de **Janela**. Ao clicar, abre um painel com:

- **Exposição** (0.1 a 8, escala logarítmica): brilho final da imagem, aplicado antes do tone mapping. 1.0 é neutro.
- **Intensidade** (0 a 1): brilho médio da luz ambiente. Com 0, só as luzes iluminam.
- **Céu**, **Horizonte** e **Chão**: as três cores do gradiente de ambiente.
- **Restaurar padrão**.

As mudanças aparecem **ao vivo** na viewport e na janela de Play. Ficam salvas **no projeto** (`.prismproj`), não na cena: cada projeto tem o seu visual, e mudar isso não marca o mapa como "com alterações não salvas" nem entra no undo/redo. O arquivo só é gravado quando você para de mexer (mouse solto e cerca de 0,35 s sem editar), não a cada frame do arrasto, e o que ficou pendente é gravado ao fechar o editor.

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

### Alterações não salvas

Fechar o editor (botão X ou **Arquivo → Fechar Projeto**), criar um **Novo Mapa** ou abrir outro mapa com duplo clique no Content Browser, tendo alterações não salvas, abre um popup com três opções:

- **Salvar**: grava o mapa e só então continua a ação. Se o mapa ainda não tem arquivo, abre o Salvar Como; cancelar o nome cancela também a ação. Se a gravação falhar, a ação não prossegue.
- **Não salvar**: continua a ação e descarta as alterações.
- **Cancelar** (ou `Esc`): volta para o editor sem mudar nada.

A detecção não depende do undo/redo: compara o estado atual da cena com o de quando ela foi carregada ou salva pela última vez (`SceneSerializer::ComputeFingerprint`). Por isso pega também as edições que não geram comando de undo (Light, Collider, RigidBody, Camera) e qualquer campo novo de component, sem precisar marcar nada. Desfazer ou reverter à mão até o estado salvo volta a contar como limpo. O cálculo só roda ao tentar sair ou trocar de mapa, nunca por frame.

Limite conhecido: duas entidades com conteúdo 100% idêntico (mesmo nome, transform e components) são indistinguíveis, então trocar o pai de um filho entre elas não é detectado. As duas árvores ficam idênticas, então o resultado visível é o mesmo.

## Prefabs e materiais

- **Criar Prefab...** salva a entidade e toda a sua subárvore em `Assets/Prefabs/`.
- **Instanciar**: arraste um `.prismprefab` do Content Browser para a Hierarquia (vira raiz, ou filho se solto sobre um node) ou para a Viewport (vira raiz, na origem).
- **Materiais**: na seção Material da Propriedades, "Salvar como Asset..." grava um `.prismmat`; arrastar um `.prismmat` para o painel o carrega. Arrastar uma imagem para um slot de textura a atribui.

Instâncias de prefab mantêm um vínculo com o `.prismprefab` de origem. Selecionar a raiz **ou qualquer filho** da instância mostra a seção **Prefab** nas Propriedades:

- **Divergências** lista os components (incluindo o **Transform dos filhos**) que foram alterados na instância em relação ao arquivo.
- **Aplicar ao Prefab** grava o valor da instância de volta no `.prismprefab`. É assim que uma edição feita numa instância passa a valer para as outras cenas.
- **Reverter** descarta a alteração e volta ao valor do arquivo; **Sincronizar com o Prefab** atualiza os components que não divergem.
- O Transform da **raiz** nunca conta como divergência: reposicionar a instância na cena é sempre livre.

Carregar um material é uma **cópia pontual** (a menos que fique vinculado a um `.prismmat`).

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
- Os ajustes do menu Renderização não entram no undo/redo (Ctrl+Z não os desfaz); use **Restaurar padrão**.
