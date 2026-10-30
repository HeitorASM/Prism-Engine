#version 450 core

// Bloom, passe de DESCIDA: filtro de 13 amostras (Jimenez, "Next Generation
// Post Processing in Call of Duty"). No primeiro nivel (u_Prefilter = 1) cada
// amostra passa antes pelo limiar (joelho suave), de modo que so o que
// ultrapassa u_Threshold entra no bloom.

in vec2 v_TexCoord;
out vec4 o_Color;

uniform sampler2D u_Source;
uniform int u_Prefilter;
uniform float u_Threshold;

vec3 Fetch(vec2 uv) {
    vec3 c = min(texture(u_Source, uv).rgb, vec3(64.0)); // limita "fireflies"
    if (u_Prefilter != 0) {
        float brightness = max(c.r, max(c.g, c.b));
        float knee = max(u_Threshold * 0.5, 1e-4);
        float soft = clamp(brightness - u_Threshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 1e-5);
        float contribution = max(soft, brightness - u_Threshold) / max(brightness, 1e-4);
        c *= max(contribution, 0.0);
    }
    return c;
}

void main() {
    vec2 t = 1.0 / vec2(textureSize(u_Source, 0));
    vec2 uv = v_TexCoord;

    vec3 a = Fetch(uv + t * vec2(-2.0,  2.0));
    vec3 b = Fetch(uv + t * vec2( 0.0,  2.0));
    vec3 c = Fetch(uv + t * vec2( 2.0,  2.0));
    vec3 d = Fetch(uv + t * vec2(-2.0,  0.0));
    vec3 e = Fetch(uv);
    vec3 f = Fetch(uv + t * vec2( 2.0,  0.0));
    vec3 g = Fetch(uv + t * vec2(-2.0, -2.0));
    vec3 h = Fetch(uv + t * vec2( 0.0, -2.0));
    vec3 i = Fetch(uv + t * vec2( 2.0, -2.0));
    vec3 j = Fetch(uv + t * vec2(-1.0,  1.0));
    vec3 k = Fetch(uv + t * vec2( 1.0,  1.0));
    vec3 l = Fetch(uv + t * vec2(-1.0, -1.0));
    vec3 m = Fetch(uv + t * vec2( 1.0, -1.0));

    vec3 result = e * 0.125
                + (a + c + g + i) * 0.03125
                + (b + d + f + h) * 0.0625
                + (j + k + l + m) * 0.125;
    o_Color = vec4(result, 1.0);
}
