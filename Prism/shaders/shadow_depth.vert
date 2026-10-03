#version 450 core

// Shader "depth-only" para o PRIMEIRO pass do shadow mapping (ver
// RenderShadowPass abaixo): desenha a cena inteira do ponto de vista
// da luz, sem cor nenhuma - so preenche o depth buffer do ShadowMap
// (ver ShadowMap.h/.cpp). O vertex shader e a unica parte que
// realmente importa (transforma a posicao pela matriz da LUZ, nao da
// camera); o fragment shader existe vazio so porque OpenGL exige um
// fragment stage linkado no programa - a GPU escreve gl_FragDepth
// (profundidade do triangulo rasterizado) automaticamente mesmo sem
// nenhuma instrucao explicita nele.

layout(location = 0) in vec3 a_Position;
// location = 1 (a_Normal) existe no Mesh (ver Mesh.h) mas nao e
// lido aqui - o layout do vertex buffer e o mesmo de basic.vert
// (Mesh e compartilhado entre os dois shaders), so os atributos
// usados diferem.

uniform mat4 u_LightSpaceMatrix; // Projection * View DA LUZ, nao da camera
uniform mat4 u_Model;

void main() {
    gl_Position = u_LightSpaceMatrix * u_Model * vec4(a_Position, 1.0);
}
