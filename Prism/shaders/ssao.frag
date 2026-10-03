#version 450 core

in vec2 v_TexCoord;
layout(location = 0) out float o_Occlusion;

uniform sampler2D u_ViewNormal;  // GeometryBuffer - view-space
uniform sampler2D u_Depth;       // GeometryBuffer
uniform sampler2D u_NoiseTexture;

uniform vec3 u_Samples[16];
uniform mat4 u_Projection;
uniform mat4 u_InverseProjection;
uniform vec2 u_ScreenSize;
uniform vec2 u_NoiseScale; // ScreenSize / 4.0 (tamanho da textura de ruido) - repete o ruido "ladrilhado" por toda a tela

const int kKernelSize = 16;
const float kRadius = 0.5;     // raio da hemisfera de amostragem, em unidades de mundo (metros, assumindo 1 unidade = 1 metro) - ajustar aqui se cenas muito maiores/menores mostrarem AO fraco/exagerado demais
const float kBias = 0.025;     // evita "acne" de auto-oclusao por precisao limitada de profundidade, mesmo espirito do bias em CalculateShadow (Renderer.cpp)

// Reconstroi a posicao em VIEW-SPACE de um pixel a partir da sua
// profundidade (NDC) - "unproject" via a inversa da matriz de
// projecao, tecnica padrao para SSAO sem precisar guardar a
// posicao 3D inteira num G-buffer extra (so profundidade, que
// ja escrevemos de qualquer forma para o teste de profundidade
// normal do pre-pass).
vec3 ReconstructViewPos(vec2 texCoord) {
    float depthNDC = texture(u_Depth, texCoord).r * 2.0 - 1.0;
    vec4 clipPos = vec4(texCoord * 2.0 - 1.0, depthNDC, 1.0);
    vec4 viewPos = u_InverseProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    vec3 fragPos = ReconstructViewPos(v_TexCoord);
    vec3 normal = normalize(texture(u_ViewNormal, v_TexCoord).rgb);
    vec3 randomVec = normalize(texture(u_NoiseTexture, v_TexCoord * u_NoiseScale).xyz);

    // Base TBN (tangent/bitangent/normal) para orientar o kernel de
    // amostras (definido em tangent space, ver SSAO::GenerateKernelAndNoise)
    // ao longo da normal REAL de cada pixel - processo de
    // Gram-Schmidt simplificado (a "aleatoriedade" de randomVec e o
    // que produz a rotacao usada para quebrar o banding, ver
    // comentario em SSAO.cpp sobre a textura de ruido).
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < kKernelSize; i++) {
        vec3 samplePos = fragPos + (TBN * u_Samples[i]) * kRadius;

        vec4 offset = u_Projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5; // NDC -> [0,1] (coordenada de textura)

        float sampleDepthViewZ = ReconstructViewPos(offset.xy).z;

        // Range check: sem isto, geometria muito distante do ponto
        // amostrado (ex: uma parede longe atras de um objeto
        // pequeno) contaria como "oclusao" so por estar mais perto
        // da camera que o far plane - smoothstep suaviza a
        // transicao em vez de um corte abrupto (que apareceria
        // como uma borda visivel de AO ao redor de cada objeto).
        float rangeCheck = smoothstep(0.0, 1.0, kRadius / max(abs(fragPos.z - sampleDepthViewZ), 0.0001));
        occlusion += (sampleDepthViewZ >= samplePos.z + kBias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = occlusion / float(kKernelSize);
    o_Occlusion = 1.0 - occlusion; // convertido para "quanto de luz passa" - facilita o pass de cor final so multiplicar direto (ver u_AOMap em basic.frag)
}
