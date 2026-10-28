// Testes do leitor de parametros de shader (std-only).
#include "Prism/Renderer/ShaderParams.h"
#include <cstdio>
#include <limits>
using namespace Prism;

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    auto p = ShaderParams::Parse(
        "uniform float u_Brilho = 1.5; // range(0, 4)\n"
        "  uniform vec3 u_Cor = vec3(1.0, 0.3, 0.1f); // color\n"
        "uniform bool u_Pulsar = true;\n"
        "uniform int u_Passos = 3; // range(1, 8)\n"
        "uniform vec2 u_Velocidade;\n"
        "uniform vec4 u_Tint = vec4(0.5); // color\n"
        "uniform sampler2D u_Tex;\n"
        "uniform float u_Arr[4];\n"
        "uniform float u_Brilho = 9.0;\n"
        "// uniform float u_Comentado = 1.0;\n"
        "/* uniform float u_Bloco = 1.0;\n   uniform float u_Bloco2; */\n"
        "uniform float u_Depois = -2.5e0;\n"
        "uniformfloat x;\n");
    CHECK(p.size() == 7);
    CHECK(p[0].Name == "u_Brilho" && p[0].Type == ShaderParamType::Float && p[0].Default[0] == 1.5f);
    CHECK(p[0].HasRange && p[0].Min == 0.0f && p[0].Max == 4.0f && !p[0].IsColor);
    CHECK(p[1].Type == ShaderParamType::Vec3 && p[1].IsColor && p[1].Default[1] == 0.3f && p[1].Default[2] == 0.1f);
    CHECK(p[2].Type == ShaderParamType::Bool && p[2].Default[0] == 1.0f);
    CHECK(p[3].Type == ShaderParamType::Int && p[3].Default[0] == 3.0f && p[3].Min == 1.0f && p[3].Max == 8.0f);
    CHECK(p[4].Type == ShaderParamType::Vec2 && p[4].Default[0] == 0.0f && p[4].Components() == 2);
    CHECK(p[5].Type == ShaderParamType::Vec4 && p[5].Default[3] == 0.5f && p[5].IsColor); // vec4(0.5) repete
    CHECK(p[6].Name == "u_Depois" && p[6].Default[0] == -2.5f);

    // CRLF e entrada vazia/lixo
    CHECK(ShaderParams::Parse("").empty());
    auto crlf = ShaderParams::Parse("uniform float a = 2.0;\r\nuniform bool b = false;\r\n");
    CHECK(crlf.size() == 2 && crlf[0].Default[0] == 2.0f && crlf[1].Default[0] == 0.0f);
    CHECK(ShaderParams::Parse("uniform float a = banana;").size() == 1); // inicializador ruim: padrao 0
    CHECK(ShaderParams::Parse("uniform float a = banana;")[0].Default[0] == 0.0f);
    CHECK(ShaderParams::Parse("uniform vec3 v = vec3(1.0, 2.0);")[0].Default[0] == 0.0f); // numero errado de componentes
    CHECK(ShaderParams::Parse("uniform float f; // range(5, 1)")[0].HasRange == false);     // faixa invertida
    CHECK(ShaderParams::Parse("uniform float f; // color")[0].IsColor == false);            // cor so vec3/vec4
    CHECK(ShaderParams::Parse("/* aberto uniform float x;").empty());                       // comentario sem fim

    // limite de quantidade
    std::string many;
    for (int i = 0; i < 100; i++) many += "uniform float u_" + std::to_string(i) + ";\n";
    CHECK(ShaderParams::Parse(many).size() == 64);

    // Clamp
    ShaderParamDecl d; d.Type = ShaderParamType::Float; d.HasRange = true; d.Min = 0; d.Max = 4; d.Default[0] = 1;
    float v[4] = { 9.0f, 0, 0, 0 }; ShaderParams::Clamp(d, v); CHECK(v[0] == 4.0f);
    v[0] = std::numeric_limits<float>::quiet_NaN(); ShaderParams::Clamp(d, v); CHECK(v[0] == 1.0f);
    ShaderParamDecl b; b.Type = ShaderParamType::Bool; float bv[4] = { 0.3f, 0, 0, 0 }; ShaderParams::Clamp(b, bv); CHECK(bv[0] == 1.0f);
    ShaderParamDecl in; in.Type = ShaderParamType::Int; float iv[4] = { 2.6f, 0, 0, 0 }; ShaderParams::Clamp(in, iv); CHECK(iv[0] == 3.0f);

    std::printf("%d verificacoes, %d falha(s).\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
