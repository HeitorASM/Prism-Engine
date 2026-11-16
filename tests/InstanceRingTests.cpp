#include <Prism/Renderer/InstanceRing.h>
#include <cstdio>
#include <vector>
using namespace Prism;
static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU %s:%d %s\n", __FILE__, __LINE__, #c); g_failed++; } } while (0)

int main() {
    InstanceRing r;
    auto a = r.Allocate(112 * 10);
    CHECK(a.Orphan && a.Offset == 0 && a.Capacity == InstanceRing::kMinCapacity);   // primeira alocacao cria o buffer
    auto b = r.Allocate(112 * 10);
    CHECK(!b.Orphan && b.Offset == 1120);                                           // faixa seguinte, sem orfanar
    CHECK(b.Offset % InstanceRing::kAlignment == 0);
    auto c = r.Allocate(17);
    CHECK(!c.Orphan && c.Offset == 2240 && r.Head() == 2240 + 32);                  // alinhado a 16

    // Enche ate o fim: a faixa que nao cabe volta ao inicio e orfana
    InstanceRing w;
    w.Allocate(InstanceRing::kMinCapacity - 64);
    auto fit = w.Allocate(64);
    CHECK(!fit.Orphan && fit.Offset == InstanceRing::kMinCapacity - 64);
    auto wrap = w.Allocate(16);
    CHECK(wrap.Orphan && wrap.Offset == 0 && wrap.Capacity == InstanceRing::kMinCapacity);

    // Pedido maior que a capacidade cresce (potencia de 2) e orfana
    InstanceRing g;
    g.Allocate(100);
    auto big = g.Allocate(3 * InstanceRing::kMinCapacity);
    CHECK(big.Orphan && big.Offset == 0 && big.Capacity == 4 * InstanceRing::kMinCapacity);
    auto after = g.Allocate(100);
    CHECK(!after.Orphan && after.Offset == 3 * InstanceRing::kMinCapacity);

    // Invalidos
    CHECK(r.Allocate(0).Capacity == 0);
    CHECK(r.Allocate(InstanceRing::kMaxCapacity + 1).Capacity == 0);

    // Invariante: nenhuma faixa ultrapassa a capacidade nem se sobrepoe a anterior dentro do mesmo ciclo
    InstanceRing s;
    size_t lastEnd = 0;
    unsigned seed = 12345;
    for (int i = 0; i < 20000; i++) {
        seed = seed * 1664525u + 1013904223u;
        size_t bytes = 1 + (seed >> 8) % 200000;
        auto al = s.Allocate(bytes);
        if (al.Orphan) lastEnd = 0;
        CHECK(al.Offset >= lastEnd);
        CHECK(al.Offset + bytes <= al.Capacity);
        lastEnd = al.Offset + bytes;
    }
    std::printf(g_failed ? "FALHAS: %d\n" : "OK\n", g_failed);
    return g_failed ? 1 : 0;
}
