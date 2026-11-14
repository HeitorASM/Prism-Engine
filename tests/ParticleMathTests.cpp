// ParticleMathTests.cpp - curvas, gradiente, formas de emissao e flipbook
// (Particles/ParticleMath.h, so std).
#include <Prism/Particles/ParticleMath.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

using namespace Prism;
using namespace Prism::ParticleMath;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static_assert(std::is_trivially_copyable_v<ParticleCurve>, "serializacao/copia dependem disto");
static_assert(std::is_trivially_copyable_v<ParticleGradient>, "serializacao/copia dependem disto");

static void TestCurveDefaults() {
    ParticleCurve c;
    CHECK(Near(c.Evaluate(0.0f), 1.0f));
    CHECK(Near(c.Evaluate(0.5f), 1.0f));
    CHECK(Near(c.Evaluate(1.0f), 1.0f));
    CHECK(Near(ParticleCurve::Constant(3.0f).Evaluate(0.7f), 3.0f));
}

static void TestCurveEvaluate() {
    ParticleCurve c;
    c.V[0] = 0.0f; c.V[1] = 1.0f;       // rampa 0 -> 1
    CHECK(Near(c.Evaluate(0.25f), 0.25f));
    CHECK(Near(c.Evaluate(-5.0f), 0.0f));  // fora da faixa: segura
    CHECK(Near(c.Evaluate(9.0f), 1.0f));
    CHECK(Near(c.Evaluate(std::numeric_limits<float>::quiet_NaN()), 0.0f));

    // Pico no meio: 0 -> 1 -> 0
    CHECK(c.AddKey(0.5f, 2.0f) == 1);
    c.V[0] = 0.0f; c.V[2] = 0.0f;
    CHECK(c.Count == 3);
    CHECK(Near(c.Evaluate(0.25f), 1.0f));
    CHECK(Near(c.Evaluate(0.5f), 2.0f));
    CHECK(Near(c.Evaluate(0.75f), 1.0f));
}

static void TestCurveAddRemove() {
    ParticleCurve c;
    CHECK(c.AddKey(0.8f, 5.0f) == 1);
    CHECK(c.AddKey(0.2f, 4.0f) == 1);           // entra antes da de 0.8
    CHECK(Near(c.T[1], 0.2f) && Near(c.T[2], 0.8f));
    CHECK(c.AddKey(-3.0f, 7.0f) == 0 || c.T[0] == 0.0f); // t limitado a 0
    while (c.Count < (uint32_t)kParticleMaxCurveKeys)
        c.AddKey(0.5f, 1.0f);
    CHECK(c.AddKey(0.5f, 1.0f) == -1);          // cheia
    for (uint32_t i = 1; i < c.Count; i++)
        CHECK(c.T[i - 1] <= c.T[i]);

    while (c.Count > 2)
        CHECK(c.RemoveKey(1));
    CHECK(!c.RemoveKey(0));                      // sempre sobram 2
    CHECK(!c.RemoveKey(5));
}

static void TestCurveSanitize() {
    ParticleCurve c;
    c.Count = 99;                                // lixo
    for (int i = 0; i < kParticleMaxCurveKeys; i++) { c.T[i] = (float)(kParticleMaxCurveKeys - i) / 10.0f; c.V[i] = 100.0f; }
    c.V[2] = std::numeric_limits<float>::quiet_NaN();
    c.Sanitize(0.0f, 10.0f);
    CHECK(c.Count == (uint32_t)kParticleMaxCurveKeys);
    for (uint32_t i = 0; i < c.Count; i++) {
        CHECK(c.V[i] >= 0.0f && c.V[i] <= 10.0f);
        CHECK(std::isfinite(c.V[i]));
        if (i > 0) CHECK(c.T[i - 1] <= c.T[i]);
    }

    ParticleCurve one;
    one.Count = 1; one.V[0] = 2.0f; one.T[0] = 0.4f;
    one.Sanitize(0.0f, 10.0f);
    CHECK(one.Count == 2);
    CHECK(Near(one.Evaluate(0.0f), 2.0f) && Near(one.Evaluate(1.0f), 2.0f));

    ParticleCurve zero;
    zero.Count = 0;
    zero.Sanitize(0.0f, 10.0f);
    CHECK(zero.Count == 2);
}

