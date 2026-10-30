#version 450 core

// Especular pre-filtrado (split-sum, Karis/Unreal 2013): cada mip e o ambiente
// convoluido com o lobo GGX de uma rugosidade (mip / (niveis-1)). Importance
// sampling com escolha do mip pela densidade da amostra (menos ruido).

in vec3 v_Dir;
out vec4 o_Color;

uniform samplerCube u_Env;
uniform float u_Roughness;
uniform float u_EnvResolution;

const float kPi = 3.14159265358979;
const uint kSamples = 192u;

float RadicalInverse(uint bits) {
    return float(bitfieldReverse(bits)) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), RadicalInverse(i));
}

vec3 ImportanceSampleGGX(vec2 xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * kPi * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

void main() {
    vec3 N = normalize(v_Dir);
    vec3 V = N;

    vec3 color = vec3(0.0);
    float weight = 0.0;
    float saTexel = 4.0 * kPi / (6.0 * u_EnvResolution * u_EnvResolution);
    for (uint i = 0u; i < kSamples; i++) {
        vec3 H = ImportanceSampleGGX(Hammersley(i, kSamples), N, u_Roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = dot(N, L);
        if (NdotL > 0.0) {
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float pdf = DistributionGGX(NdotH, u_Roughness) * NdotH / (4.0 * HdotV) + 0.0001;
            float saSample = 1.0 / (float(kSamples) * pdf + 0.0001);
            float mip = u_Roughness <= 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
            color += textureLod(u_Env, L, mip).rgb * NdotL;
            weight += NdotL;
        }
    }
    o_Color = vec4(color / max(weight, 0.0001), 1.0);
}
