#version 450 core

// Bloom, passe de SUBIDA: filtro "tenda" de 9 amostras sobre o nivel menor,
// SOMADO (blend aditivo, ligado pelo C++) ao nivel maior que ja esta no destino.

in vec2 v_TexCoord;
out vec4 o_Color;

uniform sampler2D u_Source;

void main() {
    vec2 t = 1.0 / vec2(textureSize(u_Source, 0));
    vec2 uv = v_TexCoord;

    vec3 a = texture(u_Source, uv + t * vec2(-1.0,  1.0)).rgb;
    vec3 b = texture(u_Source, uv + t * vec2( 0.0,  1.0)).rgb;
    vec3 c = texture(u_Source, uv + t * vec2( 1.0,  1.0)).rgb;
    vec3 d = texture(u_Source, uv + t * vec2(-1.0,  0.0)).rgb;
    vec3 e = texture(u_Source, uv).rgb;
    vec3 f = texture(u_Source, uv + t * vec2( 1.0,  0.0)).rgb;
    vec3 g = texture(u_Source, uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 h = texture(u_Source, uv + t * vec2( 0.0, -1.0)).rgb;
    vec3 i = texture(u_Source, uv + t * vec2( 1.0, -1.0)).rgb;

    vec3 result = (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) / 16.0;
    o_Color = vec4(result, 1.0);
}
