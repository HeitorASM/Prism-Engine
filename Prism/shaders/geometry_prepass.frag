#version 450 core

in vec3 v_ViewNormal;
layout(location = 0) out vec4 o_ViewNormal;

void main() {
    o_ViewNormal = vec4(normalize(v_ViewNormal), 1.0);
}
