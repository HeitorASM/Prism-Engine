# Caches

Dados derivados que a engine pode refazer a qualquer hora ficam em `Cache/` na pasta do projeto (fora do Git, ver `.gitignore` gerado). **Apagar `Cache/` nunca perde nada**, só tempo na próxima abertura.

| Pasta | O que guarda | Chave | Limite |
|---|---|---|---|
| `Cache/LuaBytecode` | scripts Lua compilados (com informação de depuração: erros continuam com arquivo e linha) | texto do script + nome do chunk + versão do Lua | 64 MB |
| `Cache/Thumbnails` | miniaturas (até 128 px) das imagens do navegador de conteúdo | caminho + tamanho + data de modificação | 256 MB |
| `Cache/NativeModule`, `Cache/NativeBuild` | cópia do módulo C++ carregado e arquivos da compilação (ver [scripting-cpp.md](scripting-cpp.md)) | — | — |

## Como funciona (`Core/DiskCache.h`)

- Cada entrada é um arquivo `<chave>.pdc` com cabeçalho (`PDC1`, versão do formato, tamanho e hash do conteúdo). Na leitura tudo é conferido: arquivo truncado, adulterado ou de outra versão do formato é **apagado** e tratado como "não existe".
- A escrita é atômica (arquivo temporário + renomear): o editor e um jogo abertos ao mesmo tempo, ou uma queda de energia, nunca deixam uma entrada pela metade.
- Ao primeiro uso de cada pasta, os arquivos menos usados são apagados até caber no limite. Ler uma entrada antiga a renova (no máximo uma vez por dia).
- Falha ao gravar (disco cheio, pasta somente leitura) é ignorada: a engine segue sem cache.

## Bytecode do Lua

`ScriptEngine` procura o bytecode antes de compilar o texto. Mudou uma letra do script (ou o caminho dele), a chave muda e ele é recompilado. Bytecode que o Lua recusa (cache de outra build) é refeito pelo texto e sobrescrito. O ganho aparece em projetos com muitos scripts (abrir o projeto, entrar no Play, hot reload); em poucos scripts é pequeno.

## Miniaturas

Antes, o navegador de conteúdo carregava cada imagem **inteira** (decodificação, envio à GPU e mipmaps) na thread da interface. Agora `Renderer/ThumbnailCache` gera a miniatura uma vez (no máximo 3 por quadro, o resto aparece nos quadros seguintes), guarda em disco e mantém só a miniatura na GPU. Trocar a imagem gera outra miniatura sozinho. Imagens com mais de 160 milhões de pixels não geram miniatura.

## Ainda sem cache

Dados de importação de modelos (Assimp) e prévias de material/modelo continuam sendo refeitos a cada vez.

## Testes

`PrismDiskCacheTests` (cache e matemática de miniaturas, só `std`) e `PrismLuaBytecodeTests` (usa o Lua e o sol2 reais; o código testado é o trecho `LuaChunkCache` do próprio `ScriptEngine.cpp`).
