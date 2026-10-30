#version 450 core

// Triangulo de tela cheia que renderiza UMA face de um cubemap (u_Face, ordem
// +X -X +Y -Y +Z -Z). v_Dir e a direcao do mundo que o texel representa
// (convencao de cubemap do OpenGL; a linha 0 da imagem e t = 0).

out vec3 v_Dir;

uniform int u_Face;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); // (0,0) (2,0) (0,2)
    vec2 c = p * 2.0 - 1.0;
    gl_Position = vec4(c, 0.0, 1.0);

    if (u_Face == 0)      v_Dir = vec3( 1.0, -c.y, -c.x);
    else if (u_Face == 1) v_Dir = vec3(-1.0, -c.y,  c.x);
    else if (u_Face == 2) v_Dir = vec3( c.x,  1.0,  c.y);
    else if (u_Face == 3) v_Dir = vec3( c.x, -1.0, -c.y);
    else if (u_Face == 4) v_Dir = vec3( c.x, -c.y,  1.0);
    else                  v_Dir = vec3(-c.x, -c.y, -1.0);
}
