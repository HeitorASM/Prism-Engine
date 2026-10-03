#version 450 core

// Shader minimo: posicao + normal, iluminacao direcional simples "fake"
// (um unico dot product) so para as primitivas nao parecerem uma
// silhueta plana sem nenhuma pista de profundidade/forma.
//
// Tambem descarta (discard) o fragmento quando a normal do triangulo
// esta de costas para a camera (dot(normal, viewDir) < 0) - isto e, a
// FACE INTERNA de qualquer mesh fechado (Cube/Sphere/Capsule/Cylinder)
// fica transparente. Isso resolve o problema de uma camera (ou
// qualquer outra coisa) posicionada DENTRO de um mesh fechado (ex: uma
// CameraComponent dentro da capsula de colisao de um Character): em
// vez de enxergar a face interna solida do mesh (o que parece um erro
// visual, ja que a maioria das engines faz backface culling e nunca
// desenha essa face), a face de dentro simplesmente nao e desenhada e
// a camera ve direto para o resto da cena.
//
// Deliberadamente feito no FRAGMENT SHADER via dot product (em vez de
// glCullFace(GL_BACK) no lado C++) porque as primitivas hoje NAO tem
// winding order (CW/CCW) consistente entre si - Cube e CCW visto de
// fora, mas Sphere/Capsule/Cylinder (gerados por UV-mapping
// lat/lon, ver PrimitiveMeshFactory) saem com o winding oposto.
// Ligar culling por winding no pipeline faria essas primitivas
// sumirem inteiras (ou mostrarem so a face errada) em vez de so
// esconder a face interna. Testar dot(normal, viewDir) funciona
// igual independente do winding. O culling por indice e mais barato
// para a GPU, mas para o tamanho de cena atual a diferenca e
// desprezivel. Otimizacao possivel: normalizar o winding de cada
// PrimitiveMeshFactory::Create* e trocar para glCullFace.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in vec3 a_Tangent;

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;

// Matriz normal = transpose(inverse(mat3(u_Model))), calculada UMA
// vez por objeto na CPU (Renderer::ComputeNormalMatrix) em vez de
// por vertice aqui. Com escala NAO uniforme, mat3(u_Model) distorce
// a normal (ela deixa de ser perpendicular a superficie - erro de
// ~50 graus com escala 3x1x1, ~90 em objetos achatados), o que
// aparece no especular PBR como brilho torto. Com escala uniforme as
// duas matrizes dao a mesma DIRECAO, entao cenas existentes nao mudam.
uniform mat3 u_NormalMatrix;

out vec3 v_Normal;
out vec3 v_WorldPos;
out vec2 v_UV;
out vec3 v_Tangent;

void main() {
    v_Normal = u_NormalMatrix * a_Normal;
    // A TANGENTE continua com mat3(u_Model), de proposito: ela e um
    // vetor AO LONGO da superficie e transforma como qualquer outro
    // vetor do modelo. Usar a matriz normal nela a deixaria de novo
    // nao-perpendicular a normal (~87 graus de desvio) e quebraria a
    // base TBN do normal map.
    v_Tangent = mat3(u_Model) * a_Tangent;
    v_UV = a_UV;
    vec4 worldPos = u_Model * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    gl_Position = u_ViewProjection * worldPos;
}
