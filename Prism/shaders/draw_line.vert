#version 450 core

// Desenho imediato (DrawContext): segmentos de reta com espessura em PIXELS, expandidos
// em quads voltados para a tela no proprio vertex shader (sem vertex buffer; cada instancia
// le seu segmento de um SSBO). Serve ao mundo (u_ViewProjection da camera) e a tela
// (u_ViewProjection ortografica em pixels). Funciona em qualquer contexto GL (VAO vazio).

struct Segment {
    vec4 A;      // xyz = ponto A, w = espessura (px)
    vec4 B;      // xyz = ponto B
    vec4 Color;  // rgba (cor "de tela")
};

layout(std430, binding = 0) readonly buffer SegmentBuffer {
    Segment u_Segments[];
};

uniform mat4 u_ViewProjection;
uniform vec2 u_ViewportSize; // pixels do viewport atual

out vec4 v_Color;

// (ao longo do segmento 0..1, lado -1..+1)
const vec2 kCorners[6] = vec2[6](
    vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(0.0, -1.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    Segment s = u_Segments[gl_InstanceID];
    vec4 ca = u_ViewProjection * vec4(s.A.xyz, 1.0);
    vec4 cb = u_ViewProjection * vec4(s.B.xyz, 1.0);

    // Recorta contra o plano proximo (w pequeno): um ponto atras da camera estragaria a divisao.
    const float kMinW = 0.001;
    if (ca.w < kMinW && cb.w < kMinW) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // tudo atras da camera: fora da tela
        v_Color = vec4(0.0);
        return;
    }
    if (ca.w < kMinW) ca = mix(ca, cb, (kMinW - ca.w) / (cb.w - ca.w));
    if (cb.w < kMinW) cb = mix(cb, ca, (kMinW - cb.w) / (ca.w - cb.w));

    vec2 pa = ca.xy / ca.w * 0.5 * u_ViewportSize;
    vec2 pb = cb.xy / cb.w * 0.5 * u_ViewportSize;
    vec2 d = pb - pa;
    float len = length(d);
    vec2 dir = len > 1e-4 ? d / len : vec2(1.0, 0.0);
    vec2 normal = vec2(-dir.y, dir.x);

    vec2 corner = kCorners[gl_VertexID];
    vec4 clip = corner.x < 0.5 ? ca : cb;
    // Deslocamento em pixels -> NDC (x2 / viewport), reescalado por w porque o clip ainda nao dividiu.
    vec2 offsetPx = normal * corner.y * max(s.A.w, 1.0) * 0.5;
    clip.xy += offsetPx * 2.0 / u_ViewportSize * clip.w;
    gl_Position = clip;
    v_Color = s.Color;
}
