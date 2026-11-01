#version 450 core

// UI em ESPACO DE MUNDO: o mesmo quad do ui.vert (pixels do canvas, origem no canto superior
// esquerdo), mas posto no mundo por u_Model e visto pela camera (u_ViewProjection). O canvas e
// centrado na origem do modelo e mede u_CanvasSize / u_PixelsPerMeter metros; a face visivel e +Z.

struct UIQuad {
    vec4 Rect;   // x, y, largura, altura (pixels do canvas)
    vec4 UV;     // u0, v0, u1, v1
    vec4 Color;  // rgba
    vec4 Params; // x = modo (0 cor, 1 textura, 2 glifo de fonte)
};

layout(std430, binding = 0) readonly buffer UIBuffer {
    UIQuad u_Quads[];
};

uniform mat4 u_Model;
uniform mat4 u_ViewProjection;
uniform vec2 u_CanvasSize;
uniform float u_PixelsPerMeter;

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
    vec3 local = vec3((pixel.x - u_CanvasSize.x * 0.5) / u_PixelsPerMeter,
                      -(pixel.y - u_CanvasSize.y * 0.5) / u_PixelsPerMeter,
                      0.0);
    gl_Position = u_ViewProjection * u_Model * vec4(local, 1.0);
    v_UV = mix(q.UV.xy, q.UV.zw, c);
    v_Color = q.Color;
    v_Mode = int(q.Params.x + 0.5);
}
