# Scripting em C++

> **Recomendação:** no início do desenvolvimento a Prism só suportava Lua. Agora ela suporta C++, e o trabalho atual em scripting está levando o **C++ a se tornar a linguagem principal** da Prism, com o **Lua passando a ser uma opção**. Para um projeto novo que você pretende manter e crescer, **prefira C++**. O que ainda falta no C++ em relação ao Lua (marcado com "—" na [API de scripting](06-api-de-scripting.md)) está sendo trazido aos poucos. Lua continua ótimo para protótipos rápidos.

Os scripts C++ ficam ao lado dos scripts Lua. O código do projeto vira uma biblioteca (`NativeScripts.dll` no Windows, `.so`/`.dylib` nos demais) que o próprio editor compila e carrega. As duas linguagens podem coexistir na mesma entidade. **Todas as funções disponíveis** estão na [API de scripting](06-api-de-scripting.md); esta página mostra o fluxo de trabalho.

## Fluxo

1. A pasta `Source/` (com `CMakeLists.txt`, o arquivo de entrada do módulo e um exemplo) já é criada junto com o projeto. Se faltar, o botão **Compilar** a gera.
2. Escreva suas classes em `Source/*.cpp` (o **Editor de Código** abre esses arquivos).
3. Botão **Compilar** na barra superior. A compilação roda em segundo plano e, se der certo, o módulo é recarregado sozinho. A saída fica na aba **Saída de Build**, ao lado do Console; clicar num erro ou aviso abre o arquivo no Editor de Código. Por padrão o editor também compila ao abrir o projeto.
4. Adicione o component **Native Script** a uma entidade e escolha a classe.
5. **Play.**

Requisitos: `cmake` e um compilador C++20 no `PATH`, o mesmo usado para compilar a engine. O módulo é compilado na mesma configuração (Debug/Release) do editor.

## Escrevendo um script

```cpp
#include <Prism/Scripting/NativeScript.h>
using namespace Prism;

class Coletavel : public ScriptableEntity {
public:
    void OnCreate() override { LogInfo("pronto"); }

    void OnUpdate(float dt) override {
        GetTransform().Rotation.y += 90.0f * dt;
    }

    void OnTriggerEnter(Entity other) override {
        if (other.GetComponent<TagComponent>().Tag == "Jogador")
            Destroy();   // removida no fim do frame
    }
};
PRISM_REGISTER_SCRIPT(Coletavel)
```

Uma classe por script, derivada de `ScriptableEntity`, registrada com `PRISM_REGISTER_SCRIPT`. O nome da classe é o que aparece no combo do component **Native Script**. Uma instância é criada para cada entidade que usa a classe.

## Exemplo: andar e atirar

```cpp
class Jogador : public ScriptableEntity {
public:
    void OnUpdate(float dt) override {
        float v = 5.0f * dt;
        auto& t = GetTransform();
        if (IsKeyDown(Key::W)) t.Translation.z -= v;
        if (IsKeyDown(Key::S)) t.Translation.z += v;
        if (IsKeyDown(Key::A)) t.Translation.x -= v;
        if (IsKeyDown(Key::D)) t.Translation.x += v;

        if (IsKeyPressed(Key::Space))
            Instantiate("Assets/Prefabs/Bala.prismprefab", t.Translation);
    }
};
PRISM_REGISTER_SCRIPT(Jogador)
```

## Cuidados

- O módulo **não** linka com a engine. Tudo que precisa de código da engine passa pelos ajudantes da API. Chamar outras funções da engine direto pode falhar ao carregar ou compilar. Os **components** (`GetComponent<T>()`) são só cabeçalho e funcionam direto.
- Um módulo compilado para uma versão antiga da API é recusado, com mensagem no Console: recompile.
- Exceções C++ dentro dos callbacks são capturadas (o erro vai para o Console e o script para). Falhas graves, como acesso inválido à memória, derrubam o editor, como qualquer código nativo.
- A classe é escolhida **pelo nome**: renomeá-la deixa o component apontando para um nome que não existe (aparece um aviso).
- Não há recarga com o Play aberto: pare o Play, compile e rode de novo.
- Variáveis públicas: declare em `OnProperties(NativeProperties& p)` (`p.Float("nome", membro)`, `Int`, `Bool`, `String`) e edite no painel.
- Hot reload: **Compilar** funciona com o Play rodando; os scripts C++ recomeçam com o código novo e o mundo continua.
