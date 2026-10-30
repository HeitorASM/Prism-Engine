// PhysicsLayersTests.cpp - camadas de fisica e matriz de colisao (Physics/PhysicsLayers.h, so std).
#include <Prism/Physics/PhysicsLayers.h>
#include <cstdio>

using namespace Prism::Physics;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    // ObjectLayer do Jolt: ida e volta
    for (int layer = 0; layer < kLayerCount; layer++)
        for (int moving = 0; moving < 2; moving++) {
            uint16_t object = MakeObjectLayer(layer, moving != 0);
            CHECK(UserLayerOf(object) == layer);
            CHECK(IsMovingLayer(object) == (moving != 0));
        }
    CHECK(MakeObjectLayer(31, true) == 63);   // cabe nas 64 camadas de objeto do Jolt
    CHECK(MakeObjectLayer(-5, false) == 0);   // fora do intervalo: preso
    CHECK(MakeObjectLayer(99, false) == (31 << 1));
    CHECK(LayerBit(0) == 1u && LayerBit(31) == 0x80000000u && LayerBit(32) == 0u && LayerBit(-1) == 0u);

    // padrao: tudo colide
    LayerMatrix m;
    CHECK(m.IsDefault());
    for (int a = 0; a < kLayerCount; a++) for (int b = 0; b < kLayerCount; b++) CHECK(m.ShouldCollide(a, b));
    CHECK(m.MaskOf(3) == kAllLayers);

    // desligar um par e simetrico e nao afeta o resto
    m.SetCollision(2, 5, false);
    CHECK(!m.ShouldCollide(2, 5) && !m.ShouldCollide(5, 2));
    CHECK(m.ShouldCollide(2, 2) && m.ShouldCollide(2, 4) && m.ShouldCollide(5, 6));
    CHECK(!m.IsDefault());
    CHECK((m.MaskOf(2) & LayerBit(5)) == 0u && (m.MaskOf(5) & LayerBit(2)) == 0u);
    m.SetCollision(5, 2, true);
    CHECK(m.ShouldCollide(2, 5) && m.IsDefault());

    // camada que nao colide consigo mesma
    m.SetCollision(7, 7, false);
    CHECK(!m.ShouldCollide(7, 7) && m.ShouldCollide(7, 8));

    // a matriz e a AND dos dois lados, mesmo se um lado foi gravado assimetrico (arquivo editado a mao)
    LayerMatrix n;
    n.SetRawMask(1, ~(1u << 4));          // so a camada 1 recusa a 4
    CHECK(!n.ShouldCollide(1, 4) && !n.ShouldCollide(4, 1));
    CHECK(n.ShouldCollide(4, 3));

    // fora do intervalo: nunca colide e nao quebra
    CHECK(!m.ShouldCollide(-1, 0) && !m.ShouldCollide(0, 32));
    m.SetCollision(40, 1, false);          // ignorado
    m.SetRawMask(-3, 0);
    CHECK(m.MaskOf(99) == 0u);

    std::printf("%d verificacoes, %d falha(s).\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
