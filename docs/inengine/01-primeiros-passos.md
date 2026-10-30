# Primeiros passos

## 1. Criar um projeto

Ao abrir o editor aparece a tela inicial:

1. Em **Novo projeto** (lado direito), digite o **nome** e escolha o **local** (o botão **Procurar...** abre o seletor de pastas do sistema). O editor mostra onde o projeto será criado e avisa na hora se o nome é inválido ou se a pasta de destino já existe e não está vazia. Depois clique em **Criar projeto**.
2. Para continuar um projeto, clique nele em **Projetos recentes** (lado esquerdo; clique direito remove da lista) ou use **Abrir projeto...** para escolher um arquivo `.prismproj`.

Todo projeto é C++ primeiro: a pasta `Source/` (CMake e um script de exemplo) já é criada junto, e a opção **Compilar os scripts C++ ao abrir o projeto** (ligada por padrão) compila o módulo assim que o editor abre. Projetos antigos, sem `Source/`, ganham a pasta ao serem abertos. Scripts Lua continuam disponíveis em `Scripts/`.

O projeto é uma pasta assim:

```
MeuProjeto/
  MeuProjeto.prismproj
  Assets/
    Models/  Textures/  Audio/  Materials/  Prefabs/
  Scripts/      scripts Lua
  Source/       scripts C++ (CMakeLists.txt, exemplo Spinner)
  Maps/
  Binaries/     módulo C++ compilado (fora do controle de versão)
  Cache/        dados derivados (fora do controle de versão)
  .gitignore    já ignora Cache/ e Binaries/
```

O último mapa salvo é reaberto automaticamente na próxima vez.

## 2. Montar uma cena

No menu **Entidade**:

- **Criar Cubo**: uma entidade visível (um cubo).
- **Criar Luz**: sem luz a cena fica escura.
- **Criar Camera**: a câmera do jogo. Ela nasce como `Primary` (a que vale no Play).

Selecione uma entidade na **Hierarquia** (ou clicando nela na **Viewport**) e use os gizmos: **W** move, **E** rotaciona, **R** escala. Os valores exatos ficam em **Propriedades**.

Para navegar na viewport, segure o **botão direito** e use **WASD** (Q/E descem e sobem).

## 3. Salvar

**Ctrl+S** salva o mapa. Na primeira vez ele pede um nome (**Salvar Como**). Os mapas ficam na pasta `Maps/` e têm a extensão `.prismmap`.

## 4. Rodar

Clique em **Play** (barra superior). O jogo roda na aba **Game**, numa **cópia** da cena: o que acontece lá não altera a cena que você está editando. Clique na imagem para controlar o jogo e aperte **Esc** para devolver o teclado ao editor. **Parar** encerra o Play. (**Em janela** roda o mesmo jogo numa janela separada.)

> Sem câmera `Primary`, a aba Game mostra só a cor de fundo. Se a tela ficar vazia, confira isso.

## 5. Seu primeiro script

1. Selecione o cubo e, em **Propriedades**, clique em **+ Add Component → Script**.
2. Clique em **Novo...**, dê um nome (por exemplo `girar`) e confirme. O arquivo é criado em `Scripts/`.
3. Clique em **Editar**: o arquivo abre no **Editor de Código**. Troque o conteúdo por:

```lua
local velocidade = 90 -- graus por segundo

function OnUpdate(dt)
    local t = entity:GetTransform()
    t.Rotation.y = t.Rotation.y + velocidade * dt
end
```

4. Salve com **Ctrl+S** (dentro do Editor de Código) e aperte **Play**. O cubo gira.

Daqui, o próximo passo natural é a página [Scripting em Lua](04-scripting-lua.md). Há também exemplos prontos na pasta `PrismEditor/assets/ScriptExamples/` da engine (`example_spin.lua`, `example_collect_coins.lua`, `example_player_input_raycast.lua`).

## 6. Trazer um modelo 3D

Copie um `.obj`, `.fbx`, `.gltf` ou `.glb` para `Assets/Models/`, clique em **Atualizar** no **Conteúdo do Projeto** e **arraste o modelo** para a Hierarquia ou para a Viewport. Ele entra na cena com os materiais e as texturas do arquivo.
