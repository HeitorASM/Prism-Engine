#version 450 core

// Sombra de luz PONTUAL: uma face do cubemap por vez. Entrega a posicao de
// mundo para o fragment shader gravar a DISTANCIA normalizada como profundidade.

layout(location = 0) in vec3 a_Position;

#include "skinning.glsl"

uniform mat4 u_LightSpaceMatrix; // Projection * View da face atual
uniform mat4 u_Model;

// Instancing (mesmo local de basic.vert): ver shadow_depth.vert.
uniform int u_Instanced;
layout(location = 8) in mat4 a_InstModel;

out vec3 v_WorldPos;

void main() {
    mat4 model = (u_Instanced != 0) ? a_InstModel : u_Model;
    vec4 world = model * (SkinMatrix() * vec4(a_Position, 1.0));
    v_WorldPos = world.xyz;
    gl_Position = u_LightSpaceMatrix * world;
}
