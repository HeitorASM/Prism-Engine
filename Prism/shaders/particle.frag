#version 450 core

// Particulas: sem iluminacao. A cor do emissor e a cor "de tela" (mesma
// convencao do resto dos seletores de cor); a textura, quando houver, e
// amostrada sem conversao sRGB. Sem textura, desenha um circulo suave.

in vec2 v_UV;
in vec4 v_Color;

out vec4 o_Color;

uniform sampler2D u_Texture;
uniform int u_UseTexture;

void main() {
    vec4 color = v_Color;
    if (u_UseTexture != 0) {
        color *= texture(u_Texture, v_UV);
    } else {
        float distanceFromCenter = length(v_UV * 2.0 - 1.0);
        color.a *= 1.0 - smoothstep(0.55, 1.0, distanceFromCenter);
    }
    if (color.a <= 0.003)
        discard;
    o_Color = color;
}
