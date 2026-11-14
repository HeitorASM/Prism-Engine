#version 450 core

// Particulas: quads (billboards) voltados para a camera, gerados SEM vertex
// buffer. Cada instancia le seus dados (posicao+tamanho, cor, rotacao+quadro)
// de um SSBO e escolhe o canto pelo gl_VertexID. Sem VAO de verdade (um VAO
// vazio basta), o que evita o problema de VAOs nao serem compartilhados
// entre contextos.
//
// Rotacao: gira o quad em torno do eixo de visao (as UVs nao giram, entao a
// textura gira junto). Folha de sprites (flipbook): u_FlipbookGrid = (colunas,
// linhas); o quadro 0 e o canto SUPERIOR ESQUERDO da imagem, em ordem de leitura
// (as texturas do Prism sao carregadas com V invertido: v = 1 e o topo).
// Com grade 1x1, v_AtlasUV == v_UV (textura simples, como antes).

struct Particle {
    vec4 PositionSize; // xyz = posicao de mundo, w = tamanho (lado do quad)
    vec4 Color;        // rgba
    vec4 Extra;        // x = rotacao (radianos), y = indice do quadro do flipbook
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    Particle u_Particles[];
};

uniform mat4 u_ViewProjection;
uniform vec3 u_CameraRight;
uniform vec3 u_CameraUp;
uniform vec2 u_FlipbookGrid; // colunas, linhas (>= 1)

out vec2 v_UV;      // 0..1 no quad inteiro (circulo suave, sem textura)
out vec2 v_AtlasUV; // UV dentro do quadro atual da textura
out vec4 v_Color;

const vec2 kCorners[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
);

void main() {
    Particle p = u_Particles[gl_InstanceID];
    vec2 corner = kCorners[gl_VertexID];

    float s = sin(p.Extra.x);
    float c = cos(p.Extra.x);
    vec2 rotated = vec2(c * corner.x - s * corner.y, s * corner.x + c * corner.y);

    vec3 world = p.PositionSize.xyz + (u_CameraRight * rotated.x + u_CameraUp * rotated.y) * (p.PositionSize.w * 0.5);

    vec2 uv = corner * 0.5 + 0.5;
    vec2 grid = max(u_FlipbookGrid, vec2(1.0));
    float frame = floor(p.Extra.y + 0.5);
    float column = mod(frame, grid.x);
    float row = floor(frame / grid.x);
    v_UV = uv;
    v_AtlasUV = (vec2(column, grid.y - 1.0 - row) + uv) / grid;
    v_Color = p.Color;
    gl_Position = u_ViewProjection * vec4(world, 1.0);
}
