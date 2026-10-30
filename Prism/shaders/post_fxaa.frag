#version 450 core

// FXAA (versao compacta, baseada no FXAA 3 de Timothy Lottes): suaviza bordas
// serrilhadas olhando a luma dos vizinhos. Roda sobre a imagem JA em sRGB.

in vec2 v_TexCoord;
out vec4 o_Color;

uniform sampler2D u_Source;

void main() {
    const float kReduceMin = 1.0 / 128.0;
    const float kReduceMul = 1.0 / 8.0;
    const float kSpanMax = 8.0;
    const vec3 kLuma = vec3(0.299, 0.587, 0.114);

    vec2 inv = 1.0 / vec2(textureSize(u_Source, 0));
    vec2 uv = v_TexCoord;

    vec3 rgbNW = texture(u_Source, uv + vec2(-1.0, -1.0) * inv).rgb;
    vec3 rgbNE = texture(u_Source, uv + vec2( 1.0, -1.0) * inv).rgb;
    vec3 rgbSW = texture(u_Source, uv + vec2(-1.0,  1.0) * inv).rgb;
    vec3 rgbSE = texture(u_Source, uv + vec2( 1.0,  1.0) * inv).rgb;
    vec3 rgbM  = texture(u_Source, uv).rgb;

    float lumaNW = dot(rgbNW, kLuma);
    float lumaNE = dot(rgbNE, kLuma);
    float lumaSW = dot(rgbSW, kLuma);
    float lumaSE = dot(rgbSE, kLuma);
    float lumaM  = dot(rgbM, kLuma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * kReduceMul), kReduceMin);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-kSpanMax), vec2(kSpanMax)) * inv;

    vec3 rgbA = 0.5 * (texture(u_Source, uv + dir * (1.0 / 3.0 - 0.5)).rgb
                     + texture(u_Source, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(u_Source, uv + dir * -0.5).rgb
                                   + texture(u_Source, uv + dir *  0.5).rgb);

    float lumaB = dot(rgbB, kLuma);
    o_Color = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
