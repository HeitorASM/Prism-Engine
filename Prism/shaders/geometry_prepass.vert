#version 450 core

// Pre-pass de geometria (RenderGeometryPrePass): mesma ideia do
// shadow-depth acima, mas com a camera PRINCIPAL (nao a da luz) e uma
// SEGUNDA saida (normal em view-space, alem da profundidade que a GPU
// ja escreve sozinha no depth buffer).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;

#include "skinning.glsl"

uniform mat4 u_ViewProjection;
uniform mat4 u_View;
uniform mat4 u_Model;

// Matriz normal em VIEW-SPACE: transpose(inverse(mat3(u_View *
// u_Model))), calculada na CPU (Renderer::ComputeNormalMatrix) uma
// vez por objeto. A versao antiga usava mat3(u_View * u_Model)
// direto, sob a justificativa de que a engine nao aplicava escala
// nao-uniforme - mas o editor permite (gizmo de escalar e campo
// "Escala" com eixos independentes), e nesse caso a normal sai
// distorcida e o SSAO escurece os lugares errados. Ver tambem
// basic.vert (shader principal).
uniform mat3 u_ViewNormalMatrix;

// Instancing (mesmo layout de instancia de basic.vert: modelo nos locais 8-11, matriz
// normal de MUNDO nos locais 12-14). A matriz normal em view-space de uma instancia e
// inversa-transposta(view * model) = inversa-transposta(view) * inversa-transposta(model),
// entao a CPU manda u_ViewNormalBase = inversa-transposta(mat3(view)) uma vez por quadro.
uniform int u_Instanced;
uniform mat3 u_ViewNormalBase;
layout(location = 8) in mat4 a_InstModel;
layout(location = 12) in mat3 a_InstNormal;

out vec3 v_ViewNormal;

void main() {
    mat4 model = (u_Instanced != 0) ? a_InstModel : u_Model;
    mat3 viewNormal = (u_Instanced != 0) ? (u_ViewNormalBase * a_InstNormal) : u_ViewNormalMatrix;
    mat4 skin = SkinMatrix();
    v_ViewNormal = normalize(viewNormal * (mat3(skin) * a_Normal));
    gl_Position = u_ViewProjection * model * (skin * vec4(a_Position, 1.0));
}
