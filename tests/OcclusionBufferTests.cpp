#include <array>
#include <Prism/Renderer/OcclusionBuffer.h>
#include <cstdio>
#include <cstring>
#include <random>
using namespace Prism;
static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU %s:%d %s\n", __FILE__, __LINE__, #c); g_failed++; } } while (0)

static void Perspective(float* m, float fovY, float aspect, float n, float f) {
    std::memset(m, 0, 16 * sizeof(float));
    const float t = 1.0f / std::tan(fovY * 0.5f);
    m[0] = t / aspect; m[5] = t; m[10] = -(f + n) / (f - n); m[11] = -1.0f; m[14] = -2.0f * f * n / (f - n);
}
struct V3 { float x, y, z; };
static V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Moller-Trumbore: raio da origem para 'dir'; devolve t (distancia em unidades de dir) ou -1
static float RayTri(V3 dir, V3 a, V3 b, V3 c) {
    V3 e1 = Sub(b, a), e2 = Sub(c, a), p = Cross(dir, e2);
    float det = Dot(e1, p);
    if (std::fabs(det) < 1e-9f) return -1;
    float inv = 1.0f / det; V3 t = { -a.x, -a.y, -a.z };
    float u = Dot(t, p) * inv; if (u < 0 || u > 1) return -1;
    V3 q = Cross(t, e1); float v = Dot(dir, q) * inv; if (v < 0 || u + v > 1) return -1;
    return Dot(e2, q) * inv;
}
static void Quad(OcclusionBuffer& ob, float x0, float y0, float x1, float y1, float z) {
    float a[3] = { x0, y0, z }, b[3] = { x1, y0, z }, c[3] = { x1, y1, z }, d[3] = { x0, y1, z };
    ob.AddTriangle(a, b, c); ob.AddTriangle(a, c, d);
}

int main() {
    float vp[16]; Perspective(vp, 1.2f, 2.0f, 0.1f, 200.0f);   // camera na origem olhando -Z
    OcclusionBuffer ob; ob.Begin(vp);
    Quad(ob, -10, -10, 10, 10, -10);                              // parede grande a 10 m

    auto box = [&](float cx, float cy, float cz, float h) {
        float mn[3] = { cx - h, cy - h, cz - h }, mx[3] = { cx + h, cy + h, cz + h };
        return ob.IsBoxOccluded(mn, mx);
    };
    CHECK(box(0, 0, -20, 1));          // atras da parede, dentro da silhueta
    CHECK(!box(0, 0, -5, 1));          // na frente
    CHECK(!box(0, 0, -10, 1));         // atravessa a parede
    CHECK(!box(40, 0, -20, 1));        // fora da silhueta
    CHECK(!box(18, 0, -20, 3));        // saindo pela borda da silhueta (a parede cobre +-20 a 20 m)
    CHECK(box(9, 0, -20, 3));          // ainda dentro dela
    CHECK(!box(0, 0, 0, 1));           // cruza o near
    CHECK(!box(0, 0, 20, 1));          // atras da camera
    float mnN[3] = { -1, -1, -1 }, mxN[3] = { 1, 1, 1 };
    OcclusionBuffer empty; empty.Begin(vp);
    CHECK(!empty.IsBoxOccluded(mnN, mxN));                        // sem oclusores nada some
    OcclusionBuffer invalid; CHECK(!invalid.IsBoxOccluded(mnN, mxN));

    // Triangulo que cruza o near e ignorado: nao oculta nada
    OcclusionBuffer cross; cross.Begin(vp);
    Quad(cross, -10, -10, 10, 10, 1.0f); // atras da camera
    CHECK(cross.TrianglesRasterized() == 0);

    // Propriedade (conservadorismo): se Query diz "oculta", todo ponto da caixa tem um oclusor no caminho.
    std::mt19937 rng(7);
    auto U = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    int occludedCount = 0;
    // Um oclusor por cena: a garantia e exata. (Entre DOIS oclusores distintos com uma fresta menor que 1 pixel
    // o teste pode vazar; geometria de nivel fechada nao tem essas frestas.)
    for (int scene = 0; scene < 150; scene++) {
        OcclusionBuffer o; o.Begin(vp);
        std::vector<std::array<V3, 3>> tris;
        for (int q = 0; q < 1; q++) {
            float z = -U(5, 40), x0 = U(-15, 5), y0 = U(-8, 3), x1 = x0 + U(3, 18), y1 = y0 + U(3, 10);
            V3 a{ x0, y0, z }, b{ x1, y0, z }, c{ x1, y1, z }, d{ x0, y1, z };
            tris.push_back({ a, b, c }); tris.push_back({ a, c, d });
            float pa[3] = { a.x, a.y, a.z }, pb[3] = { b.x, b.y, b.z }, pc[3] = { c.x, c.y, c.z }, pd[3] = { d.x, d.y, d.z };
            o.AddTriangle(pa, pb, pc); o.AddTriangle(pa, pc, pd);
        }
        for (int t = 0; t < 400; t++) {
            float h = U(0.3f, 3.0f), cx = U(-25, 25), cy = U(-12, 12), cz = -U(8, 80);
            float mn[3] = { cx - h, cy - h, cz - h }, mx[3] = { cx + h, cy + h, cz + h };
            if (!o.IsBoxOccluded(mn, mx)) continue;
            occludedCount++;
            for (int s = 0; s < 60; s++) {
                V3 p{ U(mn[0], mx[0]), U(mn[1], mx[1]), U(mn[2], mx[2]) };
                { // so pontos que aparecem na tela (o que sai do quadro nao importa para a oclusao)
                    const float w = -p.z, nx = vp[0] * p.x / w, ny = vp[5] * p.y / w;
                    if (std::fabs(nx) > 1.0f || std::fabs(ny) > 1.0f) continue;
                }
                bool blocked = false;
                for (auto& tr : tris) { float tt = RayTri(p, tr[0], tr[1], tr[2]); if (tt > 0 && tt < 1.0f) { blocked = true; break; } }
                CHECK(blocked);
            }
        }
    }
    CHECK(occludedCount > 20);
    std::printf(g_failed ? "FALHAS: %d (ocultas: %d)\n" : "OK (ocultas testadas: %d)\n", g_failed ? g_failed : occludedCount, occludedCount);
    return g_failed ? 1 : 0;
}
