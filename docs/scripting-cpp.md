# Scripting C++

> No início do desenvolvimento a Prism só suportava Lua; agora suporta C++, e o scripting está sendo trabalhado para que o C++ se torne a linguagem principal, com o Lua como uma opção.

Scripts de gameplay em C++ ao lado dos scripts Lua. O código do projeto vira uma biblioteca compartilhada (`NativeScripts.dll` / `.so` / `.dylib`) que o editor compila e carrega em runtime.

## Fluxo

1. `<Projeto>/Source/` (`CMakeLists.txt`, `PrismGenerated.cmake`, `PrismModuleEntry.cpp` e o exemplo `Spinner.cpp`) é gerado por `NativeScripting::GenerateProjectFiles` na criação do projeto (`ProjectManagerLayer::CreateAndOpenProject`) e, para projetos antigos, ao abri-los (`OpenProject`). O botão **Compilar** também gera se `Source/CMakeLists.txt` não existir. Só o `PrismGenerated.cmake` é regravado sempre (ele guarda o caminho do SDK da engine); o resto **nunca é sobrescrito**, então o `CMakeLists.txt` é seu (ver [Usando bibliotecas](#usando-bibliotecas-de-terceiros)).
2. Escreva seus scripts em `Source/*.cpp`.
3. Botão **Compilar** da barra superior: roda o CMake numa thread de fundo (`PrismEditor/Core/NativeBuilder`); a saída vai para a aba **Saída de Build** (`Panels/BuildOutputPanel`), com erros/avisos clicáveis, e o Console recebe só o resumo. Se compilar, o módulo é recarregado sozinho. A preferência "compilar ao abrir o projeto" fica em `recent_projects.txt` (`@build=`).
4. Adicione o component **Native Script** a uma entidade e escolha a classe no combo.
5. **Play.**

Requisitos: `cmake` e um compilador C++20 no `PATH`, o mesmo da engine. O módulo é compilado na mesma configuração do editor (Debug/Release): o ABI da STL difere entre elas no MSVC.

## Escrevendo um script

```cpp
#include <Prism/Scripting/NativeScript.h>
using namespace Prism;

class Coletavel : public ScriptableEntity {
public:
    void OnCreate() override { LogInfo("pronto"); }

    void OnUpdate(float dt) override {
        GetTransform().Rotation.y += 90.0f * dt;
        if (IsKeyPressed(Key::Space))
            Instantiate("Assets/Prefabs/Bala.prismprefab", GetTransform().Translation);
    }

    void OnTriggerEnter(Entity other) override {
        if (other.GetComponent<TagComponent>().Tag == "Player")
            Destroy();   // removida no fim do frame
    }
};
PRISM_REGISTER_SCRIPT(Coletavel)
```

Ciclo de vida (todos opcionais): `OnCreate`, `OnUpdate(dt)`, `OnFixedUpdate(fixedDt)` (passo fixo de 1/60 s antes de cada passo da física; API v4), `OnDestroy`, `OnCollisionEnter/Exit(other)`, `OnTriggerEnter/Exit(other)`, `OnClick()` (botão de UI; API v3). Mesma semântica dos scripts Lua; a mesma entidade pode ter um script Lua **e** um C++.

Ajudantes de `ScriptableEntity`: `GetEntity`, `GetScene`, `GetComponent<T>`, `HasComponent<T>`, `GetTransform`, `LogInfo/Warn/Error`, `IsKeyDown/IsKeyPressed/IsMouseButtonDown`, `GetMouseDeltaX/Y`, `GetDeltaTime`, `GetElapsedTime`, `FindEntity`, `CreateEntity`, `Instantiate`, `Destroy`, `ApplyImpulse`, `SetVelocity`, `GetVelocity`, `Raycast`, `PlaySound`, `StopSound`, `PlayAnimation(nome, fade, loop)`, `StopAnimation`, `PauseAnimation`, `SetAnimationSpeed`, `IsAnimationPlaying` (API v5), `LoadScene(mapa)`, `ReloadScene()` (API v6), `SetAnimFloat/SetAnimBool/SetAnimTrigger/SetAnimState/GetAnimState` e o callback `OnAnimationEvent(const char*)` (API v7), `UISetText/UIGetText/UISetVisible/UISetInteractable/UISetColor/UISetTextColor` e o callback `OnUIClick(handler, id)` para UI `.prismui` (API v8), `CharacterMove(vec3)`, `CharacterJump(v)`, `CharacterIsGrounded()`, `CharacterGetVelocity()` e `RaycastMasked(origem, dir, dist, máscara)` (API v9), `PlayParticles`, `StopParticles`.

Adicionados na **API v4** (paridade com o Lua): `GetFixedDeltaTime`, `GetFrameCount`, `ApplyForce`, `GetWorldTransform`/`GetWorldPosition`/`GetWorldForward`/`GetWorldRight`, `SetParent` (e `GetParent`/`GetChildCount`/`GetChildAt` da própria `Entity`), `FindAllEntities`, `CreateMeshEntity`, `GetMouseX/Y`, `SetCursorMode`/`GetCursorMode`, `IsPointerOverUI`, e o áudio: `PlayAudio`, `PlayAudio3D`, `PreloadAudio`, `SetBusVolume`/`GetBusVolume`/`SetBusMuted`/`IsBusMuted` (canais `AudioBus::SFX/Music/UI/Ambient`), `SetMasterVolume`/`GetMasterVolume`.

> Módulos compilados com uma API mais antiga (a atual é a **v8**) são recusados ao carregar: faça **Compilar** de novo (o `Source/` do projeto não muda).

## Variáveis públicas (`OnProperties`)

Declare os membros que quer editar no painel Propriedades, como o `Properties` do Lua:

```cpp
class Inimigo : public ScriptableEntity {
public:
    float velocidade = 3.0f;
    int vida = 100;
    bool agressivo = true;
    std::string apelido = "Grunt";

    void OnProperties(NativeProperties& p) override {
        p.Float("velocidade", velocidade);
        p.Int("vida", vida);
        p.Bool("agressivo", agressivo);
        p.String("apelido", apelido);
    }
};
PRISM_REGISTER_SCRIPT(Inimigo)
```

- O valor inicial do membro é o **padrão** mostrado no editor; só o que você muda é salvo na cena (bloco `Native Script` v2) e vale antes do `OnCreate`. O botão **Reset** volta ao padrão.
- `OnProperties` roda também no editor, num objeto temporário **sem entidade**: não chame `GetEntity()`/`GetComponent` dentro dele.
- `Int` aparece como número (arrastável) e é arredondado.

## Troca de cena

`LoadScene("Fase2")` (mapa relativo à pasta de mapas, com ou sem `.prismmap`) e `ReloadScene()`. A troca acontece **depois do frame atual**: todos os scripts e entidades da cena são destruídos e a nova começa do zero (nada persiste entre cenas). Em Lua: `Scene.Load("Fase2")` / `Scene.Reload()`.

## Como funciona (e limites)

- O módulo **não linka com a engine** (que é uma biblioteca estática dentro do executável do editor). Tudo que precisa de código da engine passa pela tabela `NativeEngineAPI` (`Scripting/NativeScript.h`), entregue ao módulo ao carregar. Os ajudantes acima já a usam.
- Components (`GetComponent<T>`) são só cabeçalho e funcionam direto, desde que o módulo use o mesmo compilador, a mesma configuração e as mesmas versões de EnTT/GLM da engine. O projeto gerado pega os includes do `PrismSDK.cmake`, que o build da engine copia ao lado do executável do editor.
- Não chame funções da engine fora da API (ex.: `Scene::CreateEntity` direto): no Windows dá erro de link; nos outros sistemas, falha ao carregar. Se faltar algo, peça para entrar na `NativeEngineAPI` e suba `kNativeApiVersion`.
- Um módulo compilado para outra versão da API é recusado no carregamento, com mensagem no Console.
- Exceções C++ lançadas em callbacks são capturadas: o erro vai para o Console e o script daquela entidade para de ser chamado. Falhas graves (acesso inválido, crash) derrubam o editor, como qualquer código nativo.
- O módulo é copiado para `Cache/NativeModule/` antes de carregar, para o compilador poder sobrescrever `Binaries/NativeScripts.*` sem o sistema operacional travar o arquivo.

## Usando bibliotecas de terceiros

O módulo é um projeto CMake comum. O `Source/CMakeLists.txt` é **seu**: a engine só o cria quando falta e nunca o sobrescreve. O que é da engine (includes do SDK, C++20, pasta de saída `Binaries/`) fica em `Source/PrismGenerated.cmake`, que é regravado a cada abertura/compilação e não deve ser editado. O `CMakeLists.txt` chama `prism_configure_module(NativeScripts)`, definida lá.

Para usar uma lib, adicione ao seu `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(json)
target_link_libraries(NativeScripts PRIVATE nlohmann_json::nlohmann_json)
```

(`find_package` e vcpkg também funcionam.) Cuidados:

- Prefira libs **estáticas**: ficam dentro do `NativeScripts`. Uma lib dinâmica precisa ter a DLL/.so ao lado do executável.
- A lib deve ser compilada com o mesmo compilador e a mesma configuração (Debug/Release) da engine.
- Libs que guardam estado global (ex.: ImGui) terão **uma cópia dentro do módulo**, separada da cópia da engine. Para desenhar UI ou gráficos, o caminho previsto é a engine expor uma API de desenho pela `NativeEngineAPI`, em vez de a lib falar direto com o OpenGL.
- Projetos criados antes desta mudança têm o `CMakeLists.txt` migrado uma vez para o formato novo; o antigo fica em `CMakeLists.txt.bak`.

## Limitações

- **Hot reload no Play**: o botão **Compilar** funciona com o jogo rodando. Quando o `.dll/.so` muda (e estabiliza, para não pegar o arquivo pela metade), a engine troca o módulo sem parar o Play: os scripts C++ recebem `OnDestroy` (código antigo) e são recriados com o código novo (`OnProperties` + `OnCreate`). O **mundo** (entidades, física, posições) continua; o estado dos **membros** dos scripts recomeça. Se o módulo novo for inválido, o antigo segue rodando (erro no Console).
- A classe é escolhida pelo nome: renomear a classe deixa os components antigos apontando para um nome que não existe (aviso no painel e no Play).
