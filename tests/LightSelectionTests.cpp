// LightSelectionTests.cpp - escolha das luzes por relevancia (Renderer/LightSelection.h, so std).
#include <Prism/Renderer/LightSelection.h>

#include <cmath>
#include <cstdio>
#include <limits>

using namespace Prism;
using LightSelection::Candidate;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Perspectiva GL (coluna-major), camera na origem olhando para -Z (mesma de CullingTests).
static void Perspective(float fovY, float aspect, float zn, float zf, float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    float t = 1.0f / std::tan(fovY * 0.5f);
    m[0] = t / aspect; m[5] = t;
    m[10] = -(zf + zn) / (zf - zn); m[11] = -1.0f;
    m[14] = -2.0f * zf * zn / (zf - zn);
}

static Candidate Point(uint32_t id, float x, float y, float z, float range = 10.0f, float intensity = 1.0f) {
    Candidate c;
    c.Id = id; c.Type = 0;
    c.Position[0] = x; c.Position[1] = y; c.Position[2] = z;
    c.Range = range; c.Intensity = intensity;
    return c;
}

// Id da primeira luz escolhida (0xFFFFFFFF se nenhuma).
static uint32_t lights_id(const std::vector<Candidate>& lights, const std::vector<uint32_t>& chosen) {
    return chosen.empty() ? 0xFFFFFFFFu : lights[chosen[0]].Id;
}

int main() {
    float proj[16];
    Perspective(1.5707963f, 1.0f, 1.0f, 100.0f, proj);
    const Culling::Frustum frustum = Culling::Frustum::FromViewProjection(proj);
    const float camera[3] = { 0, 0, 0 };
    const std::vector<uint32_t> none;

    // --- o bug original: mais luzes que o limite, na ordem "errada" do registro ---
    {
        std::vector<Candidate> lights;
        for (uint32_t i = 0; i < 20; i++)                      // registradas da MAIS DISTANTE para a mais proxima
            lights.push_back(Point(i, 0, 0, -(2.0f + (19 - i) * 3.0f)));
        auto chosen = LightSelection::Select(lights, frustum, camera, 16, none);
        CHECK(chosen.size() == 16);
        // as 16 mais proximas sao os Ids 4..19 (z mais perto), da mais proxima para a mais distante
        bool ok = chosen.size() == 16;
        for (size_t k = 0; ok && k < chosen.size(); k++)
            ok = lights[chosen[k]].Id == (uint32_t)(19 - k);
        CHECK(ok);
    }

    // --- directional entra sempre e vem primeiro, mesmo fraca ---
    {
        Candidate sun; sun.Id = 99; sun.Type = 2; sun.Intensity = 0.01f;
        std::vector<Candidate> lights = { Point(1, 0, 0, -3), sun, Point(2, 0, 0, -5) };
        auto chosen = LightSelection::Select(lights, frustum, camera, 2, none);
        CHECK(chosen.size() == 2 && lights[chosen[0]].Id == 99 && lights[chosen[1]].Id == 1);
    }

    // --- alcance x frustum ---
    {
        std::vector<Candidate> lights = {
            Point(1, 0, 0, 20, 5.0f),      // atras da camera, alcance curto: nao ilumina nada visivel
            Point(2, 0, 0, 20, 30.0f),     // atras, mas o alcance entra no frustum
            Point(3, 500, 0, -10, 5.0f),   // muito para o lado
            Point(4, 0, 0, -10, 5.0f),     // na frente
            Point(5, 0, 0, 500, 0.0f),     // sem alcance (Range <= 0): nunca descartada
        };
        auto chosen = LightSelection::Select(lights, frustum, camera, 16, none);
        bool has[6] = {};
        for (uint32_t index : chosen) has[lights[index].Id] = true;
        CHECK(!has[1]); CHECK(has[2]); CHECK(!has[3]); CHECK(has[4]); CHECK(has[5]);
    }

    // --- intensidade zero / NaN saem ---
    {
        std::vector<Candidate> lights = { Point(1, 0, 0, -3, 10, 0.0f), Point(2, 0, 0, -3, 10, std::numeric_limits<float>::quiet_NaN()), Point(3, 0, 0, -3) };
        auto chosen = LightSelection::Select(lights, frustum, camera, 16, none);
        CHECK(chosen.size() == 1 && lights[chosen[0]].Id == 3);
    }

    // --- empate: menor Id primeiro; resultado deterministico ---
    {
        std::vector<Candidate> lights = { Point(7, 0, 0, -4), Point(3, 0, 0, -4), Point(5, 0, 0, -4) };
        auto a = LightSelection::Select(lights, frustum, camera, 16, none);
        auto b = LightSelection::Select(lights, frustum, camera, 16, none);
        CHECK(a == b);
        CHECK(a.size() == 3 && lights[a[0]].Id == 3 && lights[a[1]].Id == 5 && lights[a[2]].Id == 7);
    }

    // --- histerese: vantagem pequena nao troca a luz escolhida; vantagem grande troca ---
    {
        // Score = I * R^2 / (R^2 + d^2), R = 10. A em z=-6 (0.735), B em z=-5.5 (0.768, +4%), C em z=-2 (0.962, +31%).
        std::vector<Candidate> close = { Point(1, 0, 0, -6), Point(2, 0, 0, -5.5) };
        CHECK(lights_id(close, LightSelection::Select(close, frustum, camera, 1, none)) == 2);   // sem historico: B vence
        const std::vector<uint32_t> previousA = { 1 };
        CHECK(lights_id(close, LightSelection::Select(close, frustum, camera, 1, previousA)) == 1); // A ja estava: fica

        std::vector<Candidate> distant = { Point(1, 0, 0, -6), Point(3, 0, 0, -2) };
        CHECK(lights_id(distant, LightSelection::Select(distant, frustum, camera, 1, previousA)) == 3);     // C e muito melhor: troca
    }

    std::printf("%d verificacoes, %d falha(s).\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
