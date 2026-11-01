#version 450 core

// Desenho imediato (DrawContext): cor solida "de tela" (mesma convencao das particulas).

in vec4 v_Color;
out vec4 o_Color;

uniform int u_OutputMode; // 1 = HDR linear (pos-processamento): a cor "de tela" vira linear

vec3 SrgbToLinear(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    vec3 low = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, vec3(cutoff));
}

void main() {
    vec4 color = v_Color;
    if (color.a <= 0.003)
        discard;
    if (u_OutputMode != 0)
        color.rgb = SrgbToLinear(color.rgb);
    o_Color = color;
}
