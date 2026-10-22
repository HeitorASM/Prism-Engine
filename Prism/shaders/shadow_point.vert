#version 450 core

// Sombra de luz PONTUAL: uma face do cubemap por vez. Entrega a posicao de
// mundo para o fragment shader gravar a DISTANCIA normalizada como profundidade.

layout(location = 0) in vec3 a_Position;

uniform mat4 u_LightSpaceMatrix; // Projection * View da face atual
uniform mat4 u_Model;

out vec3 v_WorldPos;

void main() {
    vec4 world = u_Model * vec4(a_Position, 1.0);
    v_WorldPos = world.xyz;
    gl_Position = u_LightSpaceMatrix * world;
}
