# Extensoes (components e sistemas)

Esta pagina descreve a API para estender a engine sem editar o codigo dela: **components
novos** (salvos nos mapas, com inspetor e Undo no editor) e **sistemas** (logica que roda
junto com a cena). Uma extensao e uma biblioteca (`.dll`/`.so`) com um manifesto, carregada
pela engine ao abrir o editor ou o jogo; o exemplo completo esta em
`examples/Extensions/Health/`.

## Empacotando uma extensao

```
<Projeto>/Extensions/Health/        (ou <pasta do executavel>/Extensions/Health/ - global)
    extension.ini
    Health.dll  (Windows)  |  libHealth.so  (Linux)   - ou dentro de Binaries/
```

`extension.ini` (uma chave por linha, `#` comenta):

```
name        = Health        # obrigatorio (letras, numeros, _ . -)
api         = 1             # obrigatorio: versao do ExtensionAPI com que foi compilada
version     = 1.0.0
description = Vida e dano
author      = Fulano
library     = Health        # binario sem extensao; padrao = name
target      = both          # both | editor | runtime
depends     = OutraExt, MaisUma    # carregam ANTES desta
enabled     = true
```

O que acontece ao abrir o editor/jogo, antes de qualquer mapa: a engine le os manifestos,
ordena por `depends` e carrega cada extensao (`PrismExtension_Init`). Uma extensao com problema
(manifesto invalido, `api` diferente, dependencia faltando, dll de outro compilador, `Init`
devolvendo `false` ou lancando excecao) fica como **Failed** com o motivo no log e nao impede as
demais. `target` e `enabled = false` ficam como **Skipped**. Estados e motivos:
`ExtensionLoader::GetExtensions()`.

### Compilando

Mesmo compilador e configuracao da engine (como os scripts C++ do projeto). O editor gera o
`PrismSDK.cmake` ao lado do executavel; veja o `CMakeLists.txt` do exemplo:

```
cmake -S . -B build -DPRISM_SDK_FILE=<pasta do editor>/PrismSDK.cmake
cmake --build build --config Release
```

### Regras da dll

- Use **so** `RegisterComponent<T>(host, desc)`, `RegisterSystem<S>(host)` e `LogMessage(host, nivel, texto)`:
  a dll nao enxerga os simbolos da engine, tudo passa pela tabela `ExtensionHost`.
- Dentro da dll use `WriteRaw/ReadRaw` do registro (templates) e os `Prism::WriteString/ReadString`
  de `ExtensionAPI.h` (os do `ComponentRegistry` nao sao visiveis a dll).
- Inspetor: use `host.Ui` (`DragFloat`, `DragInt`, `Checkbox`, `InputText`, `Text`) copiado por
  valor para o lambda; nao precisa linkar o ImGui.
- Mesmo runtime (CRT) da engine: `RegisterSystem` passa a posse de um `new` da dll para a engine.
- A engine **nunca descarrega** a dll antes de sair (objetos dela vivem nas cenas). Atualizar uma
  extensao exige reiniciar o editor.
- Extensoes sao codigo nativo com acesso total: so abra projetos de quem voce confia.
- Trocar de projeto na mesma sessao do editor nao carrega as extensoes do novo projeto; reinicie.

## Components de extensao

Abaixo, o codigo de dentro da dll (no `Init` da extensao; `host` e o `ExtensionHost` recebido).

```cpp
struct Health { float Hp = 100.0f; int Armor = 0; };

Prism::ExtensionComponentDesc d;
d.Name     = "Health";            // CHAVE gravada nos arquivos: nao renomeie depois de publicar
d.Category = "Gameplay";          // grupo no menu Add Component
d.Version  = 1;
d.Serialize = [](std::ostream& o, Prism::Entity e) {
    auto& h = e.GetComponent<Health>();
    Prism::ComponentRegistry::WriteRaw(o, h.Hp);
    Prism::ComponentRegistry::WriteRaw(o, h.Armor);
};
d.Deserialize = [](std::istream& i, Prism::Entity e, uint32_t version) {
    Health h;
    if (!Prism::ComponentRegistry::ReadRaw(i, h.Hp)) return false;
    Prism::ComponentRegistry::ReadRaw(i, h.Armor);
    e.AddComponent<Health>(h);
    return true;
};
auto ui = host.Ui;
d.DrawInspector = [ui](Prism::Entity e) { ui.DragFloat("Hp", &e.GetComponent<Health>().Hp, 1.0f, 0.0f, 1000.0f); };
Prism::RegisterComponent<Health>(host, d);   // Source = nome da extensao, automatico
// (codigo da propria engine / exe customizado pode usar ComponentRegistry::RegisterExtension<T>(d)
//  direto, com d.Source preenchido.)
```

Regras:

- **Registre antes de abrir mapas.** Blocos de um component ainda nao registrado sao
  preservados como "desconhecidos" (nao se perdem ao salvar) e so viram o component de
  verdade se estiverem registrados na hora da leitura.
- O **nome** e a chave no disco. Nome repetido, nome de component da engine ou tipo C++ ja
  registrado sao recusados (`RegisterExtension` devolve `false` e loga o motivo).
- Para **acrescentar campos**, escreva-os no FIM e suba `Version`; leia-os so se
  `version >= N`.
- No editor, add/remove/editar tem Undo/Redo automatico (comparando os bytes serializados);
  um gesto (arrastar um slider) vira um unico passo.

## Sistemas

```cpp
class MeuSistema : public Prism::IEngineSystem {
public:
    const char* GetName() const override { return "MeuSistema"; }
    int  GetOrder() const override { return 0; }   // menor roda primeiro; empate = ordem de registro
    void OnUpdate(Prism::Scene& s, float dt) override { /* ... */ }
};
Prism::RegisterSystem<MeuSistema>(host);   // dentro da dll
// (engine/exe customizado: Prism::SystemRegistry::Add<MeuSistema>("MinhaExtensao");)
```

- Chamados no Play: `OnSceneStart` (ao iniciar), `OnUpdate`, `OnFixedUpdate`, `OnSceneStop`.
- Uma excecao em um callback marca **so aquele sistema** como com defeito (ele para de rodar);
  o resto continua.
- `Unregister(nome)`, `UnregisterSource(extensao)` e `Shutdown()` chamam `OnShutdown`.

## Limitacoes atuais

- Duplicar entidade e sincronizar prefab nao levam blocos desconhecidos (so salvar e o
  `Scene::Clone` do Play levam).
- Nao ha descarregar extensoes em tempo de execucao; atualizar uma exige reiniciar.
- Uma extensao cujo `Init` falha depois de registrar components deixa esses components no registro ate fechar o programa (os sistemas dela sao removidos).
- Um sistema registrado durante o Play nao recebe `OnSceneStart` daquele Play.
