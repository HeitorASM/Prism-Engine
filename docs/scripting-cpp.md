# Scripting C++

> No início do desenvolvimento a Prism só suportava Lua; agora suporta C++, e o scripting está sendo trabalhado para que o C++ se torne a linguagem principal, com o Lua como uma opção.

Scripts de gameplay em C++ ao lado dos scripts Lua. O código do projeto vira uma biblioteca compartilhada (`NativeScripts.dll` / `.so` / `.dylib`) que o editor compila e carrega em runtime.

## Fluxo

1. Menu **Scripts C++ → Gerar projeto de scripts**. Cria `<Projeto>/Source/` com `CMakeLists.txt`, `PrismModuleEntry.cpp` e um exemplo (`Spinner.cpp`). Nada que já exista é sobrescrito (exceto o `CMakeLists.txt`, que guarda o caminho do SDK da engine).
2. Escreva seus scripts em `Source/*.cpp`.
3. **Scripts C++ → Compilar e recarregar**. Roda o CMake numa thread de fundo; a saída aparece no Console e, se compilar, o módulo é recarregado sozinho.
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

Ciclo de vida (todos opcionais): `OnCreate`, `OnUpdate(dt)`, `OnDestroy`, `OnCollisionEnter/Exit(other)`, `OnTriggerEnter/Exit(other)`, `OnClick()` (botão de UI; API v3). Mesma semântica dos scripts Lua; a mesma entidade pode ter um script Lua **e** um C++.

Ajudantes de `ScriptableEntity`: `GetEntity`, `GetScene`, `GetComponent<T>`, `HasComponent<T>`, `GetTransform`, `LogInfo/Warn/Error`, `IsKeyDown/IsKeyPressed/IsMouseButtonDown`, `GetMouseDeltaX/Y`, `GetDeltaTime`, `GetElapsedTime`, `FindEntity`, `CreateEntity`, `Instantiate`, `Destroy`, `ApplyImpulse`, `SetVelocity`, `GetVelocity`, `Raycast`, `PlaySound`, `StopSound`, `PlayParticles`, `StopParticles`.

## Como funciona (e limites)

- O módulo **não linka com a engine** (que é uma biblioteca estática dentro do executável do editor). Tudo que precisa de código da engine passa pela tabela `NativeEngineAPI` (`Scripting/NativeScript.h`), entregue ao módulo ao carregar. Os ajudantes acima já a usam.
- Components (`GetComponent<T>`) são só cabeçalho e funcionam direto, desde que o módulo use o mesmo compilador, a mesma configuração e as mesmas versões de EnTT/GLM da engine. O projeto gerado pega os includes do `PrismSDK.cmake`, que o build da engine copia ao lado do executável do editor.
- Não chame funções da engine fora da API (ex.: `Scene::CreateEntity` direto): no Windows dá erro de link; nos outros sistemas, falha ao carregar. Se faltar algo, peça para entrar na `NativeEngineAPI` e suba `kNativeApiVersion`.
- Um módulo compilado para outra versão da API é recusado no carregamento, com mensagem no Console.
- Exceções C++ lançadas em callbacks são capturadas: o erro vai para o Console e o script daquela entidade para de ser chamado. Falhas graves (acesso inválido, crash) derrubam o editor, como qualquer código nativo.
- O módulo é copiado para `Cache/NativeModule/` antes de carregar, para o compilador poder sobrescrever `Binaries/NativeScripts.*` sem o sistema operacional travar o arquivo.

## Limitações

- Sem recarga com o Play aberto: pare o Play, compile (ou aguarde o recarregamento automático).
- Propriedades públicas editáveis no painel (como `Properties` do Lua) ainda não existem para scripts C++.
- A classe é escolhida pelo nome: renomear a classe deixa os components antigos apontando para um nome que não existe (aviso no painel e no Play).
