#version 450 core

// Passe final do pos-processamento: HDR linear -> imagem de tela.
//   1. soma o bloom (se ligado)
//   2. exposicao + tone mapping ACES (o MESMO de antes, ver basic.frag)
//   3. sRGB, saturacao e contraste
// Pixels de FUNDO (profundidade 1.0) com ceu LDR (u_SkyPassThrough = 1) pulam
// exposicao/ACES: a cor do ceu/da cor chapada sai exatamente como foi escolhida.

in vec2 v_TexCoord;
out vec4 o_Color;

uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform sampler2D u_Depth;

uniform int u_BloomEnabled;
uniform float u_BloomIntensity;
uniform float u_Exposure;
uniform float u_Saturation;
uniform float u_Contrast;
uniform int u_SkyPassThrough;

vec3 LinearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    bvec3 cutoff = lessThanEqual(c, vec3(0.0031308));
    vec3 low = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, vec3(cutoff));
}

vec3 ToneMapACES(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    ivec2 size = textureSize(u_Scene, 0);
    ivec2 p = clamp(ivec2(v_TexCoord * vec2(size)), ivec2(0), size - 1);

    vec3 hdr = texelFetch(u_Scene, p, 0).rgb;
    if (u_BloomEnabled != 0)
        hdr += texture(u_Bloom, v_TexCoord).rgb * u_BloomIntensity;

    float depth = texelFetch(u_Depth, p, 0).r;
    bool background = depth >= 1.0 && u_SkyPassThrough != 0;

    vec3 color = background ? hdr : ToneMapACES(hdr * u_Exposure);
    color = LinearToSrgb(color);

    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, u_Saturation);
    color = (color - 0.5) * u_Contrast + 0.5;

    o_Color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