static void TestGradient() {
    ParticleGradient g;
    float c[4];
    g.Evaluate(0.5f, c);
    CHECK(Near(c[0], 1) && Near(c[1], 1) && Near(c[2], 1) && Near(c[3], 1)); // branco neutro

    // vermelho opaco -> azul transparente
    g.C[0][0] = 1; g.C[0][1] = 0; g.C[0][2] = 0; g.C[0][3] = 1;
    g.C[1][0] = 0; g.C[1][1] = 0; g.C[1][2] = 1; g.C[1][3] = 0;
    g.Evaluate(0.5f, c);
    CHECK(Near(c[0], 0.5f) && Near(c[2], 0.5f) && Near(c[3], 0.5f));
    g.Evaluate(-1.0f, c);
    CHECK(Near(c[0], 1.0f) && Near(c[3], 1.0f));
    g.Evaluate(2.0f, c);
    CHECK(Near(c[2], 1.0f) && Near(c[3], 0.0f));

    // Adicionar parada herda a cor do ponto
    int idx = g.AddStop(0.5f);
    CHECK(idx == 1 && g.Count == 3);
    CHECK(Near(g.C[1][0], 0.5f) && Near(g.C[1][3], 0.5f));
    // Colorir a do meio de verde e checar
    g.C[1][0] = 0; g.C[1][1] = 1; g.C[1][2] = 0; g.C[1][3] = 1;
    g.Evaluate(0.5f, c);
    CHECK(Near(c[1], 1.0f));
    g.Evaluate(0.25f, c);
    CHECK(Near(c[0], 0.5f) && Near(c[1], 0.5f));

    // Arrastar a parada do meio para alem da ultima: reordena e mantem o indice
    g.T[1] = 1.0f;
    g.T[2] = 0.9f;
    int newIndex = g.SortKeepingIndex(1);
    CHECK(newIndex == 2);
    CHECK(g.T[1] <= g.T[2]);
    CHECK(Near(g.C[2][1], 1.0f)); // a verde foi junto

    while (g.Count < (uint32_t)kParticleMaxGradientStops) g.AddStop(0.3f);
    CHECK(g.AddStop(0.3f) == -1);
    while (g.Count > 2) CHECK(g.RemoveStop(1));
    CHECK(!g.RemoveStop(0));
}

static void TestGradientSanitize() {
    ParticleGradient g;
    g.Count = 200;
    g.C[0][0] = std::numeric_limits<float>::infinity();
    g.C[1][3] = -5.0f;
    g.Sanitize();
    CHECK(g.Count == (uint32_t)kParticleMaxGradientStops);
    CHECK(std::isfinite(g.C[0][0]) && g.C[0][0] <= 4.0f);
    CHECK(g.C[1][3] >= 0.0f);
}

static void CheckUnit(const float v[3]) {
    CHECK(Near(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), 1.0f, 1e-3f));
}

static void TestShapeCone() {
    uint32_t seed = 12345;
    ShapeParams p;
    p.Type = ParticleShape::Cone;
    p.SpreadAngleDeg = 0.0f;
    for (int i = 0; i < 50; i++) {
        ShapeSample s = SampleShape(p, seed);
        CHECK(Near(s.Position[0], 0) && Near(s.Position[1], 0) && Near(s.Position[2], 0));
        CHECK(Near(s.Direction[1], 1.0f, 1e-3f));   // reto
    }
    p.SpreadAngleDeg = 30.0f;
    const float cosMax = std::cos(30.0f * kPi / 180.0f);
    for (int i = 0; i < 500; i++) {
        ShapeSample s = SampleShape(p, seed);
        CheckUnit(s.Direction);
        CHECK(s.Direction[1] >= cosMax - 1e-3f);
    }
    p.SpreadAngleDeg = 180.0f; // esfera inteira: ha direcoes para baixo
    bool anyDown = false;
    for (int i = 0; i < 500; i++) anyDown |= SampleShape(p, seed).Direction[1] < 0.0f;
    CHECK(anyDown);
}

