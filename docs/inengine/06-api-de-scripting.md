# API de scripting (Lua e C++)

Referência única das duas linguagens. Cada tabela mostra a mesma função em **Lua** e em **C++**. Um "—" significa que a função **ainda não existe** naquela linguagem.

> **Nota:** o C++ está se tornando a linguagem principal da Prism, e o Lua passa a ser uma opção. Por isso os "—" na coluna C++ devem sumir aos poucos, à medida que o scripting em C++ ganha o que o Lua já tem. Guias: [Scripting em Lua](04-scripting-lua.md) e [Scripting em C++](05-scripting-cpp.md).

Convenções: em Lua, `entity` é a entidade dona do script e as funções usam `entity:Funcao()`. Em C++, as funções são métodos da sua classe (derivada de `ScriptableEntity`) e `GetEntity()` devolve a entidade dona. A versão atual da API nativa é a **3**.

## Ciclo de vida

| Evento | Lua | C++ |
|---|---|---|
| Ao iniciar o Play | `function OnCreate()` | `void OnCreate() override` |
| A cada frame | `function OnUpdate(dt)` | `void OnUpdate(float dt) override` |
| Passo fixo (1/60 s, antes da física) | `function OnFixedUpdate(dt)` | `void OnFixedUpdate(float dt) override` |
| Depois de um hot reload (só Lua) | `function OnReload()` | — |
| Ao destruir / parar o Play | `function OnDestroy()` | `void OnDestroy() override` |
| Começou a colidir | `function OnCollisionEnter(other)` | `void OnCollisionEnter(Entity other) override` |
| Terminou a colisão | `function OnCollisionExit(other)` | `void OnCollisionExit(Entity other) override` |
| Entrou num Trigger | `function OnTriggerEnter(other)` | `void OnTriggerEnter(Entity other) override` |
| Saiu de um Trigger | `function OnTriggerExit(other)` | `void OnTriggerExit(Entity other) override` |
| Botão de UI clicado | `function OnClick()` | `void OnClick() override` |
| Variáveis públicas | `Properties = { ... }` | — |

## Entidade e Transform

Rotação sempre em **graus**.

| Função | Lua | C++ |
|---|---|---|
| Ler/alterar Transform | `entity:GetTransform()` (`Translation`, `Rotation`, `Scale`) | `GetTransform()` (`.Translation`, `.Rotation`, `.Scale`) |
| Posição | `entity:SetPosition(x, y, z)` | `GetTransform().Translation = {x, y, z}` |
| Rotação | `entity:SetRotation(x, y, z)` | `GetTransform().Rotation = {x, y, z}` |
| Escala | `entity:SetScale(x, y, z)` | `GetTransform().Scale = {x, y, z}` |
| Mover relativo | `entity:Translate(x, y, z)` | `GetTransform().Translation += glm::vec3(x, y, z)` |
| Posição no mundo | `entity:GetWorldPosition()` | — |
| Frente / direita (local) | `entity:GetForward()`, `entity:GetRight()` | — |
| Frente / direita (mundo) | `entity:GetWorldForward()`, `entity:GetWorldRight()` | — |

## Identificação e hierarquia

| Função | Lua | C++ |
|---|---|---|
| Nome | `entity:GetName()` / `entity:SetName(n)` | `GetComponent<TagComponent>().Tag` |
| Entidade válida? | `entity:IsValid()` | `entity.IsValid()` |
| Pai | `entity:GetParent()` / `entity:SetParent(p)` | `entity.GetParent()` / — |
| Nº de filhos | `entity:GetChildCount()` | `entity.GetChildCount()` |
| Filho por índice | `entity:GetChildAt(i)` | `entity.GetChildAt(i)` |
| Filho por nome | `entity:GetChild("nome")` | `entity.GetChild("nome")` |
| Comparar entidades | `==` | `==` |
| Entidade dona | `entity` | `GetEntity()` |
| Destruir a si mesma | `entity:Destroy()` | `Destroy()` |
| Script de outra entidade | `entity:GetScript()` | — |

## Components

| Component | Lua | C++ |
|---|---|---|
| Mesh | `entity:GetMeshRenderer()` | `GetComponent<MeshRendererComponent>()` |
| Material | `entity:GetMaterial()` | — (campos do material via component) |
| Luz | `entity:GetLight()` | `GetComponent<LightComponent>()` |
| Câmera | `entity:GetCamera()` | `GetComponent<CameraComponent>()` |
| Ambiente da cena | `entity:GetEnvironment()` | `GetComponent<EnvironmentComponent>()` |
| Fonte de áudio | `entity:GetAudioSource()` | `GetComponent<AudioSourceComponent>()` (`Volume`, `Pitch`) |
| Emissor de partículas | `entity:GetParticleEmitter()` | `GetComponent<ParticleEmitterComponent>()` |
| UI | `entity:GetUI()` | `GetComponent<UIElementComponent>()` (`Text`, `Color`...) |
| Tem component? | — | `HasComponent<T>()` |
| Acesso à cena | — | `GetScene()` |

## Cena

