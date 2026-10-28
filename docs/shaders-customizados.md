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

## Limitações atuais

- Só a **superfície** (fragmento). Não há deslocamento de vértices nem passe de pós-processamento por material.
- **Sem parâmetros expostos** no Inspetor (os `uniform` do estilo Godot): por enquanto use `i.time`, `i.uv` e os campos do material (`Opacity`, `EmissiveColor`... chegam em `s`).
- **Sombras e SSAO ignoram o seu código**: o objeto projeta sombra e entra no SSAO com a forma/alfa do material comum.
- Materiais com shader próprio não entram no mesmo lote que os padrão (o shader faz parte da chave do material), então muitos shaders diferentes custam mais trocas de programa.
- Jogo exportado: o hot reload é desligado; o `.glsl` precisa estar na pasta do projeto exportada (não verificado).
