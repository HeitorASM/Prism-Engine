// ============================================================================
// skinning.glsl - skinning por osso (animacao esqueletica), incluido pelos
// vertex shaders de basic, shadow_depth, shadow_point e geometry_prepass.
//
// Atributos 4/5 do MeshVertex: ate 4 ossos por vertice. Pesos todos 0 =
// vertice sem skinning (primitivas, malhas estaticas) -> matriz identidade.
// u_Skinned = 0 desliga tudo (malhas sem ossos nao precisam da paleta).
// ============================================================================
#ifndef PRISM_SKINNING_GLSL
#define PRISM_SKINNING_GLSL

#define PRISM_MAX_BONES 128

layout(location = 4) in ivec4 a_BoneIds;
layout(location = 5) in vec4 a_BoneWeights;

uniform int u_Skinned;
uniform mat4 u_Bones[PRISM_MAX_BONES];

// Matriz de skinning do vertice, no espaco do modelo (rig): vertice da malha -> pose atual.
mat4 SkinMatrix() {
    if (u_Skinned == 0)
        return mat4(1.0);
    float sum = a_BoneWeights.x + a_BoneWeights.y + a_BoneWeights.z + a_BoneWeights.w;
    if (sum < 0.001)
        return mat4(1.0);
    return a_BoneWeights.x * u_Bones[a_BoneIds.x]
         + a_BoneWeights.y * u_Bones[a_BoneIds.y]
         + a_BoneWeights.z * u_Bones[a_BoneIds.z]
         + a_BoneWeights.w * u_Bones[a_BoneIds.w];
}

#endif
