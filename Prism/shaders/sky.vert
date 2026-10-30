#version 450 core

// Skybox: um triangulo gigante (mesma tecnica de fullscreen_quad.vert) no
// plano distante. A direcao de visada de cada pixel sai de desprojetar o
// ponto do plano distante e subtrair a posicao da camera - sem cubo, sem
// VBO, sem VAO real.

out vec3 v_Direction;

uniform mat4 u_InverseViewProjection;
uniform vec3 u_CameraPosition;

void main() {
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec4 ndc = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = u_InverseViewProjection * ndc;
    v_Direction = world.xyz / world.w - u_CameraPosition;
    gl_Position = vec4(ndc.xy, 1.0, 1.0);
}
