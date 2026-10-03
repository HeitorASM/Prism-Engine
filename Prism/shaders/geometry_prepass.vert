#version 450 core

// Pre-pass de geometria (RenderGeometryPrePass): mesma ideia do
// shadow-depth acima, mas com a camera PRINCIPAL (nao a da luz) e uma
// SEGUNDA saida (normal em view-space, alem da profundidade que a GPU
// ja escreve sozinha no depth buffer).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;

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

out vec3 v_ViewNormal;

void main() {
    v_ViewNormal = normalize(u_ViewNormalMatrix * a_Normal);
    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
}
