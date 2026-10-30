# Shaders de material (no projeto)

Um material pode ter um **shader próprio**, escrito em GLSL dentro da pasta do projeto (por exemplo `Shaders/Fogo.glsl`), no estilo dos *shaders de superfície* da Godot: você só escreve a parte que muda a aparência; a iluminação PBR, as sombras, o SSAO e o ambiente continuam sendo os da engine.

## Como usar

1. Selecione uma entidade com `Material` → seção **Shader do material**.
2. **Criar shader de exemplo** gera `Shaders/MeuShader.glsl` e o liga ao material. Ou digite o caminho de um `.glsl` seu (relativo à pasta do projeto).
3. Edite e salve o arquivo: em até ~0,5 s o material recarrega (mesmo hot reload dos shaders da engine). Se o arquivo não compilar, o material usa o shader padrão e o erro aparece no Console; corrigir e salvar recompila.

O caminho também é salvo no `.prismmat` e pode ser lido/escrito em Lua (`entity:GetMaterial().ShaderPath`).

## O que o arquivo define

Exatamente uma função:

```glsl
void prism_surface(inout PrismSurface s, in PrismSurfaceInput i) {
    s.emissive += vec3(1.0, 0.3, 0.1) * (0.5 + 0.5 * sin(i.time * 3.0));
}
```

`PrismSurface` (entra com o valor já resolvido — textura × fator — e **volta** para a iluminação; cores em **linear**):

| Campo | Significado |
|---|---|
| `albedo` | cor base (vec3) |
| `alpha` | opacidade final (limitada a 0..1; o recorte do modo *Mask* acontece **depois** do seu código) |
| `roughness`, `metallic` | 0..1 (limitados) |
| `emissive` | luz própria somada depois da iluminação (negativos viram 0) |
| `normal` | normal no espaço do mundo, já com o normal map; devolva vetor não nulo |

`PrismSurfaceInput` (somente leitura): `worldPos`, `viewDir` (do ponto para a câmera), `uv` (já com tiling/offset) e `time` (segundos).

O arquivo é incluído no `basic.frag` no lugar de `prism_surface.glsl` (que na engine é vazio). Você pode escrever funções auxiliares e `#include "outro.glsl"` (procurado ao lado do seu arquivo); os uniforms da engine (`u_Time`, `u_CameraWorldPos`, `u_AlbedoMap`...) também estão visíveis.

## Parâmetros no Inspetor

Cada `uniform` de nível de topo com tipo `float`, `int`, `bool`, `vec2`, `vec3` ou `vec4` vira um campo no material (valores guardados por nome na cena e no `.prismmat`):

```glsl
uniform float u_Velocidade = 3.0;                  // range(0, 10)   -> slider
uniform vec3  u_Cor = vec3(1.0, 0.3, 0.1);         // color          -> seletor de cor
uniform bool  u_Pulsar = true;                     //                -> checkbox
```

- O inicializador é o valor padrão (sem ele, zero). `range(a, b)` limita o campo e o valor; `color` (só `vec3`/`vec4`) usa o seletor, em sRGB como as outras cores, e o shader recebe **linear**.
- Salvar o `.glsl` relê as declarações; valores de parâmetros que não existem mais ficam guardados e são ignorados. Nomes repetidos, arrays, `sampler*` e outros tipos são ignorados pelo Inspetor (o GLSL continua valendo).
- Máximo de 64 parâmetros por material. Materiais com valores diferentes não entram no mesmo lote instanciado.

## Limitações atuais

- Só a **superfície** (fragmento). Não há deslocamento de vértices nem passe de pós-processamento por material.
- Parâmetros ainda não podem ser lidos/animados por Lua ou C++ (só pelo Inspetor).
- Sem parâmetros de textura (`sampler2D`).
- **Sombras e SSAO ignoram o seu código**: o objeto projeta sombra e entra no SSAO com a forma/alfa do material comum.
- Materiais com shader próprio não entram no mesmo lote que os padrão (o shader faz parte da chave do material), então muitos shaders diferentes custam mais trocas de programa.
- Jogo exportado: o hot reload é desligado; o `.glsl` precisa estar na pasta do projeto exportada (não verificado).
