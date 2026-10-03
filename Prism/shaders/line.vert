#version 450 core

// Shader de linha: sem normal, sem luz - so posicao transformada e uma
// cor solida via uniform. Deliberadamente separado do shader principal (basic.vert/basic.frag)
// (que exige atributo de normal no layout location 1) para o VAO de
// linhas poder ter um layout de vertice mais simples (so vec3
// posicao) sem enviar normais fake para a GPU.

layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection;

void main() {
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
