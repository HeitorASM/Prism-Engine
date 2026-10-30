// CullingTests.cpp - frustum culling e camadas (Renderer/Culling.h, so std).
#include <Prism/Renderer/Culling.h>

#include <cmath>
#include <cstdio>
#include <limits>

using namespace Prism::Culling;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Perspectiva GL (coluna-major), camera na origem olhando para -Z.
static void Perspective(float fovY, float aspect, float zn, float zf, float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    float t = 1.0f / std::tan(fovY * 0.5f);
    m[0] = t / aspect; m[5] = t;
    m[10] = -(zf + zn) / (zf - zn); m[11] = -1.0f;
    m[14] = -2.0f * zf * zn / (zf - zn);
}
static void Translation(float x, float y, float z, float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = 1; m[12] = x; m[13] = y; m[14] = z;
}
static void RotationY(float a, float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = std::cos(a); m[2] = -std::sin(a); m[5] = 1; m[8] = std::sin(a); m[10] = std::cos(a); m[15] = 1;
}
// Ortografica GL (como glm::ortho): caixa l..r, b..t, n..f na frente da camera (-Z).
static void Ortho(float l, float r, float b, float t, float n, float f, float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = 2.0f / (r - l); m[5] = 2.0f / (t - b); m[10] = -2.0f / (f - n); m[15] = 1.0f;
    m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n);
}
static void Scale(float s, float* m) { Translation(0, 0, 0, m); m[0] = m[5] = m[10] = s; }