static void TestShapeSphere() {
    uint32_t seed = 777;
    ShapeParams p;
    p.Type = ParticleShape::Sphere;
    p.Radius = 2.0f;
    p.Thickness = 0.0f; // so superficie
    for (int i = 0; i < 300; i++) {
        ShapeSample s = SampleShape(p, seed);
        float r = std::sqrt(s.Position[0] * s.Position[0] + s.Position[1] * s.Position[1] + s.Position[2] * s.Position[2]);
        CHECK(Near(r, 2.0f, 1e-3f));
        CheckUnit(s.Direction);
        // direcao radial: posicao / raio
        CHECK(Near(s.Position[0] / r, s.Direction[0], 1e-3f));
    }
    p.Thickness = 1.0f; // volume
    float maxR = 0.0f, minR = 99.0f;
    for (int i = 0; i < 2000; i++) {
        ShapeSample s = SampleShape(p, seed);
        float r = std::sqrt(s.Position[0] * s.Position[0] + s.Position[1] * s.Position[1] + s.Position[2] * s.Position[2]);
        maxR = std::max(maxR, r); minR = std::min(minR, r);
    }
    CHECK(maxR <= 2.0f + 1e-3f);
    CHECK(minR < 0.8f); // preenche o interior
    p.Thickness = 0.25f; // casca [1.5, 2.0]
    for (int i = 0; i < 300; i++) {
        ShapeSample s = SampleShape(p, seed);
        float r = std::sqrt(s.Position[0] * s.Position[0] + s.Position[1] * s.Position[1] + s.Position[2] * s.Position[2]);
        CHECK(r >= 1.5f - 1e-3f && r <= 2.0f + 1e-3f);
    }
}

static void TestShapeHemisphere() {
    uint32_t seed = 99;
    ShapeParams p;
    p.Type = ParticleShape::Hemisphere;
    p.Radius = 1.0f;
    for (int i = 0; i < 300; i++) {
        ShapeSample s = SampleShape(p, seed);
        CHECK(s.Position[1] >= -1e-5f);
        CHECK(s.Direction[1] >= -1e-5f);
    }
}

static void TestShapeBoxCircleEdge() {
    uint32_t seed = 4242;
    ShapeParams p;
    p.Type = ParticleShape::Box;
    p.Box[0] = 2.0f; p.Box[1] = 1.0f; p.Box[2] = 4.0f;
    p.SpreadAngleDeg = 0.0f;
    for (int i = 0; i < 300; i++) {
        ShapeSample s = SampleShape(p, seed);
        CHECK(std::fabs(s.Position[0]) <= 1.0f + 1e-4f);
        CHECK(std::fabs(s.Position[1]) <= 0.5f + 1e-4f);
        CHECK(std::fabs(s.Position[2]) <= 2.0f + 1e-4f);
        CHECK(Near(s.Direction[1], 1.0f, 1e-3f));
    }

    p = ShapeParams{};
    p.Type = ParticleShape::Circle;
    p.Radius = 3.0f;
    p.Thickness = 0.0f; // so aro
    p.SpreadAngleDeg = 0.0f;
    for (int i = 0; i < 300; i++) {
        ShapeSample s = SampleShape(p, seed);
        float r = std::sqrt(s.Position[0] * s.Position[0] + s.Position[2] * s.Position[2]);
        CHECK(Near(r, 3.0f, 1e-3f));
        CHECK(Near(s.Position[1], 0.0f));
    }
    p.RadialBlend = 1.0f; // para fora, no plano
    for (int i = 0; i < 300; i++) {
        ShapeSample s = SampleShape(p, seed);
        CHECK(Near(s.Direction[1], 0.0f, 1e-3f));
        float r = std::sqrt(s.Position[0] * s.Position[0] + s.Position[2] * s.Position[2]);
        CHECK(Near(s.Direction[0], s.Position[0] / r, 1e-3f));
        CheckUnit(s.Direction);
    }

    p = ShapeParams{};
    p.Type = ParticleShape::Edge;
    p.Radius = 1.5f;
    for (int i = 0; i < 300; i++) {
        ShapeSample s = SampleShape(p, seed);
        CHECK(std::fabs(s.Position[0]) <= 1.5f + 1e-4f);
        CHECK(Near(s.Position[1], 0) && Near(s.Position[2], 0));
    }
}

