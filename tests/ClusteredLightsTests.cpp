#include <Prism/Renderer/ClusteredLights.h>
#include <cstdio>
#include <cstring>
#include <random>
using namespace Prism::ClusteredLights;
static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU %s:%d %s\n", __FILE__, __LINE__, #c); g_failed++; } } while (0)

static void Perspective(float* m, float fovY, float aspect, float n, float f, float offX = 0, float offY = 0) {
    std::memset(m, 0, 16 * sizeof(float));
    const float t = 1.0f / std::tan(fovY * 0.5f);
    m[0] = t / aspect; m[5] = t; m[10] = -(f + n) / (f - n); m[11] = -1.0f; m[14] = -2.0f * f * n / (f - n);
    m[8] = offX; m[9] = offY;
}
// Camera em (cx,cy,cz) olhando para -Z com rotacao em Y.
static void ViewMatrix(float* m, float yaw, float cx, float cy, float cz) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    // R^T (mundo->view) com rotacao yaw em Y; translacao = -R^T * c
    float r[3][3] = { { c, 0, -s }, { 0, 1, 0 }, { s, 0, c } }; // linhas = eixos da camera no mundo (x, y, z)
    std::memset(m, 0, 16 * sizeof(float));
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) m[col * 4 + row] = r[row][col];
        m[12 + row] = -(r[row][0] * cx + r[row][1] * cy + r[row][2] * cz);
    }
    m[15] = 1;
}

static void RandomScene(unsigned seed, bool offCenter) {
    std::mt19937 rng(seed);
    auto U = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    float proj[16], view[16];
    const float n = U(0.05f, 0.5f), f = U(50.0f, 500.0f);
    Perspective(proj, U(0.6f, 1.6f), U(1.0f, 2.0f), n, f, offCenter ? U(-0.2f, 0.2f) : 0.0f, offCenter ? U(-0.2f, 0.2f) : 0.0f);
    ViewMatrix(view, U(-3.0f, 3.0f), U(-20, 20), U(-5, 5), U(-20, 20));
    Grid g = MakeGrid(view, proj);
    CHECK(g.Valid);
    CHECK(std::fabs(g.Near - n) < n * 1e-3f && std::fabs(g.Far - f) < f * 1e-3f);

    std::vector<Light> lights(40);
    for (auto& l : lights) { l.Position[0] = U(-60, 60); l.Position[1] = U(-20, 20); l.Position[2] = U(-90, 60); l.Range = U(0.5f, 25.0f); }
    Result res = Build(g, lights);
    CHECK(res.Overflowed == 0);
    CHECK(res.Clusters.size() == (size_t)kClusterCount * 2);

    // Todo ponto dentro do frustum afetado por uma luz tem que achar essa luz no seu cluster.
    int tested = 0, hits = 0;
    for (int i = 0; i < 20000; i++) {
        const float d = std::exp(U(std::log(g.Near * 1.001f), std::log(g.Far * 0.999f)));
        const float ndcX = U(-1, 1), ndcY = U(-1, 1);
        const float vx = (ndcX + g.P20) * d / g.P00, vy = (ndcY + g.P21) * d / g.P11;
        // view -> mundo: p = R * (v - t)  (R ortonormal: R = view3^T)
        float v[3] = { vx - g.View[12], vy - g.View[13], -d - g.View[14] };
        float p[3];
        for (int r = 0; r < 3; r++) p[r] = g.View[r * 4 + 0] * v[0] + g.View[r * 4 + 1] * v[1] + g.View[r * 4 + 2] * v[2];
        const int c = ClusterOfPoint(g, p);
        CHECK(c >= 0 && c < kClusterCount);
        if (c < 0) continue;
        const uint32_t off = res.Clusters[2 * c], cnt = res.Clusters[2 * c + 1];
        for (size_t li = 0; li < lights.size(); li++) {
            const float dx = p[0] - lights[li].Position[0], dy = p[1] - lights[li].Position[1], dz = p[2] - lights[li].Position[2];
            if (dx * dx + dy * dy + dz * dz >= lights[li].Range * lights[li].Range) continue;
            tested++;
            bool found = false;
            for (uint32_t k = 0; k < cnt; k++) if (res.Indices[off + k] == li) { found = true; break; }
            CHECK(found);
            hits += found;
        }
    }
    CHECK(tested > 50 && hits == tested);
}

int main() {
    for (unsigned s = 1; s <= 12; s++) RandomScene(s, false);
    for (unsigned s = 100; s <= 105; s++) RandomScene(s, true);

    // Ortografica / invalida: sem clusters
    float view[16], proj[16] = {};
    ViewMatrix(view, 0, 0, 0, 0);
    proj[0] = proj[5] = 0.1f; proj[10] = -0.02f; proj[14] = -1.0f; proj[15] = 1.0f;
    CHECK(!MakeGrid(view, proj).Valid);
    proj[0] = std::nanf("");
    Perspective(proj, 1.0f, 1.0f, 0.1f, 100.0f); proj[3] = std::nanf("");
    CHECK(!MakeGrid(view, proj).Valid);
    CHECK(Build(Grid{}, { Light{} }).Clusters.empty());

    // Luz atras da camera / alem do far / sem alcance: nao entra em nenhum cluster
    Perspective(proj, 1.0f, 1.0f, 0.1f, 100.0f);
    Grid g = MakeGrid(view, proj);
    std::vector<Light> ls(4);
    ls[0].Position[2] = 50;  ls[0].Range = 5;      // atras (camera olha para -Z)
    ls[1].Position[2] = -500; ls[1].Range = 5;     // alem do far
    ls[2].Position[2] = -10; ls[2].Range = 0;      // sem alcance
    ls[3].Position[0] = 500; ls[3].Position[2] = -10; ls[3].Range = 5; // longe do lado
    Result r = Build(g, ls);
    CHECK(r.Indices.empty());

    // Luz sobre a camera cobre todas as fatias proximas; cluster cheio descarta as ultimas
    std::vector<Light> many(100);
    for (auto& l : many) { l.Position[2] = -10; l.Range = 3; }
    Result cap = Build(g, many);
    CHECK(cap.Overflowed > 0);
    uint32_t maxCount = 0;
    for (int c = 0; c < kClusterCount; c++) maxCount = std::max(maxCount, cap.Clusters[2 * c + 1]);
    CHECK(maxCount == kMaxPerCluster);
    // Os kMaxPerCluster primeiros (mais relevantes) sao os que ficam
    for (int c = 0; c < kClusterCount; c++)
        if (cap.Clusters[2 * c + 1] == kMaxPerCluster) { CHECK(cap.Indices[cap.Clusters[2 * c]] == 0 && cap.Indices[cap.Clusters[2 * c] + kMaxPerCluster - 1] == kMaxPerCluster - 1); break; }

    CHECK(SliceOf(g, 0.0f) == 0 && SliceOf(g, 1e9f) == kSlices - 1 && SliceOf(g, g.Near * 1.0001f) == 0);
    std::printf(g_failed ? "FALHAS: %d\n" : "OK\n", g_failed);
    return g_failed ? 1 : 0;
}
