# Exportar o jogo

**Arquivo → Exportar Jogo...** empacota o projeto aberto como um jogo que roda **sem o editor**: uma pasta com o `PrismRuntime` (renomeado para o nome do jogo), as bibliotecas e recursos de que ele precisa e o projeto dentro de `Game/`.

## O que sai

```
MeuJogo/                      pasta de destino
  MeuJogo.exe                 o PrismRuntime, com o nome do projeto
  assimp-*.dll ...            o que mais o Runtime precisa
  shaders/  fonts/
  Game/                       o projeto
    MeuJogo.prismproj
    Assets/  Maps/  Scripts/
    Binaries/NativeScripts.dll    só se o projeto tem scripts C++
  .prism-export               manifesto (lista do que a exportação criou)
```

O `PrismRuntime` já procura o projeto em `Game/` ao lado do executável quando abre sem argumentos (`RuntimeLayer.cpp`), então **a pasta toda é o jogo**: basta copiar/zipar e distribuir.

**Não vão**: `Source/` (código dos scripts C++), `Cache/`, `.git`, `.gitignore`, backups (`.bak`, `.tmp`), fontes de arte que a engine não lê (`.blend`, `.psd`, `.kra`, `.xcf`) e arquivos de depuração (`.pdb`, `.ilk`...). Os `.meta` **vão**: são eles que mantêm a identidade dos assets.

## Como usar

1. Salve o mapa inicial (o projeto precisa ter um; senão a exportação recusa).
2. Se o projeto tem scripts C++, use **Compilar**. A exportação **avisa** se o módulo não existe ou está mais velho que o código.
3. **Arquivo → Exportar Jogo...** e escolha a pasta de destino. Se houver alterações não salvas, o editor pergunta antes (a exportação copia arquivos do disco).
4. Acompanhe na aba **Saída de Build**. A cópia roda em segundo plano, então o editor não trava.

A pasta escolhida deve estar **vazia** ou ser uma exportação anterior. Se ela tiver outras coisas (ex.: Documentos), o editor exporta numa subpasta com o nome do jogo em vez de misturar.

## Reexportar

Exportar de novo na mesma pasta é seguro: o `.prism-export` lista os arquivos que a exportação criou, e **só esses** são apagados antes de copiar de novo (um script que saiu do projeto some do jogo; um `LEIAME.txt` que você pôs lá fica). Se o manifesto for apagado, a pasta deixa de ser reconhecida e a exportação recusa em vez de arriscar apagar arquivos seus.

## Recusas

A exportação não começa (e não apaga nada) se: o projeto não tem mapa inicial ou o mapa não existe; o Runtime não foi compilado; o destino fica **dentro** do projeto ou do Runtime, ou **contém** o projeto/Runtime; o destino tem arquivos que não são de uma exportação anterior.

## Scripts C++ e a configuração do build

O módulo `NativeScripts` precisa ter sido compilado com a **mesma configuração** (Debug/Release) e o mesmo compilador do Runtime (o ABI da STL difere entre elas no MSVC). O botão **Compilar** do editor usa a configuração do próprio editor, e o CMake copia o Runtime para `<pasta do editor>/Runtime` na mesma configuração, então o par é compatível. **Para distribuir, use um build Release** do editor (no Debug o jogo abre com console e é bem mais lento).

No jogo exportado o hot reload é desligado (shaders, Lua e módulo C++) e o módulo é aberto direto de `Game/Binaries/`, sem a cópia em `Cache/` — a pasta de instalação pode ser somente leitura.

## Limitações

- Só a plataforma em que o editor foi compilado (o Runtime copiado é o do seu build).
- Sem ícone/metadados do executável, sem instalador e sem compactação (zipe a pasta).
- Os assets vão **como estão** (sem empacotar em um arquivo único, sem compressão de texturas).
- Não verifica quais assets o jogo realmente usa: tudo em `Assets/` vai junto.

## Código

- `Prism/Project/GameExporter.{h,cpp}`: a lógica (só `std::filesystem`, sem OpenGL/EnTT). Testada em `tests/ExportTests.cpp` (`PrismExportTests`).
- `PrismEditor/Core/GameExportRunner`: thread de fundo; `EditorLayer::StartGameExport`: menu, aviso de alterações não salvas e dialogo de pasta.
- `NativeScripting::SetDirectLoad` e o ramo `distributed` de `RuntimeLayer::OnAttach`: comportamento do jogo exportado.
