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

## Documentação

O botão **Documentação (F1)** da barra superior (ou o menu **Ajuda**) abre o painel de documentação: a lista de documentos fica à esquerda e o conteúdo à direita, com **busca** (filtra a lista e destaca os trechos encontrados), botão **Voltar** e links entre documentos.

Há **duas fontes**, alternadas pelo botão pequeno no fim da lista:

- **Documentação da engine** (padrão): os guias para quem **usa** a engine, em `docs/inengine/*.md`. A primeira página é a "Visão geral". A ordem é a do nome do arquivo (`00-`, `01-`...).
- **Docs do GitHub**: esta documentação técnica (`docs/*.md`), voltada a quem mexe no código da engine.

O painel lê os `.md` da pasta `docs/` (ao lado do código ou, num build, a cópia ao lado do executável), então editar um doc e apertar **Recarregar** já mostra a mudança.

## Layout padrão

Na primeira abertura (sem layout salvo) os painéis já vêm organizados:

- **Barra superior** (fixa): Salvar, Desfazer, Refazer, **Compilar** (scripts C++, com o estado do módulo ao lado), **Play / Em janela / Parar** (centro) e **Restaurar layout**.
- **Esquerda**: Hierarquia.
- **Direita**: Propriedades.
- **Centro**: Viewport, **Game** e Editor de Código, em abas (a Documentação também abre aqui).
- **Embaixo**: Conteúdo do Projeto, Console e **Saída de Build**, em abas.

Arraste as abas para reorganizar; o editor lembra a sua disposição (`imgui.ini`). Se ficar bagunçado, use **Janela → Restaurar layout padrão** (ou o botão da barra superior).

## Painéis

- **Viewport**: renderização da cena com gizmos.
- **Hierarquia**: árvore de entidades. Arraste uma entidade sobre outra para torná-la filha, ou para a área vazia para torná-la raiz.
- **Propriedades**: uma seção por component, com botão "X" para remover e "+ Add Component" para adicionar. A seção Camera inclui uma preview do que a câmera vê.
- **Saída de Build**: saída da compilação dos scripts C++, com erros em vermelho e avisos em amarelo; erros e avisos com arquivo são clicáveis (abrem no Editor de Código, ainda sem pular para a linha). Filtro "So erros e avisos". Abre em foco quando a compilação começa e quando falha.
- **Console**: lê o `LogBuffer` (últimas 2000 mensagens), com cor por nível, filtro de texto, toggles de verbosidade e auto-scroll.
- **Conteúdo do Projeto**: navega pelos arquivos do projeto. Duplo clique numa pasta entra nela; duplo clique num `.prismmap` carrega o mapa.
  - Barra: **Voltar**, **Atualizar**, **Nova pasta**, tamanho dos ícones e o caminho clicável (cada trecho navega).
  - **Renomear** (`F2` ou botão direito), **Excluir** (`Del`, pede confirmação) e **mover** (arraste para uma pasta ou para o botão Voltar). O `.meta` acompanha o arquivo, então o AssetID e as referências feitas por ele continuam valendo; o caminho do mapa aberto é atualizado.
  - As pastas e arquivos da **raiz do projeto** (`Assets`, `Maps`, `Scripts`, `.prismproj`...) são protegidos.
  - Limitação: o componente Script guarda o **caminho** do `.lua`; ao renomear/mover um script, reaponte os componentes que o usam (o editor avisa no Console).
- **Tema e fonte**: a interface usa a fonte DejaVu Sans e uma paleta única definida em `EditorTheme` (`PrismEditor/src/PrismEditor/Core/EditorTheme.h`); os ícones são desenhados por código (`EditorIcons`).
- **Editor de Código**: abre `.lua` e `.cpp`/`.h` (com syntax highlight) e qualquer outro arquivo de texto (`.md`, `.txt`…) sem highlight. Dê duplo clique num arquivo no Conteúdo do Projeto, ou use "Editar" no component Script. Vários arquivos podem ficar abertos: a lateral esquerda do painel lista os abertos (clique troca, **x** ou clique do meio fecha, `*` marca alterações não salvas; fechar um arquivo alterado pergunta se salva). **Ctrl+S** salva o arquivo ativo (preservando CRLF/LF) e **Salvar tudo** salva todos. Com o foco no painel, os atalhos da cena (Ctrl+Z, Ctrl+S, Delete) ficam desligados. Arquivos binários ou maiores que 4 MB são recusados.

### Ambiente e câmera (visual)

O visual **não é mais um menu do editor**: pertence à cena e à câmera.

- **Environment** (component da entidade "Ambiente", criada com cada mapa novo; ou **Entidade → Criar Ambiente**): intensidade, cores Céu/Horizonte/Chão e o fundo (cor, gradiente ou panorama). É gravado no mapa, então cada mapa tem o seu visual, e as edições entram no desfazer.
- **Camera**, seção **Render**: fundo (skybox da cena ou cor chapada), exposição e SSAO, mais **Pós-processamento** (bloom, FXAA, saturação e contraste), por câmera.

A viewport livre do editor usa o ambiente da cena e as opções padrão de câmera. Detalhes em [Renderização](renderizacao.md).

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

## UI in-game

Menu **Entidade → Criar UI** cria Panel, Texto, Botão ou Imagem. O resultado aparece no viewport (só visualização) e no Play. Detalhes em [UI in-game](ui.md).

## Modo Play

O botão **Play** roda o jogo na aba **Game**, ao lado da Viewport. **Em janela** roda o mesmo jogo numa janela separada do sistema (1280×720, redimensionável). Nos dois casos roda uma **cópia** da cena: a cena de edição nunca é tocada por scripts ou física, então não há nada para restaurar ao parar.

- **Aba Game**: clique na imagem para o jogo receber teclado e mouse; **Esc** devolve o controle ao editor (e solta o cursor, se um script o travou). Enquanto o jogo controla o teclado, atalhos do editor como Delete e Ctrl+Z ficam desligados. A UI in-game é clicável com o mouse relativo à imagem. A aba precisa ficar **docada** na janela do editor: arrastada para uma janela própria, o mouse não bate com a imagem.
- **Janela separada**: **Esc** ou fechar a janela encerram o Play; ela compartilha o contexto OpenGL do editor.
- **Parar** (barra superior) encerra qualquer um dos dois e descarta a cópia.
- Só um Play por vez (aba ou janela).
- Sem câmera `Primary` na cena, aparece só a cor de fundo (um aviso vai para o Console).
- Não há hot-reload de script durante o Play: edite o `.lua`, pare e rode de novo.

### Rodar sem o editor (PrismRuntime)

O alvo **PrismRuntime** é um executável só do jogo: `PrismRuntime <pasta-do-projeto | arquivo.prismproj>`. Carrega o projeto, abre o **mapa inicial** (o último salvo no editor), carrega os scripts C++ compilados (`Binaries/`) e roda com a câmera `Primary`. **F11** alterna tela cheia. Sem argumento, procura um projeto em `Game/` ao lado do executável.

## Limitações conhecidas

- Os campos de Light, Collider, RigidBody, Camera e a troca de mesh não geram comandos de undo individuais. Geram undo: Transform, cor, criar/duplicar/excluir entidade, reparentar, instanciar prefab, carregar material e adicionar/remover component.
- Não há copiar/colar de subárvores.
- Não há resolução configurável para a janela de Play.
