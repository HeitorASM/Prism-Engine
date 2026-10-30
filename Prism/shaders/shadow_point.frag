#version 450 core

// Grava distancia(fragmento, luz) / alcance como profundidade: o basic.frag
// compara a distancia de cada pixel contra este valor (linear, sem a
// nao-linearidade da projecao perspectiva).

in vec3 v_WorldPos;

uniform vec3 u_LightPos;
uniform float u_Far;

void main() {
    gl_FragDepth = clamp(length(v_WorldPos - u_LightPos) / u_Far, 0.0, 1.0);
}
