#version 450 core

// UI in-game: cor "de tela" (mesma convencao das particulas - sem conversao
// sRGB). Modo 1 amostra a textura da imagem; modo 2 usa o canal R do atlas de
// fonte (R8) como alpha.

in vec2 v_UV;
in vec4 v_Color;
flat in int v_Mode;

out vec4 o_Color;

layout(binding = 0) uniform sampler2D u_Texture;
layout(binding = 1) uniform sampler2D u_Font;

void main() {
    vec4 color = v_Color;
    if (v_Mode == 1)
        color *= texture(u_Texture, v_UV);
    else if (v_Mode == 2)
        color.a *= texture(u_Font, v_UV).r;
    if (color.a <= 0.003)
        discard;
    o_Color = color;
}