int main() {
    // --- camadas ---
    CHECK(LayerVisible(1u, kAllLayers));
    CHECK(!LayerVisible(1u, 2u));
    CHECK(LayerVisible(0x6u, 0x4u));
    CHECK(!LayerVisible(0u, kAllLayers));      // objeto sem camada nunca aparece
    CHECK(!LayerVisible(kAllLayers, 0u));      // camera sem camada nao ve nada
    CHECK(LayerBit(0) == 1u && LayerBit(31) == 0x80000000u && LayerBit(32) == 0u && LayerBit(-1) == 0u);

    // --- frustum: fov 90, aspecto 1, perto 1, longe 100 ---
    float proj[16];
    Perspective(1.5707963f, 1.0f, 1.0f, 100.0f, proj);
    Frustum f = Frustum::FromViewProjection(proj);
    CHECK(f.Valid);
    const float unit[3] = { 0.5f, 0.5f, 0.5f };
    auto at = [&](float x, float y, float z) { float c[3] = { x, y, z }; return f.IntersectsBox(c, unit); };
    CHECK(at(0, 0, -10));          // na frente
    CHECK(!at(0, 0, 10));          // atras da camera
    CHECK(!at(0, 0, -200));        // alem do plano longe
    CHECK(!at(30, 0, -10));        // muito a direita (a 90 graus, |x| <= |z|)
    CHECK(!at(-30, 0, -10));
    CHECK(!at(0, 30, -10));
    CHECK(!at(0, -30, -10));
    CHECK(at(10, 0, -10));         // exatamente na borda (a caixa invade o plano)
    CHECK(at(10.3f, 0, -10));      // metade da caixa ainda dentro
    CHECK(!at(11.5f, 0, -10));     // toda fora

    // --- volume de LUZ (sombras): frustum ortografico das cascatas ---
    {
        float ortho[16];
        Ortho(-5, 5, -5, 5, 0.01f, 50.0f, ortho);
        Frustum light = Frustum::FromViewProjection(ortho);
        CHECK(light.Valid);
        const float half[3] = { 0.5f, 0.5f, 0.5f };
        auto in = [&](float x, float y, float z) { float c[3] = { x, y, z }; return light.IntersectsBox(c, half); };
        CHECK(in(0, 0, -10));
        CHECK(in(4.8f, 0, -10));       // caixa invade a borda lateral
        CHECK(!in(6.0f, 0, -10));      // fora do lado (nao projeta sombra nesta cascata)
        CHECK(!in(0, -6.0f, -10));
        CHECK(!in(0, 0, 5));           // atras do plano perto da luz
        CHECK(!in(0, 0, -60));         // alem do plano longe

        // caixa local girada/escalada dentro do volume: usa a mesma matriz de modelo do caster
        float rot[16], scl[16], model2[16] = {};
        RotationY(0.7f, rot); Scale(2.0f, scl);
        for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) {
            float s = 0; for (int k = 0; k < 4; k++) s += rot[k * 4 + r] * scl[c * 4 + k];
            model2[c * 4 + r] = s;
        }
        model2[12] = 0; model2[13] = 0; model2[14] = -10;
        const float lo[3] = { -1, -1, -1 }, hi[3] = { 1, 1, 1 };
        CHECK(light.IntersectsLocalBox(lo, hi, model2));
        model2[12] = 20.0f;
        CHECK(!light.IntersectsLocalBox(lo, hi, model2));
    }

    // --- caixa local + model ---
    const float mn[3] = { -1, -1, -1 }, mx[3] = { 1, 1, 1 };
    float model[16];
    Translation(0, 0, -10, model);
    CHECK(f.IntersectsLocalBox(mn, mx, model));
    Translation(0, 0, 10, model);
    CHECK(!f.IntersectsLocalBox(mn, mx, model));
    Translation(40, 0, -10, model);
    CHECK(!f.IntersectsLocalBox(mn, mx, model));
    Scale(50.0f, model); model[14] = -10; // caixa enorme na frente: cobre o frustum
    CHECK(f.IntersectsLocalBox(mn, mx, model));
    // Rotacao: caixa fina e comprida girada 90 graus em Y passa a invadir o frustum.
    const float thinMin[3] = { -20, -0.1f, -0.1f }, thinMax[3] = { 20, 0.1f, 0.1f };
    Translation(0, 0, -10, model);
    CHECK(f.IntersectsLocalBox(thinMin, thinMax, model));
    float rot[16], place[16], combined[16];
    RotationY(1.5707963f, rot);
    Translation(60, 0, -10, place);
    // combined = place * rot (so coluna 3 de 'place' importa): rotacao + translacao
    for (int i = 0; i < 16; i++) combined[i] = rot[i];
    combined[12] = place[12]; combined[13] = place[13]; combined[14] = place[14];
    CHECK(!f.IntersectsLocalBox(thinMin, thinMax, combined)); // comprimento agora no eixo Z, x=60 continua fora
    Translation(25, 0, -10, place);
    for (int i = 0; i < 16; i++) combined[i] = rot[i];
    combined[12] = place[12]; combined[13] = place[13]; combined[14] = place[14];
    CHECK(f.IntersectsLocalBox(thinMin, thinMax, combined));  // estende de z=-30 a z=10 em x=25: cruza o frustum

    // --- camera movida: view = translate(0,0,-20) => objeto em z=-30 mundo fica a -10 da camera? (camera em z=+20)
    float view[16], vp[16];
    Translation(0, 0, -20, view);
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) {
        float s = 0; for (int k = 0; k < 4; k++) s += proj[k * 4 + r] * view[c * 4 + k];
        vp[c * 4 + r] = s;
    }
    Frustum g = Frustum::FromViewProjection(vp);
    Translation(0, 0, -10, model);
    CHECK(g.IntersectsLocalBox(mn, mx, model));   // camera em z=20 olhando -Z: objeto a 30 de distancia
    Translation(0, 0, 40, model);
    CHECK(!g.IntersectsLocalBox(mn, mx, model));  // atras da camera

    // --- entradas invalidas nao descartam nada ---
    float bad[16] = {};
    bad[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!Frustum::FromViewProjection(bad).Valid);
    float zero[16] = {};
    CHECK(!Frustum::FromViewProjection(zero).Valid);
    CHECK(Frustum::FromViewProjection(zero).IntersectsLocalBox(mn, mx, model));
    const float nanMin[3] = { std::numeric_limits<float>::quiet_NaN(), 0, 0 };
    Translation(0, 0, 40, model);
    CHECK(f.IntersectsLocalBox(nanMin, mx, model)); // limites NaN: conservador

    std::printf("%d verificacoes, %d falha(s).\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
