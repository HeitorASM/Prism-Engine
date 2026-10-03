#version 450 core

out vec4 o_Color;

uniform vec3 u_BaseColor;

void main() {
    o_Color = vec4(u_BaseColor, 1.0);
}
