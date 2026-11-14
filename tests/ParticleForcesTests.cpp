#include "Prism/Particles/ParticleForces.h"
#include <cstdio>
using namespace Prism::ParticleForces;
static int g_n = 0, g_fail = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FALHA linha %d: %s\n", __LINE__, #c); } } while (0)
int main() {
    // determinismo e faixa
    double sum = 0; float mn = 1e9f, mx = -1e9f; const int N = 20000;
    for (int i = 0; i < N; i++) {
        float x = i * 0.37f, y = i * 0.11f - 40.f, z = i * 0.53f;
        float a = Noise3(x, y, z, 5), b = Noise3(x, y, z, 5);
        CHECK(a == b && a >= -1.0f && a <= 1.0f);
        sum += a; mn = std::min(mn, a); mx = std::max(mx, a);
    }
    CHECK(std::fabs(sum / N) < 0.05);       // media ~0
    CHECK(mn < -0.5f && mx > 0.5f);         // usa boa parte da faixa
    // continuidade: passos pequenos => variacao pequena (cruzando celulas, incl. negativos)
    for (int i = 0; i < 4000; i++) {
        float x = -20.f + i * 0.01f;
        CHECK(std::fabs(Noise3(x + 0.001f, 1.3f, -2.7f) - Noise3(x, 1.3f, -2.7f)) < 0.02f);
    }
    // campo: componentes distintas (descorrelacionadas) e suave no tempo
    glm::vec3 f0 = TurbulenceField({1.f, 2.f, 3.f}, 0.f, 0.7f);
    CHECK(f0.x != f0.y || f0.y != f0.z);
    glm::vec3 f1 = TurbulenceField({1.f, 2.f, 3.f}, 0.001f, 0.7f);
    CHECK(glm::length(f1 - f0) < 0.05f);
    CHECK(TurbulenceField({0,0,0}, 0, 1) == TurbulenceField({0,0,0}, 0, 1));
    // colisao: sem impacto acima do chao
    { glm::vec3 p{0, 1, 0}, v{1, -2, 0}; CHECK(!CollideGround(p, v, 0, 0.1f, 0.5f, 0.2f) && v.y == -2.f); }
    // impacto: quica com restituicao e perde atrito horizontal
    { glm::vec3 p{0, 0.05f, 0}, v{2, -4, 1}; CHECK(CollideGround(p, v, 0, 0.1f, 0.5f, 0.25f));
      CHECK(std::fabs(p.y - 0.1f) < 1e-6f && std::fabs(v.y - 2.0f) < 1e-6f && std::fabs(v.x - 1.5f) < 1e-6f && std::fabs(v.z - 0.75f) < 1e-6f); }
    // repouso: velocidade pequena zera; subindo nao inverte
    { glm::vec3 p{0, 0.0f, 0}, v{0, -0.2f, 0}; CollideGround(p, v, 0, 0.1f, 0.5f, 0); CHECK(v.y == 0.f); }
    { glm::vec3 p{0, 0.0f, 0}, v{0, 3.f, 0}; CollideGround(p, v, 0, 0.1f, 0.5f, 0); CHECK(v.y == 3.f && p.y == 0.1f); }
    // simula uma bola caindo: nunca atravessa o chao e termina em repouso
    { glm::vec3 p{0, 3, 0}, v{0, 0, 0}; bool ok = true;
      for (int i = 0; i < 2000; i++) { v.y -= 9.81f * 0.016f; p += v * 0.016f; CollideGround(p, v, 0, 0.1f, 0.6f, 0.1f); if (p.y < 0.1f - 1e-5f) ok = false; }
      CHECK(ok && std::fabs(p.y - 0.1f) < 0.02f && std::fabs(v.y) < 0.2f); }
    std::printf("%d checks, %d falhas\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
