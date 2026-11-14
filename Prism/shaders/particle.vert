#version 450 core

// Particulas: quads (billboards) voltados para a camera, gerados SEM vertex
// buffer. Cada instancia le seus dados (posicao+tamanho, cor) de um SSBO e
// escolhe o canto pelo gl_VertexID. Sem VAO de verdade (um VAO vazio basta),
// o que evita o problema de VAOs nao serem compartilhados entre contextos.

struct Particle {
    vec4 PositionSize; // xyz = posicao de mundo, w = tamanho (lado do quad)
    vec4 Color;        // rgba
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    Particle u_Particles[];
};

uniform mat4 u_ViewProjection;
uniform vec3 u_CameraRight;
uniform vec3 u_CameraUp;

out vec2 v_UV;
out vec4 v_Color;

const vec2 kCorners[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
);

void main() {
    Particle p = u_Particles[gl_InstanceID];
    vec2 corner = kCorners[gl_VertexID];
    vec3 world = p.PositionSize.xyz + (u_CameraRight * corner.x + u_CameraUp * corner.y) * (p.PositionSize.w * 0.5);
    v_UV = corner * 0.5 + 0.5;
    v_Color = p.Color;
    gl_Position = u_ViewProjection * vec4(world, 1.0);
}
