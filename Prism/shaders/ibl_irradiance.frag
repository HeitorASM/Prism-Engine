#version 450 core

// Convolucao difusa: para cada normal, integra o cubemap de ambiente sobre o
// hemisferio (peso cos). Resultado = E / pi (basta multiplicar pelo albedo).
// Amostra um mip alto do ambiente para suavizar e evitar aliasing.

in vec3 v_Dir;
out vec4 o_Color;

uniform samplerCube u_Env;

const float kPi = 3.14159265358979;

void main() {
    vec3 N = normalize(v_Dir);
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    const float delta = 0.06;
    vec3 sum = vec3(0.0);
    float count = 0.0;
    for (float phi = 0.0; phi < 2.0 * kPi; phi += delta) {
        for (float theta = 0.0; theta < 0.5 * kPi; theta += delta) {
            vec3 t = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 s = t.x * right + t.y * up + t.z * N;
            sum += textureLod(u_Env, s, 3.0).rgb * cos(theta) * sin(theta);
            count += 1.0;
        }
    }
    o_Color = vec4(kPi * sum / count, 1.0);
}
