// Testes do buffer de comandos do desenho imediato (Renderer/DrawCommands.h): vida dos comandos,
// geometria das formas e protecao contra dados invalidos. So glm + std.
#include <Prism/Renderer/DrawCommands.h>

#include <cmath>
#include <cstdio>
#include <limits>

using namespace Prism;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const glm::vec4 kWhite(1.0f);

static void TestFrameLifetime() {
    DrawCommandBuffer b;
    CHECK(b.Line({ 0, 0, 0 }, { 1, 0, 0 }, kWhite));             // 1 quadro
    CHECK(b.Line({ 0, 0, 0 }, { 0, 1, 0 }, kWhite, 0.5f));       // 0,5 s
    CHECK(b.Count() == 2);
    b.NewFrame(0.1f);                                            // o de 1 quadro some
    CHECK(b.Count() == 1);
    b.NewFrame(0.3f);                                            // restam 0,1 s
    CHECK(b.Count() == 1);
    b.NewFrame(0.2f);                                            // acabou
    CHECK(b.Count() == 0);

    // dt invalido nao quebra nem apaga os temporizados antes da hora
    b.Line({ 0, 0, 0 }, { 1, 0, 0 }, kWhite, 1.0f);
    b.NewFrame(-5.0f);
    b.NewFrame(std::numeric_limits<float>::quiet_NaN());
    CHECK(b.Count() == 1);
    b.Clear();
    CHECK(b.Count() == 0);
}

static void TestShapes() {
    DrawCommandBuffer b;
    b.Box(glm::mat4(1.0f), kWhite);
    CHECK(b.Lines().size() == 12);                               // 12 arestas
    // cada aresta tem comprimento 1 (caixa unitaria)
    bool unit = true;
    for (const DrawLineCmd& c : b.Lines()) unit = unit && std::fabs(glm::length(c.A - c.B) - 1.0f) < 1e-5f;
    CHECK(unit);

    b.Clear();
    b.BoxAABB({ -1, 0, 2 }, { 1, 4, 3 }, kWhite);                // 2 x 4 x 1
    CHECK(b.Lines().size() == 12);
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const DrawLineCmd& c : b.Lines()) { lo = glm::min(lo, glm::min(c.A, c.B)); hi = glm::max(hi, glm::max(c.A, c.B)); }
    CHECK(std::fabs(lo.x + 1) < 1e-5f && std::fabs(hi.y - 4) < 1e-5f && std::fabs(lo.z - 2) < 1e-5f && std::fabs(hi.z - 3) < 1e-5f);

    b.Clear();
    CHECK(b.Sphere({ 1, 2, 3 }, 2.0f, kWhite, 0, true, 2, 16));
    CHECK(b.Lines().size() == 3 * 16);
    bool onSphere = true;
    for (const DrawLineCmd& c : b.Lines())
        onSphere = onSphere && std::fabs(glm::length(c.A - glm::vec3(1, 2, 3)) - 2.0f) < 1e-4f;
    CHECK(onSphere);

    b.Clear();
    CHECK(b.Sphere({ 0, 0, 0 }, 1.0f, kWhite, 0, true, 2, 1000));  // segmentos limitados a 64
    CHECK(b.Lines().size() == 3 * 64);
    CHECK(!b.Sphere({ 0, 0, 0 }, 0.0f, kWhite));
    CHECK(!b.Sphere({ 0, 0, 0 }, -1.0f, kWhite));

    b.Clear();
    CHECK(b.Arrow({ 0, 0, 0 }, { 0, 0, 5 }, kWhite));              // haste + 4 linhas da ponta
    CHECK(b.Lines().size() == 5);
    CHECK(!b.Arrow({ 1, 1, 1 }, { 1, 1, 1 }, kWhite));             // comprimento zero
    b.Clear();
    CHECK(b.Arrow({ 0, 0, 0 }, { 0, 3, 0 }, kWhite));              // vertical: o auxiliar perpendicular troca de eixo
    bool finite = true;
    for (const DrawLineCmd& c : b.Lines()) finite = finite && std::isfinite(c.A.x + c.A.y + c.A.z + c.B.x + c.B.y + c.B.z);
    CHECK(finite);

    b.Clear();
    CHECK(b.Axes(glm::mat4(1.0f), 2.0f));
    CHECK(b.Lines().size() == 3);
}

static void TestSanitizing() {
    DrawCommandBuffer b;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(!b.Line({ nan, 0, 0 }, { 1, 0, 0 }, kWhite));
    CHECK(!b.Line({ 0, 0, 0 }, { inf, 0, 0 }, kWhite));
    CHECK(!b.Line({ 0, 0, 0 }, { 1, 0, 0 }, glm::vec4(nan, 0, 0, 1)));
    CHECK(!b.Text2D("", 0, 0, kWhite));
    CHECK(!b.Text2D("x", nan, 0, kWhite));
    CHECK(!b.Text2D("x", 0, 0, kWhite, 0.0f));
    CHECK(!b.Text3D({ 0, 0, 0 }, "x", kWhite, -1.0f));
    CHECK(!b.Rect2D(0, 0, 0, 10, kWhite));
    CHECK(!b.Rect2D(0, 0, 10, -1, kWhite));
    CHECK(b.Count() == 0);

    // espessura e duracao limitadas
    CHECK(b.Line({ 0, 0, 0 }, { 1, 0, 0 }, kWhite, 1e9f, true, 1000.0f));
    CHECK(b.Lines()[0].Thickness == 32.0f);
    CHECK(b.Lines()[0].Remaining == 3600.0f);
    CHECK(b.Line({ 0, 0, 0 }, { 1, 0, 0 }, kWhite, 0.0f, true, 0.0f));
    CHECK(b.Lines()[1].Thickness == 1.0f);
    CHECK(!b.Lines()[1].Timed);

    // texto longo e cortado
    b.Clear();
    CHECK(b.Text2D(std::string(5000, 'a'), 0, 0, kWhite));
    CHECK(b.Texts()[0].Text.size() == DrawCommandBuffer::kMaxTextLength);
}

static void TestCapAndKinds() {
    DrawCommandBuffer b;
    size_t accepted = 0;
    for (size_t i = 0; i < DrawCommandBuffer::kMaxCommands + 50; i++)
        if (b.Line({ 0, 0, 0 }, { 1, 0, 0 }, kWhite, 10.0f)) accepted++;
    CHECK(accepted == DrawCommandBuffer::kMaxCommands);          // o excedente e descartado
    CHECK(!b.Rect2D(0, 0, 5, 5, kWhite));                        // cheio para todos os tipos
    b.Clear();

    CHECK(b.Line2D({ 0, 0 }, { 10, 10 }, kWhite));
    CHECK(b.Rect2D(1, 2, 3, 4, kWhite, false));
    CHECK(b.Text2D("ola", 5, 6, kWhite));
    CHECK(b.Text3D({ 1, 2, 3 }, "mundo", kWhite));
    CHECK(b.Lines().empty() && b.Lines2D().size() == 1 && b.Rects().size() == 1 && b.Texts().size() == 2);
    CHECK(b.Texts()[0].World == false && b.Texts()[1].World == true);
    CHECK(b.Count() == 4);
}

int main() {
    TestFrameLifetime();
    TestShapes();
    TestSanitizing();
    TestCapAndKinds();
    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
