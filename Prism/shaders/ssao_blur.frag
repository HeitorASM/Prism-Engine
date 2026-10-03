#version 450 core

in vec2 v_TexCoord;
layout(location = 0) out float o_Occlusion;

uniform sampler2D u_SSAOTexture;

void main() {
    // Box blur 4x4 simples sobre o texel size do proprio SSAO
    // (nao um blur "geometry-aware"/bilateral que preservaria
    // bordas com mais fidelidade) - suficiente para remover o
    // ruido introduzido pela textura de rotacao 4x4 (ver
    // SSAO::GenerateKernelAndNoise) sem borrar visivelmente
    // silhuetas de objetos, dado que o proprio kernel de SSAO ja
    // e localizado (kRadius pequeno). Um blur bilateral (que leva
    // profundidade/normal em conta para nao misturar objetos
    // diferentes) e a evolucao natural se esta versao mostrar halo
    // perceptivel nas bordas dos objetos.
    vec2 texelSize = 1.0 / vec2(textureSize(u_SSAOTexture, 0));
    float result = 0.0;
    for (int x = -2; x < 2; x++) {
        for (int y = -2; y < 2; y++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(u_SSAOTexture, v_TexCoord + offset).r;
        }
    }
    o_Occlusion = result / 16.0;
}