| Função | Lua | C++ |
|---|---|---|
| Achar por nome | `Scene.Find("nome")` | `FindEntity("nome")` |
| Achar todas por nome | `Scene.FindAll("nome")` | — |
| Criar entidade vazia | `Scene.Create("nome")` | `CreateEntity("nome")` |
| Criar com mesh | `Scene.CreateMesh("nome", ...)` | — |
| Instanciar prefab | `Scene.Instantiate(caminho, pos)` | `Instantiate(caminho)` / `Instantiate(caminho, pos)` |
| Destruir outra entidade | `Scene.Destroy(e)` | `Destroy(entidade)` |
| Trocar de cena | — | — |

A destruição é adiada para o fim do frame, nas duas linguagens.

## Física

| Função | Lua | C++ |
|---|---|---|
| Aplicar força | `entity:ApplyForce(x, y, z)` | — |
| Aplicar impulso | `entity:ApplyImpulse(x, y, z)` | `ApplyImpulse(vec3)` |
| Ler velocidade | `entity:GetVelocity()` | `GetVelocity()` |
| Definir velocidade | `entity:SetVelocity(x, y, z)` | `SetVelocity(vec3)` |
| Raycast | `Physics.Raycast(origem, direcao, maxDist, ignorar)` | `Raycast(origem, direcao, maxDist = 1000, ignorarSelf = true)` |
| Resultado do raycast | `hit.Hit`, `hit.Point`, `hit.Normal`, `hit.Distance`, `hit:GetEntity()` | `NativeRayHit`: `Hit`, `Point`, `Normal`, `Distance`, `Entity` |

## Entrada

| Função | Lua | C++ |
|---|---|---|
| Tecla segurada | `Input.IsKeyDown(Key.W)` | `IsKeyDown(Key::W)` |
| Tecla apertada neste frame | `Input.IsKeyPressed(Key.Space)` | `IsKeyPressed(Key::Space)` |
| Botão do mouse | `Input.IsMouseButtonDown(Mouse.Left)` | `IsMouseButtonDown(MouseButton::Left)` |
| Delta do mouse | `Input.GetMouseDeltaX()` / `GetMouseDeltaY()` | `GetMouseDeltaX()` / `GetMouseDeltaY()` |
| Posição do mouse | `Input.GetMouseX()` / `GetMouseY()` | — |
| Modo do cursor | `Input.SetCursorMode(CursorMode.Locked)` / `GetCursorMode()` | — |

## Tempo e matemática

| Função | Lua | C++ |
|---|---|---|
| Delta time | `Time.DeltaTime` | `GetDeltaTime()` (ou o `dt` de `OnUpdate`) |
| Tempo total | `Time.Elapsed` | `GetElapsedTime()` |
| Passo fixo | `Time.FixedDeltaTime` | `GetFixedDeltaTime()` |
| Contador de frames | `Time.FrameCount` | `GetFrameCount()` |
| Vetor 3D | `Vec3.new(x, y, z)` (operadores, `Length`, `Normalized`, `Dot`, `Cross`, `Distance`) | `glm::vec3` e funções do GLM |
| Utilitários | `Mathf.Clamp`, `Mathf.Lerp`, `Mathf.Sign` | `glm::clamp`, `glm::mix`, `<cmath>` |

## Áudio

| Função | Lua | C++ |
|---|---|---|
| Tocar o som da entidade | `entity:PlaySound()` | `PlaySound()` |
| Parar | `entity:StopSound()` | `StopSound()` |
| Está tocando? | `entity:IsSoundPlaying()` | — |
| Volume / pitch da entidade | `entity:SetSoundVolume(v)` / `SetSoundPitch(p)` | `GetComponent<AudioSourceComponent>().Volume` / `.Pitch` |
| Tocar arquivo avulso | `Audio.Play(caminho)` | — |
| Tocar em 3D | `Audio.Play3D(caminho, x, y, z)` | — |
| Volume geral | `Audio.SetMasterVolume(v)` / `GetMasterVolume()` | — |
| Volume por canal | `Audio.SetBusVolume("music", v)` / `GetBusVolume` / `SetBusMuted` | — |
| Carregar antes | `Audio.Preload(caminho)` | — |

## Partículas

| Função | Lua | C++ |
|---|---|---|
| Iniciar | `entity:PlayParticles()` | `PlayParticles()` |
| Parar | `entity:StopParticles()` | `StopParticles()` |
| Limpar | `entity:ClearParticles()` | — |
| Está tocando? | `entity:IsParticlesPlaying()` | — |
| Contagem | `entity:GetParticleCount()` | — |

## UI

| Função | Lua | C++ |
|---|---|---|
| Component de UI | `entity:GetUI()` | `GetComponent<UIElementComponent>()` |
| Mouse sobre a UI? | `UI.IsPointerOverUI()` | — |
| Clique de botão | `function OnClick()` | `void OnClick() override` |

## Log

| Função | Lua | C++ |
|---|---|---|
| Informação | `log("texto")` / `print(...)` | `LogInfo("texto")` |
| Aviso | `log_warn("texto")` | `LogWarn("texto")` |
| Erro | `log_error("texto")` | `LogError("texto")` |

## Bibliotecas e limites

- **Lua:** só `base`, `math`, `string` e `table` estão abertas (sem arquivos nem comandos do sistema).
- **C++:** o módulo não linka com a engine; funções que dependem dela passam pelos ajudantes acima. Os components são só cabeçalho e podem ser usados direto.
- Nenhuma das duas linguagens troca de cena ainda.
