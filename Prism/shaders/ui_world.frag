#version 450 core

// UI em espaco de mundo: igual ao ui.frag (modo 0 cor, 1 textura, 2 glifo de fonte), mais a
// conversao "tela -> linear" quando a cena ainda vai passar pelo pos-processamento (HDR).

in vec2 v_UV;
in vec4 v_Color;
flat in int v_Mode;

out vec4 o_Color;

layout(binding = 0) uniform sampler2D u_Texture;
layout(binding = 1) uniform sampler2D u_Font;

uniform int u_OutputMode; // 1 = HDR linear

vec3 SrgbToLinear(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    vec3 low = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, vec3(cutoff));
}

void main() {
    vec4 color = v_Color;
    if (v_Mode == 1)
        color *= texture(u_Texture, v_UV);
    else if (v_Mode == 2)
        color.a *= texture(u_Font, v_UV).r;
    if (color.a <= 0.003)
        discard;
    if (u_OutputMode != 0)
        color.rgb = SrgbToLinear(color.rgb);
    o_Color = color;
}
