# Prism Engine

Engine 3D em C++20 com editor integrado (WYSIWYG), construída para projetos
reais e portfólio. Renderização via OpenGL 4.5, janela via GLFW, UI via Dear
ImGui (docking), matemática via glm.

## Estado atual (fundação)

Esta é a **fundação** do projeto: arquitetura de camadas (`Layer`/`LayerStack`),
`Application` central, sistema de eventos, `Project`/`ProjectManager` e o
`EditorLayer` com dockspace e painéis (Viewport, Hierarquia, Propriedades,
Console). A viewport 3D agora **renderiza de verdade**: um `Framebuffer`
offscreen recebe o desenho de um cubo de teste (via `Renderer` + `Shader`
básicos) e o resultado é mostrado dentro do painel Viewport com
`ImGui::Image`, com uma câmera de órbita simples (botão direito + arrastar,
scroll para zoom) e resize automático do framebuffer conforme o painel muda
de tamanho.

Ainda **não** implementados (por design, para não travar o projeto tentando
fazer tudo de uma vez): física Box3D, BSP/CSG, importação de assets
(FBX/OBJ/glTF/áudio), sistema de luzes de verdade (afetando a renderização).
Scene/Entity real e scripting Lua básico já foram implementados desde a
escrita original deste parágrafo - ver "Nota sobre a Scene/ECS" e "Nota
sobre Scripting Lua" mais abaixo.

## Arquitetura em uma frase

`Application` roda um loop e possui uma `LayerStack`. O Editor não é um caso
especial dentro da engine — ele é só mais uma `Layer` empilhada. Isso significa
que, quando o modo "Runtime" (jogo exportado, sem editor) existir, ele também
será só outra `Layer`/executável, sem exigir mudanças no core.

```
Prism/          -> a engine, biblioteca estática (Prism.lib)
  src/Prism/
    Core/       -> Application, Window, eventos, Log, LogBuffer,
                   Command/CommandHistory (undo-redo)
    Layer/      -> Layer, LayerStack
    Renderer/   -> GraphicsContext (abstrai OpenGL/futuro Vulkan),
                   Framebuffer (offscreen render target para a viewport),
                   Shader, Mesh (wrapper generico de VAO/VBO/EBO),
                   PrimitiveMeshFactory (gera geometria de Cube/Sphere/
                   Capsule/Cylinder/Plane), Renderer (API minima de desenho)
    Scene/      -> Scene, Entity, Components (ECS via EnTT), SceneSerializer
    Scripting/  -> ScriptEngine (VM Lua via sol2, ciclo de vida de scripts)
    Physics/    -> PhysicsEngine (mundo Box3D, corpos, sincronizacao com Transform)
    ImGui/      -> ImGuiLayer (integra Dear ImGui ao ciclo de eventos)
    Project/    -> Project, ProjectSerializer (.prismproj)

PrismEditor/    -> o executável do editor
  src/
    EditorApp.cpp             -> define CreateApplication()
    PrismEditor/Layers/
      ProjectManagerLayer.*   -> tela inicial (criar/abrir projeto)
      EditorLayer.*           -> dockspace + painéis do editor
    PrismEditor/Panels/
      ConsolePanel.*          -> painel de log (lê Prism::LogBuffer)
      ContentBrowserPanel.*   -> navega os arquivos do projeto (Assets/Maps/Scripts/Cache)
    PrismEditor/Commands/
      EditorCommands.h        -> Commands concretos (Transform, criar/excluir entidade, etc)

vendor/         -> dependências de terceiros
  glad/         -> vendorizado localmente (gerado, OpenGL 4.5 Core)
  CMakeLists.txt -> baixa GLFW, glm e Dear ImGui via FetchContent
```

## Como compilar

### Pré-requisitos
- CMake 3.20+
- Visual Studio 2022 (com "Desktop development with C++") **ou** qualquer
  compilador C++20 (MSVC, Clang, GCC)
- Conexão com a internet na primeira configuração (CMake baixa GLFW/glm/ImGui)

### Opção A — Visual Studio 2022 (recomendado)

1. Abra o Visual Studio 2022.
2. **Arquivo → Abrir → Pasta...** e selecione a pasta raiz deste projeto
   (a que contém este `README.md` e o `CMakeLists.txt` principal).
3. O Visual Studio detecta o `CMakeLists.txt` automaticamente e configura o
   projeto sozinho (pode levar um tempo na primeira vez, baixando as
   dependências).
4. No seletor de "Startup Item" (topo da janela), escolha `PrismEditor.exe`.
5. Compile e rode com **Ctrl+F5** ou o botão verde de Play.

Não é necessário criar `.vcxproj` manualmente, adicionar arquivos um por um,
nem configurar Include/Library Directories — o CMake cuida de tudo isso.

### Opção B — linha de comando

```bash
cmake -B build -S .
cmake --build build --config Debug
```

O executável final fica em `build/PrismEditor/Debug/PrismEditor.exe`
(ou `Release/` conforme a configuração escolhida).

## Convenção crítica: ordem de includes do OpenGL

`glad` **sempre** precisa ser incluído antes de qualquer coisa que toque em
GLFW ou OpenGL, em **todo** arquivo `.cpp`. Veja o comentário completo em
`Prism/src/Prism/Renderer/OpenGLContext.cpp`. Ignorar isso é a causa mais
comum de erro de build nesta engine.

## Nota sobre Parenting / Hierarquia (estado atual)

Entidades agora podem ter uma entidade PAI (`Prism::RelationshipComponent`
em `Components.h`) - a Hierarchy panel virou uma arvore de verdade (era
uma lista plana): arraste uma entidade sobre outra para torna-la filha, ou
solte na area vazia do painel para torna-la raiz de novo. Cada
reparentamento passa pelo `CommandHistory` (`SetParentCommand`), entao
Ctrl+Z desfaz.

`Scene::GetWorldTransform(Entity)` combina o `TransformComponent` local de
uma entidade com o de TODOS os ancestrais - e o unico lugar que deveria ser
usado para desenhar/posicionar algo no espaco do mundo (a viewport, os
gizmos de camera/collider e a preview da camera Primary ja foram
atualizados para usar isto em vez do `TransformComponent::GetTransform()`
local sozinho). Mover/rotacionar/escalar um pai move os filhos junto
automaticamente.

`Scene::DestroyEntity` agora e recursivo: excluir uma entidade tambem
exclui toda a subarvore de filhos dela (mesmo comportamento de
Unity/Unreal/Godot) - nao ha suporte a "desanexar filhos antes de
excluir o pai" na UI ainda.

