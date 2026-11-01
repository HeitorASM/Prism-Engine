// Testes da matematica da UI em espaco de mundo (UI/UIWorld.h): raio x canvas, billboard e
// raio a partir da tela. So glm.
#include <Prism/UI/UIWorld.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>

using namespace Prism::Ui;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

static void TestRayCanvas() {
    // Canvas 800x600 a 200 px/m = 4 x 3 m, centrado em (0,0,-5), de frente para +Z.
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -5));
    const glm::vec3 forward(0, 0, -1);

    CanvasHit center = RayCanvasHit({ 0, 0, 0 }, forward, model, 800, 600, 200);
    CHECK(center.Hit && Near(center.X, 400) && Near(center.Y, 300) && Near(center.Distance, 5));

    // 1 m para a direita e 0,5 m para cima -> pixel (600, 200) (Y do canvas cresce para baixo)
    CanvasHit offset = RayCanvasHit({ 1, 0.5f, 0 }, forward, model, 800, 600, 200);
    CHECK(offset.Hit && Near(offset.X, 600) && Near(offset.Y, 200));

    // fora do retangulo
    CHECK(!RayCanvasHit({ 2.5f, 0, 0 }, forward, model, 800, 600, 200).Hit);
    CHECK(!RayCanvasHit({ 0, 1.6f, 0 }, forward, model, 800, 600, 200).Hit);
    // por tras: a camera do outro lado olhando para +Z
    CHECK(!RayCanvasHit({ 0, 0, -10 }, { 0, 0, 1 }, model, 800, 600, 200).Hit);
    // canvas atras da origem do raio
    CHECK(!RayCanvasHit({ 0, 0, -10 }, forward, model, 800, 600, 200).Hit);
    // paralelo ao plano
    CHECK(!RayCanvasHit({ 0, 0, 0 }, { 1, 0, 0 }, model, 800, 600, 200).Hit);
    // parametros invalidos
    CHECK(!RayCanvasHit({ 0, 0, 0 }, forward, model, 0, 600, 200).Hit);
    CHECK(!RayCanvasHit({ 0, 0, 0 }, forward, model, 800, 600, 0).Hit);
    CHECK(!RayCanvasHit({ 0, 0, 0 }, forward, glm::scale(glm::mat4(1.0f), glm::vec3(0.0f)), 800, 600, 200).Hit);

    // escala 2x dobra o tamanho no mundo: 1 m a direita agora e metade do caminho do centro a borda
    const glm::mat4 scaled = model * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    CanvasHit s = RayCanvasHit({ 1, 0, 0 }, forward, scaled, 800, 600, 200);
    CHECK(s.Hit && Near(s.X, 500) && Near(s.Distance, 5));

    // painel girado 90 graus em Y: de frente para +X
    const glm::mat4 rotated = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, 0)) * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    // quem olha de +X para -X ve a frente; vindo de -X (por tras) nao acerta
    CanvasHit r = RayCanvasHit({ 10, 0, 0 }, { -1, 0, 0 }, rotated, 800, 600, 200);
    CHECK(r.Hit && Near(r.X, 400) && Near(r.Y, 300) && Near(r.Distance, 5));
    CHECK(!RayCanvasHit({ 0, 0, 0 }, { 1, 0, 0 }, rotated, 800, 600, 200).Hit);
}

static void TestBillboard() {
    // Camera em (0,0,10) olhando para -Z: view = translate(0,0,-10). O painel gira para a camera.
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -10));
    glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(3, 1, 0)) *
                      glm::rotate(glm::mat4(1.0f), glm::radians(70.0f), glm::vec3(0, 1, 0)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(2, 3, 1));
    glm::mat4 model = CanvasModel(world, true, view);
    CHECK(Near(model[3].x, 3) && Near(model[3].y, 1));                         // posicao preservada
    CHECK(Near(glm::length(glm::vec3(model[0])), 2) && Near(glm::length(glm::vec3(model[1])), 3)); // escala preservada
    CHECK(Near(glm::normalize(glm::vec3(model[2])).z, 1.0f));                  // +Z do canvas aponta para a camera (+Z)
    // sem billboard devolve o transform da entidade intacto
    CHECK(CanvasModel(world, false, view) == world);

    // a camera enxerga o painel billboard (raio do ponto da camera ao centro dele acerta)
    glm::vec3 cam(0, 0, 10), target(3, 1, 0);
    glm::vec3 dir = glm::normalize(target - cam);
    CanvasHit hit = RayCanvasHit(cam, dir, model, 800, 600, 200);
    CHECK(hit.Hit && Near(hit.X, 400, 1.0f) && Near(hit.Y, 300, 1.0f));
}

static void TestScreenRay() {
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 inverse = glm::inverse(projection * view);

    glm::vec3 o, d;
    CHECK(ScreenRay(0, 0, inverse, o, d));
    CHECK(Near(d.x, 0) && Near(d.y, 0) && Near(d.z, -1));                       // centro da tela = para onde a camera olha
    CHECK(Near(o.x, 0) && Near(o.y, 0) && Near(o.z, 4.9f));                     // origem no plano proximo
    CHECK(Near(glm::length(d), 1.0f));

    CHECK(ScreenRay(1, 0, inverse, o, d));
    CHECK(d.x > 0.1f);                                                          // borda direita aponta para +X
    CHECK(ScreenRay(0, 1, inverse, o, d));
    CHECK(d.y > 0.1f);                                                          // topo aponta para +Y

    // ida e volta: o raio do centro acerta um canvas a 3 m na frente da camera, no pixel central
    ScreenRay(0, 0, inverse, o, d);
    CanvasHit hit = RayCanvasHit(o, d, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 2)), 800, 600, 200);
    CHECK(hit.Hit && Near(hit.X, 400) && Near(hit.Y, 300) && Near(hit.Distance, 2.9f, 1e-2f));

    CHECK(!ScreenRay(0, 0, glm::mat4(0.0f), o, d));                              // matriz degenerada
}

int main() {
    TestRayCanvas();
    TestBillboard();
    TestScreenRay();
    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
