#version 450 core

// Panorama equirretangular -> uma face do cubemap de ambiente (sem rotacao e
// sem intensidade: ambas sao aplicadas na hora de usar, ver basic.frag).
// Mesma convencao de sky.frag (u = atan(x, -z), v = acos(y)).

in vec3 v_Dir;
out vec4 o_Color;

uniform sampler2D u_Panorama;

const float kPi = 3.14159265358979;

void main() {
    vec3 d = normalize(v_Dir);
    vec2 uv = vec2(atan(d.x, -d.z) / (2.0 * kPi) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / kPi);

    // Costura de atan(): corrige as derivadas (ver sky.frag).
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    if (abs(dx.x) > 0.5) dx.x -= sign(dx.x);
    if (abs(dy.x) > 0.5) dy.x -= sign(dy.x);

    // Limite: um sol de milhares de unidades causaria "fireflies" no filtro; a luz
    // direta do sol ja vem das luzes da cena.
    vec3 color = min(textureGrad(u_Panorama, uv, dx, dy).rgb, vec3(256.0));
    o_Color = vec4(color, 1.0);
}
