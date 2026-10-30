#version 450 core

// UI in-game: quads em espaco de tela (pixels, origem no canto superior
// esquerdo), gerados SEM vertex buffer - cada instancia le seu retangulo de um
// SSBO e escolhe o canto pelo gl_VertexID (mesmo esquema de particle.vert).

struct UIQuad {
    vec4 Rect;   // x, y, largura, altura (pixels)
    vec4 UV;     // u0, v0, u1, v1 (canto superior esquerdo -> inferior direito)
    vec4 Color;  // rgba
    vec4 Params; // x = modo (0 cor, 1 textura, 2 glifo de fonte)
};

layout(std430, binding = 0) readonly buffer UIBuffer {
    UIQuad u_Quads[];
};

uniform vec2 u_ScreenSize;

out vec2 v_UV;
out vec4 v_Color;
flat out int v_Mode;

const vec2 kCorners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    UIQuad q = u_Quads[gl_InstanceID];
    vec2 c = kCorners[gl_VertexID];
    vec2 pixel = q.Rect.xy + c * q.Rect.zw;
    gl_Position = vec4(pixel.x / u_ScreenSize.x * 2.0 - 1.0, 1.0 - pixel.y / u_ScreenSize.y * 2.0, 0.0, 1.0);
    v_UV = mix(q.UV.xy, q.UV.zw, c);
    v_Color = q.Color;
    v_Mode = int(q.Params.x + 0.5);
}