`Scene::SetParent` recusa (retorna `false`, sem mudar nada) qualquer
operacao que criaria um ciclo (um ancestral virando filho do proprio
descendente) - protegido tanto no drag-and-drop quanto internamente.

**Formato de arquivo**: `.prismmap` subiu para **v4** (indice do pai por
entidade, ver comentario em `SceneSerializer.cpp`) - mapas salvos em v3
precisam ser resalvos uma vez. `RelationshipComponent::Children` NAO e
salvo diretamente - e reconstruido a partir dos indices de pai depois que
todas as entidades da cena ja foram criadas no `Deserialize()`.

**Limitacoes conhecidas, deixadas de proposito**: sem opcao de UI para
"desanexar da hierarquia" alem do drag-and-drop para a area vazia; sem
indentacao visual customizada alem da propria arvore do ImGui; sem
suporte a copiar/colar uma subarvore inteira.

## Nota sobre Scripting Lua (estado atual)

**Correcoes de build aplicadas apos a primeira integracao** (se voce ja
tinha configurado o CMake antes, apague a pasta `build/`/`out/` e
reconfigure do zero):
- `onelua.c` (um "amalgamation" alternativo que ja inclui todos os outros
  `.c` do Lua sozinho) estava sendo compilado JUNTO com os `.c`
  individuais - toda funcao do Lua ficava definida duas vezes
  (`lua_next ja definido...`, etc). Corrigido excluindo `onelua.c` do GLOB
  em `vendor/CMakeLists.txt` (so os `.c` individuais sao compilados).
- `LUA_USE_WINDOWS` estava sendo definida manualmente via
  `target_compile_definitions`, mas `luaconf.h` (dentro do proprio source
  do Lua) ja se autodetecta em Windows sozinho - a definicao manual
  chegava via linha de comando do compilador ANTES do preprocessador
  rodar o `#if !defined(LUA_USE_WINDOWS)` de `luaconf.h`, causando
  "redefinicao de macro" (C4005) em todo `.c` do Lua. Corrigido removendo
  a definicao manual - `luaconf.h` cuida disso sozinho.
- `ScriptEngine::LoadScript` usava `sol::state::safe_script(codigo, env,
  chunkname)`, uma sobrecarga do sol2 com um bug de resolucao conhecido no
  MSVC (SFINAE escolhe a sobrecarga errada - ver
  github.com/ThePhD/sol2/issues/572), que aparecia como "C2064: term does
  not evaluate to a function taking 2 arguments" em `state_view.hpp`.
  Corrigido trocando para `load()` + `sol::set_environment()` + chamada
  manual do chunk - mesmo resultado, sem passar pela sobrecarga
  problematica.

Lua 5.4 esta embutido via `sol2` (bindings header-only) - ver
`vendor/CMakeLists.txt` (ambos baixados via FetchContent, mesmo padrao do
GLFW/glm/EnTT/ImGui). `Prism::ScriptEngine`
(`Prism/src/Prism/Scripting/ScriptEngine.h/.cpp`) cuida da VM Lua global do
processo (uma so, nao uma por script - cada script isolado via
`sol::environment` proprio) e do ciclo de vida de scripts individuais.

**API exposta a scripts Lua (deliberadamente minima por enquanto)**:
- `entity` - variavel global implicita dentro de cada script, a propria
  entidade dona do `ScriptComponent`.
- `entity:GetTransform()` - retorna a `Transform` (Translation/Rotation/Scale,
  cada uma um `Vec3` com `.x`/`.y`/`.z`) da propria entidade, editavel
  diretamente (`entity:GetTransform().Translation.y = 5`).
- `entity:GetName()`, `entity:SetPosition(x,y,z)`, `entity:Translate(dx,dy,dz)`.
- `log(msg)` / `log_warn(msg)` / `log_error(msg)` - escreve no mesmo Console
  panel do editor (prefixo `[Lua]`).

Fisica (Box3D) e Input ainda nao existem na engine, entao scripts nao tem
acesso a nenhum dos dois ainda - proximos TODOs marcados direto no codigo
de `ScriptEngine::RegisterAPI()`.

**Tres callbacks opcionais** que um arquivo `.lua` pode definir no seu
escopo global: `OnCreate()`, `OnUpdate(deltaTime)`, `OnDestroy()`. Nenhum e
obrigatorio. Ver `PrismEditor/assets/ScriptExamples/example_spin.lua` para
um exemplo comentado completo (gira uma entidade em Y).

**Ciclo de vida / modo Play**: scripts SO rodam enquanto a Scene esta
"rodando" (`Scene::IsRunning()`) - fora disso a viewport do editor fica
estatica, exibindo so o estado editado (igual antes do scripting existir).
Um botao **Play/Parar** foi adicionado a direita da menu bar do editor:
Play chama `Scene::OnScriptsStart()` (carrega + `OnCreate()` de todo
`ScriptComponent` com um caminho preenchido); Parar chama
`Scene::OnScriptsStop()` (`OnDestroy()` + descarrega tudo). Isto NAO e
ainda o modo Play "de verdade" planejado no README (janela separada, com a
camera de jogo Primary) - e so o scripting rodando dentro da propria
viewport do editor, suficiente para testar um script sem mais UI. O modo
Play com janela separada fica para quando fizer sentido combinar os dois.

Trocar de mapa (Novo Mapa / carregar outro `.prismmap` pelo Content
Browser) enquanto rodando para os scripts da cena antiga primeiro
(`OnScriptsStop()`) antes de trocar `m_ActiveScene` - evita instancias
Lua "orfas" apontando para uma Scene que nao existe mais.

**Erros de script nunca derrubam o editor**: erro de sintaxe ao carregar,
ou erro de runtime em `OnCreate`/`OnUpdate`/`OnDestroy`, viram
`PRISM_CORE_ERROR` no Console - o script fica marcado como "com erro"
(`LoadedScript::HasRuntimeError`) e para de ser chamado a cada frame (evita
spammar o mesmo erro a 60fps), ate ser corrigido e recarregado.

**Recarregar um script individualmente**: com a cena rodando, a Properties
panel mostra um botao "Recarregar" na secao Script de uma entidade
selecionada - chama `ScriptEngine::LoadScript()` de novo so para aquela
entidade (chama `OnDestroy` da instancia antiga, depois `OnCreate` da
nova), sem precisar parar/tocar Play de novo para a cena inteira.

**Bibliotecas padrao do Lua abertas**: so `base`/`math`/`string`/`table` -
`io`/`os`/`package`/`debug` ficam de fora de proposito (um script de
gameplay nao deveria conseguir ler arquivos do disco ou rodar comandos do
SO so por existir numa Scene).