static void TestShapeRobustness() {
    uint32_t seed = 1;
    ShapeParams p;
    p.Type = ParticleShape::Sphere;
    p.Radius = std::numeric_limits<float>::quiet_NaN();
    p.Thickness = std::numeric_limits<float>::infinity();
    p.SpreadAngleDeg = std::numeric_limits<float>::quiet_NaN();
    for (int t = 0; t <= 5; t++) {
        p.Type = (ParticleShape)t;
        ShapeSample s = SampleShape(p, seed);
        CHECK(std::isfinite(s.Position[0]) && std::isfinite(s.Position[1]) && std::isfinite(s.Position[2]));
        CHECK(std::isfinite(s.Direction[0]) && std::isfinite(s.Direction[1]) && std::isfinite(s.Direction[2]));
        CheckUnit(s.Direction);
    }
    // Determinismo: mesma semente, mesma sequencia
    uint32_t a = 555, b = 555;
    ShapeParams q;
    q.Type = ParticleShape::Box;
    for (int i = 0; i < 20; i++) {
        ShapeSample sa = SampleShape(q, a), sb = SampleShape(q, b);
        CHECK(sa.Position[0] == sb.Position[0] && sa.Direction[2] == sb.Direction[2]);
    }
}

static void TestFlipbook() {
    // 4x4 = 16 quadros
    CHECK(FlipbookFrame(ParticleFlipbookMode::OverLifetime, 4, 4, 0.0f, 0.0f, 12, 0.0f) == 0);
    CHECK(FlipbookFrame(ParticleFlipbookMode::OverLifetime, 4, 4, 0.5f, 0.0f, 12, 0.0f) == 8);
    CHECK(FlipbookFrame(ParticleFlipbookMode::OverLifetime, 4, 4, 1.0f, 0.0f, 12, 0.0f) == 15); // nunca passa do ultimo
    CHECK(FlipbookFrame(ParticleFlipbookMode::OverLifetime, 4, 4, 7.0f, 0.0f, 12, 0.0f) == 15);
    CHECK(FlipbookFrame(ParticleFlipbookMode::FixedFPS, 2, 2, 0.0f, 0.5f, 8.0f, 0.0f) == 0);   // 4 quadros -> 8*0.5 = 4 % 4 = 0
    CHECK(FlipbookFrame(ParticleFlipbookMode::FixedFPS, 2, 2, 0.0f, 0.3f, 8.0f, 0.0f) == 2);   // 2.4 -> 2
    CHECK(FlipbookFrame(ParticleFlipbookMode::RandomStatic, 2, 2, 0.0f, 0.0f, 8.0f, 0.99f) == 3);
    CHECK(FlipbookFrame(ParticleFlipbookMode::RandomStatic, 2, 2, 0.0f, 0.0f, 8.0f, 1.0f) == 3); // limite superior
    CHECK(FlipbookFrame(ParticleFlipbookMode::OverLifetime, 1, 1, 0.9f, 0.0f, 8.0f, 0.5f) == 0); // 1x1 = sem flipbook
    CHECK(FlipbookFrame(ParticleFlipbookMode::OverLifetime, 0, 0, 0.9f, 0.0f, 8.0f, 0.5f) == 0); // invalido
    CHECK(FlipbookFrame(ParticleFlipbookMode::FixedFPS, 2, 2, 0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f) == 0);
}

int main() {
    TestCurveDefaults();
    TestCurveEvaluate();
    TestCurveAddRemove();
    TestCurveSanitize();
    TestGradient();
    TestGradientSanitize();
    TestShapeCone();
    TestShapeSphere();
    TestShapeHemisphere();
    TestShapeBoxCircleEdge();
    TestShapeRobustness();
    TestFlipbook();
    std::printf("%d verificacoes, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
