#version 450 core

// Vertex shader COMPARTILHADO pelos 2 fullscreen-quad passes abaixo
// (SSAO e blur) - gera um unico triangulo GIGANTE que cobre a tela
// inteira usando so gl_VertexID (sem VBO/atributos nenhum - ver
// comentario em Renderer::GetFullscreenQuadVAOForCurrentContext, Renderer.h). Tecnica
// padrao ("fullscreen triangle trick"): um triangulo com vertices em
// (-1,-1), (3,-1), (-1,3) cobre totalmente a regiao [-1,1]x[-1,1] (o
// NDC inteiro), com a parte que sobra do triangulo fora da tela
// simplesmente descartada pelo clipping do rasterizador - mais barato
// que desenhar 2 triangulos (4 vertices, 6 indices) formando um quad
// de verdade, pela mesma razao que menos chamadas/vertices e sempre
// melhor quando o resultado visual e identico.

out vec2 v_TexCoord;

void main() {
    v_TexCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(v_TexCoord * 2.0 - 1.0, 0.0, 1.0);
}