**Limitacoes conhecidas, deixadas de proposito**: sem hot-reload automatico
de arquivo (o botao "Recarregar" e manual); sem um painel de "scripts
ativos"/breakpoints/debugger; `entity:GetTransform()` nao inclui
ancestrais (retorna so o `TransformComponent` LOCAL, nao
`Scene::GetWorldTransform` - um script que precise da posicao de MUNDO
ainda nao tem essa funcao exposta); sem suporte a scripts chamarem funcoes
de OUTRAS entidades ainda (so a propria `entity` e visivel).

## Nota sobre Fisica (Box3D) - estado atual

**Correcao de bug "objetos se atravessam sem colidir" + gizmo gigantesco**
(investigado com logs de diagnostico temporarios - ver historico de commits
se precisar do processo completo): a fisica em si estava correta desde o
inicio - o problema real era `ColliderComponent::Size` ser uma medida em
UNIDADES ABSOLUTAS DE MUNDO, independente do `TransformComponent::Scale`
da entidade (ver comentario detalhado em `ColliderComponent::Size`,
`Components.h`). Se voce escala um mesh (ex: um Plane esticado para virar
um chao grande) sem tambem ajustar manualmente o `Size` do Collider na
Properties panel, o collider real usado pela fisica continua no default
(0.5 em cada eixo) - o objeto PARECE do tamanho certo visualmente, mas a
colisao de verdade e minuscula (ou grande demais) perto do mesh. Isso
sozinho ja explica "cai, quica de leve ao tocar uma esquina do collider
errado, depois atravessa".

Havia tambem um bug real separado, no GIZMO do collider (nao na fisica):
`EditorLayer::RenderSelectedColliderGizmo` desenhava o wireframe aplicando
a matriz de mundo COMPLETA da entidade (`Scene::GetWorldTransform`,
incluindo Scale) a pontos que ja sao calculados em unidades absolutas a
partir de `Collider::Size` - isso aplicava a escala da entidade DUAS
vezes sempre que `Scale != {1,1,1}`, fazendo o wireframe ficar gigantesco
e desalinhado do tamanho real usado pela fisica (que corretamente ignora
Scale). Corrigido extraindo so posicao+rotacao da matriz de mundo (sem
escala) antes de posicionar o gizmo - agora o wireframe reflete fielmente
o volume que `PhysicsEngine::CreateBodyForEntity` de fato cria no Box3D.

**Correcao de crash aplicada apos a primeira integracao** (se voce ja
tinha testado antes, este fix resolve um `__debugbreak()`/assert do
proprio Box3D ao apertar Play): `PhysicsEngine::CreateBodyForEntity`
extraia a rotacao inicial do corpo com `glm::quat_cast(worldMatrix)`
DIRETO da matriz de mundo, que ainda tinha a ESCALA da entidade embutida
nas colunas (`Translation * Rotation * Scale`, ver
`TransformComponent::GetTransform()`) - a menos que `Scale` fosse
exatamente `{1,1,1}`, isso produzia um quaternion nao normalizado/
distorcido, que o Box3D valida internamente e rejeita com um assert (o
crash reportado, `Core.c`: "instrucao de ponto de interrupcao"). Corrigido
normalizando as 3 colunas de rotacao da matriz (removendo a escala) ANTES
de extrair o quaternion, em vez de normalizar o quaternion depois (que
corrige a magnitude mas nao o eixo de rotacao distorcido por uma escala
nao-uniforme). Ver comentario `ATENCAO - bug corrigido` em
`PhysicsEngine::CreateBodyForEntity`.

**ATENCAO - Box3D esta em v0.1.0, alpha** (unica release existente,
30/jun/2026 - ver github.com/erincatto/box3d/releases). O autor
(erincatto, tambem autor do consagrado Box2D) pede para nao enviar pull
requests ainda; a API pode ganhar mudancas incompativeis em versoes
futuras sem aviso de depreciacao, como e comum em libs pre-1.0. Decisao
consciente de usar mesmo assim - ver `vendor/CMakeLists.txt` para o
raciocinio completo. `GIT_TAG` fixado em `v0.1.0` exato (nao `main`) de
proposito - atualizar e uma escolha manual, nunca automatica.

**Duas funcoes NAO confirmadas contra a documentacao oficial** no momento
em que esta integracao foi escrita: `b3CreateSphereShape`/
`b3CreateCapsuleShape` (para `ColliderShape::Sphere`/`Capsule`) e
`b3Body_SetLinearVelocity`. Foram implementadas seguindo o padrao
consistente do resto da API C do Box3D (e o padrao identico do Box2D
3.x), mas a doc publica so mostra o exemplo literal de `b3CreateHullShape`
(usado para `ColliderShape::Box`, esse sim 100% confirmado). Se os nomes
reais divergirem, o erro aparece como erro de COMPILACAO (funcao nao
encontrada) - facil de localizar e corrigir em
`build/_deps/box3d-src/include/box3d/*.h` apos o primeiro build. Ver
comentarios `ATENCAO` em `PhysicsEngine.cpp` nesses dois pontos.

`Prism::PhysicsEngine` (`Prism/src/Prism/Physics/PhysicsEngine.h/.cpp`)
cuida de um mundo Box3D (`b3WorldId`) por Scene "rodando" - criado em
`Scene::OnScriptsStart()` e destruido em `Scene::OnScriptsStop()`: fisica
e scripting ligam/desligam JUNTOS (o mesmo botao Play da menu bar liga os
dois - conceitualmente sao "a simulacao esta rodando ou nao").

**Como usar**: adicione `ColliderComponent` (Box/Sphere/Capsule, ver
Properties panel) E `RigidBodyComponent` (Static/Kinematic/Dynamic) a uma
entidade - Fisica so cria um corpo Box3D para entidades com AMBOS os
components. Aperte Play: entidades `Dynamic` caem sob gravidade e colidem
com `Static`/`Kinematic`, sincronizado de volta para
`TransformComponent.Translation/Rotation` a cada frame (ver
`PhysicsEngine::Simulate`).

**API exposta a scripts Lua** (ver `ScriptEngine::RegisterAPI`):
`entity:ApplyForce(x,y,z)`, `entity:ApplyImpulse(x,y,z)`,
`entity:GetVelocity()` (retorna um `Vec3`), `entity:SetVelocity(x,y,z)`.
Todas silenciosas (nao fazem nada) se a entidade nao tiver um corpo fisico
ativo no momento da chamada.

