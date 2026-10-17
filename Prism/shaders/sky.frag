#version 450 core

// Skybox. Modos (u_Mode):
//   0 = cor chapada (u_SolidColor, sRGB como o picker, escrita direto)
//   1 = gradiente ceu/horizonte/chao - o MESMO do ambiente (ver
//       EnvironmentColor em basic.frag), visivel ao fundo
//   2 = panorama equirretangular (u_Panorama, linear). HDR (u_ToneMap = 1)
//       passa por exposicao + ACES; LDR so por intensidade, sem tone mapping,
//       para a imagem aparecer como foi pintada.

in vec3 v_Direction;

out vec4 o_Color;

uniform int u_Mode;
uniform vec3 u_SolidColor;
uniform vec3 u_EnvZenith;   // sRGB
uniform vec3 u_EnvHorizon;
uniform vec3 u_EnvGround;
uniform sampler2D u_Panorama;
uniform float u_Rotation;   // radianos, em torno de Y
uniform float u_Intensity;
uniform float u_Exposure;
uniform int u_ToneMap;

const float kPi = 3.14159265358979;

vec3 SrgbToLinear(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    vec3 low = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, vec3(cutoff));
}

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

vec3 GradientColor(float y) {
    vec3 zenith = SrgbToLinear(u_EnvZenith);
    vec3 horizon = SrgbToLinear(u_EnvHorizon);
    vec3 ground = SrgbToLinear(u_EnvGround);
    float up = clamp(y, 0.0, 1.0);
    float dn = clamp(-y, 0.0, 1.0);
    float su = up * up * (3.0 - 2.0 * up);
    float sd = dn * dn * (3.0 - 2.0 * dn);
    return (y >= 0.0) ? mix(horizon, zenith, su) : mix(horizon, ground, sd);
}

void main() {
    vec3 dir = normalize(v_Direction);

    if (u_Mode == 0) {
        o_Color = vec4(u_SolidColor, 1.0);
        return;
    }
    if (u_Mode == 1) {
        o_Color = vec4(LinearToSrgb(GradientColor(dir.y)), 1.0);
        return;
    }

    // Panorama: gira em torno de Y e converte a direcao em (u, v) equirretangular.
    // A imagem e carregada SEM inverter, entao v = 0 e o topo (cima).
    float c = cos(u_Rotation);
    float s = sin(u_Rotation);
    vec3 d = vec3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z);
    vec2 uv = vec2(atan(d.x, -d.z) / (2.0 * kPi) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / kPi);

    // atan() salta de 1 para 0 na costura: as derivadas de uv explodem ali e o
    // mipmap escolheria o nivel mais borrado (uma linha visivel). Corrige o salto.
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    if (abs(dx.x) > 0.5) dx.x -= sign(dx.x);
    if (abs(dy.x) > 0.5) dy.x -= sign(dy.x);

    vec3 color = textureGrad(u_Panorama, uv, dx, dy).rgb * u_Intensity;
    if (u_ToneMap != 0)
        color = ToneMapACES(color * u_Exposure);
    o_Color = vec4(LinearToSrgb(color), 1.0);
}
