# Visão geral

Bem-vindo à **Prism Engine**: uma engine 3D escrita em C++20, com um editor integrado, e opensource feita como **projeto de hobby**. Ela existe como um projeto para aprender como uma engine funciona por dentro e de construir, aos poucos, as ferramentas para fazer jogos pequenos.

## Repositorio da engine

[Acessar o Repositório do Prism Engine](https://github.com/HeitorASM/Prism-Engine)

## O que esperar

- Não é um produto comercial nem um concorrente de Unity, Unreal ou Godot. É um projeto pessoal, em **fase inicial**, que muda com frequência.
- Muita coisa ainda está incompleta, e alguns recursos podem ter erros. A API de scripts pode mudar entre versões.
- Mesmo assim, já dá para montar cenas, programar comportamentos e rodar um jogo simples dentro do editor.

## O que já dá para fazer

- Montar **cenas 3D** com entidades, hierarquia, câmeras e luzes (Point, Spot e Directional), com iluminação PBR, sombras e SSAO.
- Importar **modelos 3D** (`.obj`, `.fbx`, `.gltf`, `.glb`) com materiais e texturas.
- Usar **física** (Jolt): corpos estáticos, cinemáticos e dinâmicos, triggers e raycast.
- Programar o jogo em **Lua** (rápido, sem compilar) ou em **C++** (módulo compilado pelo próprio editor).
- Tocar **áudio** 2D e 3D, criar **partículas** e montar **interface in-game** (painéis, textos, imagens e botões).
- Escolher o **fundo** da cena: cor, gradiente ou um panorama HDR.
- Salvar **mapas**, **prefabs** e **materiais**, e testar tudo no modo **Play**.

## O que ainda não existe

Ícone e instalador para o jogo exportado, *morph targets*, entre outros. A lista está em [Limitações e roadmap](12-limitacoes-e-roadmap.md).

## Como esta documentação está organizada

Estas páginas são um guia para **quem usa a engine**. Leia na ordem ou pule para o que precisa:

- [Primeiros passos](01-primeiros-passos.md): criar um projeto, uma cena e rodar o primeiro script.
- [O editor](02-editor.md): painéis, atalhos, mapas, prefabs e materiais.
- [Entidades e components](03-entidades-e-components.md): do que uma cena é feita.
- [Scripting em Lua](04-scripting-lua.md), [Scripting em C++](05-scripting-cpp.md) e a [API de scripting](06-api-de-scripting.md) (referência única para as duas linguagens).
- [Física](07-fisica.md), [Áudio](08-audio.md), [Visual e renderização](09-visual-e-renderizacao.md) e [UI in-game](10-ui-in-game.md).
- [Como a engine funciona](11-como-a-engine-funciona.md): o que acontece por baixo, em cada frame.

Existe também a **documentação técnica do GitHub** (pasta `docs/` do repositório), mais detalhada e voltada a quem mexe no código da engine. Para vê-la, use o botão pequeno **Ver docs do GitHub**, no fim da lista de documentos à esquerda.

## Dicas rápidas

- **F1** abre esta documentação a qualquer momento. Use a caixa **Buscar** para achar um assunto.
- O console (embaixo) mostra avisos e erros: olhe lá primeiro quando algo não funcionar.
- Salve com **Ctrl+S**. Antes de mexer muito, vale copiar a pasta do projeto: o projeto ainda é jovem.