**Timestep fixo**: `PhysicsEngine::Simulate` usa um acumulador para rodar
`b3World_Step` em fatias fixas de 1/60s (recomendado pela doc oficial do
Box3D para estabilidade), independente do deltaTime variavel que a engine
recebe - protegido contra "espiral da morte" limitando a no maximo 5 steps
por frame (descarta o resto do acumulador se ultrapassar isso, preferindo
desacelerar a travar).

**Limitacoes conhecidas, deixadas de proposito**:
- **Collider::Size nao acompanha Scale automaticamente** (ver nota acima
  sobre o bug ja corrigido) - nao ha um botao "ajustar collider ao
  tamanho do mesh" ainda; o usuario precisa calcular/digitar o Size
  manualmente na Properties panel toda vez que redimensiona um objeto via
  Scale. Uma melhoria futura natural seria um botao que auto-preenche
  Size a partir do bounding box do mesh vezes a Scale atual.
- **Fisica + Parenting nao se combinam ainda**: o corpo Box3D e criado na
  posicao de MUNDO da entidade (`Scene::GetWorldTransform`, ancestrais
  inclusos), mas depois de criado e totalmente independente do parenting -
  a sincronizacao de volta escreve a posicao de mundo direto no
  `TransformComponent` LOCAL, o que da resultado ERRADO para uma entidade
  fisica que tambem seja filha de outra entidade na Hierarchy. Correto
  para o caso comum (entidade fisica sem pai). Corrigir isso exigiria
  multiplicar pela inversa da transform do pai a cada sincronizacao -
  adiado para manter o escopo desta primeira integracao controlado.
- **Sem callbacks de colisao em Lua ainda**: `PhysicsEngine::Simulate` ja
  le `b3World_GetBodyEvents` (posicao/rotacao) mas ainda NAO le
  `b3World_GetContactEvents` (begin/end touch) nem os traduz para
  `OnCollisionEnter`/`OnCollisionExit` no lado do script - o tipo
  `CollisionEvent` e o TODO ja estao la (ver `PhysicsEngine.h/.cpp`), so
  falta ligar.
- Sem UI no editor para visualizar/depurar o mundo fisico rodando
  (wireframes de collider, contagem de corpos ativos etc) alem do que ja
  existia antes (gizmo estatico do collider na viewport, ver parenting).
- Sem joints (revolute/distance/etc) expostos - Box3D suporta, mas nao ha
  nenhum ColliderComponent-equivalente para configurar um joint no editor
  ainda.
- `RigidBodyComponent::Mass` ainda nao e usado (a massa vem da densidade
  padrao de 1.0 aplicada ao shape, nao do campo `Mass` da propria
  entidade) - `b3Body_SetMassData`/`ApplyMassFromShapes` existem na API
  para isso, mas conectar o campo `Mass` da UI a eles ficou de fora do
  escopo desta etapa.

## Nota sobre a Janela de Play (estado atual)

O botao Play abre uma **janela separada do sistema operacional** (nao um
painel dockable dentro do editor - `PrismEditor::PlayWindow`, ver
`PrismEditor/src/PrismEditor/Play/PlayWindow.h/.cpp`), no mesmo estilo de
Godot/Unity/Source. Isto substitui a abordagem anterior (Play dentro da
propria viewport do editor, com popup de "salvar antes de rodar?" e
snapshot/restore via arquivo temporario) - a Scene de edicao (`m_ActiveScene`)
**nunca** e tocada por scripts/fisica agora, entao nao ha mais nada para
restaurar nem por que perguntar sobre salvar antes de rodar.

**Como funciona**: apertar Play clona `m_ActiveScene` (serializa para um
arquivo `.prismmap` temporario e desserializa de volta numa `Scene` nova -
reaproveitando o `SceneSerializer` ja existente, mesmo mecanismo que o
snapshot antigo usava, agora para "clonar" em vez de "salvar e restaurar")
e abre a janela do SO rodando essa copia com `Scene::OnScriptsStart()` ja
chamado. Apertar Parar (ou fechar a janela pelo X, ou apertar Esc dentro
dela) chama `Scene::OnScriptsStop()` na copia e destroi a janela - a copia
inteira e descartada, a Scene de edicao nunca foi tocada.

**Contexto OpenGL compartilhado**: a `PlayWindow` cria sua `GLFWwindow`
com o ultimo parametro de `glfwCreateWindow` (contexto a compartilhar)
apontando para a janela do editor - isso significa que toda a
geometria/shaders ja carregados pelo `Renderer` (VAOs/VBOs criados no
contexto do editor, ver `Renderer::Init()`) sao validos tambem na janela
de Play, sem precisar recarregar nada. `PlayWindow::OnUpdate` troca o
contexto ativo (`glfwMakeContextCurrent`) antes de desenhar nesta janela e
troca de volta para o editor ao final do frame - "object sharing" no GLFW
NAO inclui estado global de contexto (bind atual, viewport, depth test),
entao esse estado e reconfigurado a cada troca (ver comentario extenso em
`PlayWindow.h`).

**`Renderer::DrawScene`** e uma funcao nova, extraida do antigo
`EditorLayer::RenderSceneEntities` para dentro do `Renderer` - o mesmo
loop de desenho (toda entidade com Transform+MeshRenderer, usando a
transform de MUNDO) agora e compartilhado entre a viewport do editor, a
preview de camera, e a `PlayWindow`, sem duplicar a logica em varios
lugares.

**Limitacoes conhecidas, deixadas de proposito**:
- **Sem hot-reload de script durante o Play**: como a `PlayWindow` roda
  uma Scene CLONADA (entidades com handles `entt::entity` diferentes da
  Scene de edicao, ainda que correspondentes), nao ha como "recarregar"
  remotamente um script individual a partir da Properties panel do editor
  enquanto o Play esta rodando - editar o `.lua` e ver o efeito exige
  Parar e apertar Play de novo (a proxima clonagem ja pega o arquivo
  atualizado do disco).
- **Sem redimensionamento configuravel pela UI** ainda - a `PlayWindow`
  abre num tamanho fixo (1280x720); o usuario pode redimensionar a janela
  do SO manualmente como qualquer outra janela, mas nao ha um campo na UI
  do editor para escolher a resolucao inicial.
- **Uma unica `PlayWindow` por vez** - apertar Play de novo enquanto ja
  esta aberta nao faz nada (`PlayWindow::Open` recusa se `IsOpen()` ja for
  true); nao ha suporte a multiplas janelas de Play simultaneas (util para
  testar multiplayer local, por exemplo) ainda.
- Sem camera Primary na Scene clonada: a `PlayWindow` mostra so a cor de
  fundo (`Renderer::Clear`), sem nenhum erro - mesma limitacao que a
  preview de camera do editor ja tinha.


## Próximos passos sugeridos (nesta ordem)

1. ~~Framebuffer + renderização real da cena na Viewport panel.~~ ✅ feito
2. ~~Sistema de `Scene`/`Entity` (ECS via EnTT) + Hierarchy panel real.~~ ✅ feito
3. ~~Salvar/carregar `Scene` em disco.~~ ✅ feito
   (formato binário `.prismmap` dentro de `Project::GetMapDirectory()`;
   menu "Arquivo > Salvar Mapa" já funciona; a cena salva mais recentemente
   é reaberta automaticamente na próxima vez que o editor abre, via
   `ProjectConfig::StartMap`).
4. ~~Undo/Redo command stack.~~ ✅ feito
   (Ctrl+Z/Ctrl+Y globais, cobrindo mover/rotacionar/escalar entidade, mudar
   cor, criar e excluir entidade - ver `Prism::CommandHistory` e
   `PrismEditor/Commands/EditorCommands.h`).
5. ~~Console de verdade.~~ ✅ feito
   (le de `Prism::LogBuffer`, cor por nivel, filtro de texto, toggles de
   verbosidade, auto-scroll - ver `PrismEditor/Panels/ConsolePanel.h`).
6. ~~Content Browser (painel de arquivos do projeto).~~ ✅ feito
   (`PrismEditor/Panels/ContentBrowserPanel.h/.cpp` - navega Assets/Maps/
   Scripts/Cache a partir da raiz do projeto; duplo-clique num `.prismmap`
   já carrega aquele mapa na Scene ativa).
7. ~~Sistema de salvamento melhor.~~ ✅ feito
   ("Salvar Como" com popup de nome, aviso de sobrescrita, múltiplos mapas
   por projeto, "Novo Mapa" funcional, atalhos Ctrl+S/Ctrl+Shift+S - ver
   nota abaixo).
8. ~~Entidades mais robustas.~~ ✅ feito (parcialmente - ver nota abaixo)
   (`LightComponent`, `ColliderComponent`, `RigidBodyComponent`,
   `ScriptComponent` novos; Properties panel virou "Add/Remove Component";
   presets no menu Entidade: Luz, Character, Entidade Vazia).
9. ~~Mais meshes básicas.~~ ✅ feito
   (Sphere, Capsule, Cylinder, Plane, além do Cube - ver nota abaixo).
10. ~~`CameraComponent` funcional (edição completa + gizmo visual na
    viewport).~~ ✅ feito (ver nota abaixo). Modo "Play" fica para depois,
    como uma janela separada (ver nota abaixo) - não dentro do viewport do
    editor.
11. ~~Parenting / hierarquia real (arvore de entidades).~~ ✅ feito
    (`RelationshipComponent`, `Scene::SetParent`/`GetWorldTransform`,
    Hierarchy panel com drag-and-drop, `.prismmap` v4 - ver nota acima).
12. ~~Embutir Lua (ex: via `sol2` ou `LuaBridge`) + primeiro script rodando.~~ ✅ feito
    (`Prism::ScriptEngine`, botao Play/Parar na menu bar, API minima de
    Transform + log - ver nota "Scripting Lua" acima).
13. ~~Integrar Box3D, corpos rígidos básicos.~~ ✅ feito (fixado em v0.1.0
    alpha - `Prism::PhysicsEngine`, corpos Dynamic/Static/Kinematic com
    Box/Sphere/Capsule, ApplyForce/Impulse expostos ao Lua - ver nota
    "Fisica (Box3D)" acima).
14. ~~Janela de Play separada (própria janela/viewport, câmera Primary do
    jogo, distinta da viewport de edição).~~ ✅ feito (janela real do SO
    com contexto OpenGL compartilhado, Scene clonada - ver nota "Janela de
    Play" acima).
15. Sistema de iluminação de verdade (hoje só um ponto de luz fixo,
    hardcoded - sem suporte a spotlight/omnilight configuráveis por
    cena/jogo, como o guia do protótipo pede).
16. IDE/editor de código embutido (scripting sem sair do editor).
17. BSP/CSG (brushes como um tipo de Entity no editor).

## Nota sobre `CameraComponent` funcional (estado atual)

`CameraComponent` (`Prism/src/Prism/Scene/Components.h`) ganhou
`ProjectionType` (Perspective/Orthographic), `GetProjection(aspectRatio)`
(monta a matriz de projeção a partir dos campos do component) e a regra de
"no máximo uma câmera `Primary` por Scene" - imposta na UI, não no
component em si (ver `EditorLayer::SetPrimaryCamera`).

**Viewport do editor**: sempre usa a câmera de órbita livre
(`EditorLayer::RenderScene`) - nunca é substituída pela câmera de jogo,
mesmo quando existe uma `Primary` (ver correção abaixo). A câmera de jogo
Primary é renderizada à parte, no painel "Camera"
(`EditorLayer::RenderCameraPreview`): `view = inverse(Transform)`,
`projection = CameraComponent::GetProjection` - a mesma lógica que o modo
Play vai usar depois numa janela separada, então o que você edita já é o
que o jogo vai ver (WYSIWYG) - só que numa preview própria, não na
viewport principal.

**Gizmo visual**: toda entidade com `CameraComponent` desenha um frustum em
wireframe na viewport (`EditorLayer::RenderCameraGizmos`), via um novo
`Renderer::DrawLines` (shader de linha dedicado, sem iluminação, VAO/VBO
próprios reescritos a cada chamada - ver `Renderer.h/.cpp`). A câmera
`Primary` aparece em ciano; as demais em cinza, para diferenciar de relance
qual câmera o modo Play vai usar quando há mais de uma na cena. O tamanho do
frustum desenhado é fixo (não usa o `FarClip` real, que pode ser enorme) -
é só uma indicação visual de posição/direção/abertura, não uma preview
exata.

**Properties panel**: nova seção "Camera" (Projeção, FOV ou Tamanho
Ortográfico dependendo do tipo, Near/Far Clip, checkbox Primary). Marcar
Primary numa câmera desmarca qualquer outra automaticamente
(`SetPrimaryCamera`). Preset "Criar Câmera" no menu Entidade e opção
"Camera" no botão "+ Add Component" - os dois já cuidam de manter só uma
Primary por cena.

**Formato de arquivo**: `.prismmap` subiu para **v3** (`CameraComponent`
adicionado, mesmo padrão de flag de presença dos outros components
opcionais) - mapas salvos em v2 precisam ser resalvos uma vez.

**Correção importante (depois do primeiro teste)**: a primeira versão desta
feature fazia a viewport principal do editor ser SUBSTITUÍDA pela câmera de
jogo quando ela existia como Primary - isso causava um problema sério
sempre que a câmera ficava posicionada dentro de outro mesh (ex: uma câmera
de personagem dentro da cápsula de colisão do Character): a viewport
passava a mostrar o interior/face de trás da geometria (culling normal
fazendo seu trabalho), sem nenhuma visão de trabalho disponível para
corrigir a posição. Corrigido: agora a viewport principal usa SEMPRE a
câmera de órbita livre do editor, do mesmo jeito que Unity/Unreal/Godot
fazem - a câmera de jogo Primary ganhou seu próprio painel de preview
("Camera"), renderizado num framebuffer separado
(`m_CameraPreviewFramebuffer`), então dá pra ver os dois ao mesmo tempo e
ajustar a posição da câmera olhando a preview.

**Limitações conhecidas, deixadas de propósito**: os campos da seção Camera
ainda não geram comandos individuais de undo por edição (mesma limitação já
documentada para Light/Collider/RigidBody). Sem picking por clique no gizmo
ainda (selecionar a câmera continua sendo só pela Hierarchy panel). Sem
suporte a `AspectRatio` fixo/customizado por câmera - sempre usa o aspect
ratio do painel Viewport (ou da janela do modo Play, quando existir). O
painel Camera não tem um aviso/indicador de "atravessando geometria" (ex:
destacar em vermelho quando a posição da câmera está dentro de outro
Collider) - hoje isso só é visível olhando a imagem da preview.

## Gizmo de Collider (Box/Sphere/Capsule)

Toda entidade com `ColliderComponent` já guardava `Shape` (Box, Sphere ou
Capsule) e `Size` como dado (ver comentário em `Components.h`), mas não
havia nenhuma forma de VER essa forma de colisão no editor - por exemplo, a
cápsula de colisão de um Character/player. Isso dificultava saber onde
exatamente ela está (ex: para posicionar uma `CameraComponent` fora dela,
em vez de dentro - ver seção da Camera acima).

`EditorLayer::RenderSelectedColliderGizmo` desenha o wireframe do Collider
da entidade **atualmente selecionada** (não de todas as entidades com
Collider da cena, para não poluir a viewport) - clique na entidade na
Hierarchy panel (ou depois, quando existir picking por clique na viewport)
para ver o gizmo, em amarelo. Box desenha as 12 arestas de uma caixa;
Sphere desenha 3 círculos ortogonais (XY/XZ/YZ); Capsule desenha uma
aproximação cilíndrica (2 círculos + 4 linhas verticais, sem as calotas
hemisféricas de verdade - suficiente para ver posição/raio/altura, não uma
representação geometricamente exata). O gizmo usa a matriz de mundo
completa da entidade (`TransformComponent::GetTransform()`), então
acompanha posição, rotação e escala.

Com isso, o fluxo para tirar uma câmera de dentro do collider de um
Character fica: selecionar o Character (vê a cápsula em amarelo na
viewport) → selecionar a entidade da câmera → editar `Translation` dela na
Properties panel (seção Transform, sempre presente) → acompanhar em tempo
real no painel "Camera" o momento em que ela sai de dentro da cápsula.

## Correção: cápsula do gizmo parecia cilindro

A primeira versão do gizmo de Capsule (seção acima) desenhava só o "meio"
(2 círculos + 4 linhas verticais) - sem as calotas hemisféricas, então
visualmente parecia um cilindro em vez de uma cápsula. Corrigido: cada
calota agora desenha 4 meridianos de 180° (`appendArc`, dois planos
ortogonais XY e ZY), formando a cúpula de verdade em cada ponta - ainda uma
aproximação (não é uma malha completa de esfera), mas já lê como "cápsula"
de relance.

## Correção: face interna dos meshes sólida em vez de transparente

Mesmo com a preview separada da câmera (seção acima), o problema de fundo
persistia: se a câmera ficasse posicionada dentro de outro mesh (ex: dentro
da cápsula de colisão de um Character), a preview mostrava a face INTERNA
do mesh, sólida - porque o Renderer nunca fazia nenhum tipo de culling,
então ambas as faces de qualquer triângulo eram sempre desenhadas.

Corrigido dentro do fragment shader do `s_BasicShader` (`Renderer.cpp`): a
cada fragmento, calcula `dot(normal, viewDir)` (`viewDir` = direção da
câmera atual até aquele ponto) e faz `discard` quando o resultado é
negativo - ou seja, a face que está de costas para a câmera simplesmente
não é desenhada, exatamente como um backface culling normal faria, só que
via shader em vez de `glCullFace`.

**Por que via shader em vez de `glCullFace(GL_BACK)`**: seria a forma
"padrão"/mais barata, mas descobri que as primitivas atuais
(`PrimitiveMeshFactory`) não têm winding order (CW/CCW) consistente entre
si - o Cube é CCW visto de fora, mas Sphere/Capsule/Cylinder (gerados por
UV-mapping lat/lon) saem com o winding oposto. Ligar `glCullFace`
diretamente faria essas primitivas sumirem inteiras (ou mostrarem a face
errada) em vez de só esconder a face interna. O teste por produto escalar
funciona igual não importa o winding de cada mesh, então foi a correção
seguro de se fazer agora. Uma limpeza futura (normalizar o winding de cada
`PrimitiveMeshFactory::Create*` e trocar para `glCullFace`, mais barato
para a GPU) fica registrada aqui como possível otimização, não como bug -
para o volume de geometria que este protótipo desenha hoje a diferença de
custo é desprezível.

Nova função `Renderer::SetCameraPosition(worldPos)` - precisa ser chamada
uma vez por framebuffer, antes de qualquer `DrawMesh()` daquele
framebuffer (viewport principal usa a posição da câmera de órbita; a
preview da câmera usa `Translation` da própria entidade Primary - ver
`EditorLayer::RenderScene`/`RenderCameraPreview`).

## Nota sobre o Console (estado atual)

`Prism::LogBuffer` (`Prism/src/Prism/Core/LogBuffer.h/.cpp`) se conecta como
um *sink* opcional do `Log` (`Log::SetSink`, instalado em
`Application::Application` antes de qualquer log relevante acontecer) e
guarda as últimas 2000 mensagens num buffer circular. Nenhuma mudança na API
de log existente (`PRISM_INFO`, `PRISM_CORE_ERROR`, etc.) — quem chama essas
macros não sabe nem precisa saber que existe um Console; a engine standalone
(fora do editor) continua funcionando exatamente como antes.

`PrismEditor::ConsolePanel` (`PrismEditor/src/PrismEditor/Panels/ConsolePanel.h`)
lê esse buffer a cada frame e desenha: cor por nível (cinza/branco/amarelo/
vermelho), filtro de texto case-insensitive, toggles para esconder
Trace/Info/Warn/Error (Critical fica sempre junto de Error), auto-scroll
inteligente (só desce sozinho se você já estava perto do fim — rolar pra
cima pra ler algo antigo não é interrompido por mensagens novas), e um botão
Limpar.

## Nota sobre Undo/Redo (estado atual)

`Prism::Command`/`Prism::CommandHistory` (`Prism/src/Prism/Core/`) ficam no
CORE da engine de propósito — qualquer ferramenta futura que edite estado
(editor de brushes/BSP, editor de materiais) vai precisar do mesmo
mecanismo. Os comandos concretos (`TransformCommand`, `MeshColorCommand`,
`CreateEntityCommand`, `DeleteEntityCommand`) ficam do lado do Editor
(`PrismEditor/src/PrismEditor/Commands/EditorCommands.h`), porque conhecem
`Entity`/`Scene` do fluxo de edição específico.

Para edições contínuas via `DragFloat3`/`ColorEdit3` (arrastar posição,
escolher cor), o editor captura o estado "antes" quando o gesto **começa**
(`ImGui::IsItemActivated()`) e só empurra **um** comando no histórico quando
o gesto **termina** (`ImGui::IsItemDeactivatedAfterEdit()`) — arrastar a
posição não gera um comando por frame, só um comando por gesto completo.

Trocar de cena (carregar um mapa do disco) limpa o histórico
(`CommandHistory::Clear()`), já que um `Undo()` não tem como desfazer ações
sobre entidades de uma cena que não está mais em memória.

Limitação conhecida: não há ainda um atalho Ctrl+S para salvar (fica para
quando existir um sistema de atalhos mais genérico); Ctrl+Z/Ctrl+Y já
funcionam globalmente, exceto enquanto o ImGui está capturando texto (ex:
editando o campo "Nome"), onde o undo nativo do campo de texto tem
prioridade.

## Nota sobre o Content Browser (estado atual)

`PrismEditor::ContentBrowserPanel` navega a partir de
`Project::GetProjectDirectory()` (a raiz inteira do projeto, não só
`Assets/`) — assim dá pra ver `Maps/` e `Scripts/` também. Duplo-clique numa
pasta entra nela; botão "< Voltar" sobe um nível (desabilitado na raiz);
duplo-clique num `.prismmap` chama `EditorLayer::LoadScene()`, que substitui
a Scene ativa pela do arquivo clicado (sem perguntar "salvar antes?" ainda —
ver limitação abaixo).

Limitações conhecidas, deixadas de propósito para não expandir escopo agora:
sem criar/renomear/excluir/arrastar arquivos pelo painel (isso se conecta
naturalmente ao fluxo de importação de assets do guia do protótipo, que
ainda não existe); sem confirmação de "salvar antes de trocar de mapa" —
carregar outro mapa (seja pelo Content Browser, seja por "Novo Mapa") perde
qualquer alteração não salva na cena atual, sem aviso — fica para quando
existir rastreamento de "alterações não salvas" (dirty flag).

## Nota sobre as novas primitivas de mesh (estado atual)

Cinco primitivas embutidas agora: Cube, Sphere, Capsule, Cylinder, Plane
(`PrimitiveMesh` em `Components.h`). A geometria de cada uma é gerada uma
única vez, na CPU, por `Prism::PrimitiveMeshFactory`
(`Prism/src/Prism/Renderer/PrimitiveMeshFactory.h/.cpp` — matemática pura,
nenhuma chamada OpenGL) e enviada para a GPU como um `Prism::Mesh`
(`Renderer/Mesh.h/.cpp` — dono de VAO/VBO/EBO genérico) dentro de
`Renderer::Init()`. O `Renderer` mantém uma malha de GPU por primitiva,
reaproveitada por toda entidade que usar aquele tipo — não há geração nem
alocação por entidade ou por frame.

A Capsule é construída como uma esfera "esticada": os anéis de latitude da
metade de cima são deslocados para `+halfHeight` e os da metade de baixo
para `-halfHeight`, criando o trecho cilíndrico reto entre as duas
meias-esferas. A forma (raio 0.5, altura 1.0) já bate com o preset "Criar
Character" do menu Entidade, que usa `ColliderShape::Capsule` com essas
mesmas dimensões.

A Properties panel agora tem um combo "Mesh" no `Mesh Renderer` para trocar
a primitiva de uma entidade existente a qualquer momento — como os outros
campos de Light/Collider/RigidBody, essa troca ainda não gera um comando
individual de undo (mesma limitação já documentada acima).

## Nota sobre modo "Play" (planejado, ainda não implementado)

Diferente de Unity/Unreal (que rodam o jogo inline dentro do viewport do
editor), o modo Play da Prism Engine vai abrir uma **janela separada**
(nova janela GLFW) rodando a `Scene` ativa a partir da `CameraComponent`
marcada como `Primary` — o editor continua aberto do lado, sem precisar
duplicar framebuffers ou gerenciar duas fontes de câmera dentro da mesma
janela. É o mesmo modelo que engines baseadas em Source/Hammer usam
(compilar → o jogo roda numa janela própria). Isso ainda não está
implementado — é a direção planejada para quando o modo Play for
construído, depois de `CameraComponent` funcional e do scripting em Lua.

## Nota sobre Entidades mais robustas / Add Component (estado atual)

**Decisão de arquitetura**: em vez de "tipos de entidade" fixos (Prop,
Character, Light como um enum rígido), a engine continua 100% ECS livre —
uma entidade não "é" um tipo, ela é só a soma dos components que tem. Um
Character é uma entidade com `MeshRendererComponent` + `ColliderComponent` +
`RigidBodyComponent` + `ScriptComponent`; uma Light estática é só
`LightComponent`; uma tocha (luz + prop físico) é as duas coisas juntas sem
nenhum caso especial. É o mesmo modelo que Unity/Unreal usam por baixo. Os
"presets" do menu Entidade (Criar Luz, Criar Character, Criar Entidade
Vazia) são só atalhos de conveniência que já adicionam a combinação comum de
components — não travam nada; qualquer component pode ser adicionado ou
removido depois pela Properties panel.

**Novos components** (`Prism/src/Prism/Scene/Components.h`):
- `LightComponent` — tipo (Point/Spot/Directional), cor, intensidade,
  alcance, ângulo do cone.
- `ColliderComponent` — forma (Box/Sphere/Capsule), tamanho, flag de
  trigger.
- `RigidBodyComponent` — tipo de corpo (Static/Kinematic/Dynamic), massa,
  gravidade, CCD — nomes escolhidos já pensando no Box3D (próximo item do
  roadmap).
- `ScriptComponent` — só um caminho relativo para um arquivo `.lua` dentro
  de `Scripts/`. Ainda **não executa nada** — é o slot de dado que a UI e o
  formato de arquivo já suportam, para quando Lua for embutido não precisar
  de outra rodada de migração de `.prismmap`s salvos.

**Properties panel** virou "Add/Remove Component": cada component vira uma
seção colapsável com um "X" para remover (exceto Transform, que toda
entidade tem por definição); um botão "+ Add Component" no final abre um
popup só com os components que a entidade ainda não tem. Tudo passa pelo
`CommandHistory` (Ctrl+Z desfaz adicionar/remover um component inteiro).

**Limitações conhecidas, deixadas de propósito**: os campos numéricos de
Light/Collider/RigidBody (drag floats, combos, checkboxes) ainda **não**
geram comandos individuais de undo por edição — diferente de
Transform/Cor, que capturam um `TransformCommand`/`MeshColorCommand` por
gesto de arraste. Adicionar isso para mais ~15 campos infestaria bastante
esta etapa; fica para uma passada futura se se mostrar necessário na
prática. `LightComponent` ainda não afeta a renderização de verdade (o
Renderer só tem uma luz direcional fixa hardcoded no shader) —
`ColliderComponent`/`RigidBodyComponent` ainda não alimentam nenhuma
simulação física — ambos aguardam os próximos itens do roadmap (iluminação
de verdade / Box3D). Sem parenting/hierarquia real ainda (a Hierarchy panel
continua sendo uma lista plana, não uma árvore).

## Nota sobre Salvar/Salvar Como/Novo Mapa (estado atual)

`EditorLayer` agora rastreia `m_CurrentMapPath` — o arquivo `.prismmap`
associado à cena ativa, vazio quando a cena ainda não foi salva em lugar
nenhum (cena nova, ou a cena de exemplo do primeiro `OnAttach`).

- **Salvar Mapa** (Ctrl+S): grava em `m_CurrentMapPath` se ele já existe;
  caso contrário, se comporta como Salvar Como (não há "onde" sobrescrever
  ainda).
- **Salvar Como...** (Ctrl+Shift+S): sempre abre um popup pedindo um nome,
  mostra um preview do caminho final, avisa em amarelo se um mapa com esse
  nome já existe (seria sobrescrito), e salva num arquivo **novo** dentro de
  `Project::GetMapDirectory()` — nunca sobrescreve outro mapa sem avisar.
  Depois de salvar, esse novo mapa também vira o `StartMap` do projeto (é
  reaberto automaticamente da próxima vez).
- **Novo Mapa**: cria uma `Scene` vazia e limpa `m_CurrentMapPath` — a
  próxima vez que "Salvar Mapa" for usado, pede um nome (mesma lógica do
  primeiro save).

Múltiplos mapas por projeto já funcionam na prática: cada "Salvar Como" com
um nome diferente cria um arquivo `.prismmap` separado dentro de `Maps/`,
todos navegáveis pelo Content Browser — só falta uma UI dedicada para listar
"todos os mapas do projeto" fora do Content Browser genérico, se isso vier a
fazer falta.

## Nota sobre persistência de Scene (estado atual)

`Prism::SceneSerializer` (`Prism/src/Prism/Scene/SceneSerializer.h/.cpp`)
salva/carrega uma `Scene` inteira em um único arquivo binário `.prismmap`,
com um cabeçalho `magic + versão` (`kSceneFormatVersion`) para detectar
arquivos corrompidos ou de um formato futuro incompatível — hoje qualquer
versão diferente da atual é recusada (sem migração automática ainda).
**Formato atual: v2** (v1 não é mais lido — projetos criados antes dos novos
components de Light/Collider/RigidBody/Script, ver seção acima, precisam ser
resalvos uma vez).

Fluxo no editor: `EditorLayer::LoadOrCreateScene()` tenta carregar
`ProjectConfig::StartMap`; se não existir (projeto novo), cria a cena de
exemplo em memória de sempre. `EditorLayer::SaveActiveScene()` (menu
Arquivo > Salvar Mapa) grava o arquivo e, no primeiro save, também chama
`Project::SetStartMap()` para lembrar qual mapa reabrir da próxima vez.

Limitações conhecidas, deixadas de propósito para não expandir escopo agora:
não há atalho de teclado Ctrl+S funcional ainda (precisa de um sistema de
Input por polling, que ainda não existe — ver nota em `EditorLayer::OnEvent`);
não há suporte a múltiplos mapas por projeto nem uma janela "Salvar como"
(o nome do arquivo vem do nome da cena, automaticamente, no primeiro save).

## Nota sobre a Scene/ECS (estado atual)

`Scene`/`Entity` usam [EnTT](https://github.com/skypjack/entt) por baixo
(header-only, baixado via `FetchContent` — não precisa instalar nada à
parte). `Entity` é só um par (ID, `Scene*`); todo dado real vive em
*components* (`Prism/src/Prism/Scene/Components.h`): `TagComponent`,
`TransformComponent`, `MeshRendererComponent`, e um `CameraComponent` ainda
não usado (reservado para quando existir um modo "Play" com câmera de jogo,
distinta da câmera de órbita do editor).

Toda entidade nasce com `TagComponent` + `TransformComponent` (ver
`Scene::CreateEntity`). O "sistema" que desenha a cena é só uma função que
itera `registry.view<TransformComponent, MeshRendererComponent>()` — ver
`EditorLayer::RenderScene()`. Adicionar um novo tipo de mesh primitivo
(esfera, plano) é: adicionar ao `enum PrimitiveMesh`, ensinar `Renderer` a
desenhar essa geometria, e adicionar um `case` no `switch` de
`RenderScene()`.

## Nota sobre a Viewport (estado atual do renderer)

O `Renderer` hoje é propositalmente mínimo: um shader único com iluminação
direcional simples, sabendo desenhar só um tipo de primitiva (cubo unitário,
`Renderer::DrawTestCube`). Isso existe para provar o caminho completo
`Framebuffer → Scene/ECS → Shader/Renderer → ImGui::Image` funcionando de
ponta a ponta. O "Renderizador principal" definitivo (forward vs deferred,
batching, materiais de verdade, importação de meshes) ainda está em aberto,
conforme o guia do protótipo.
