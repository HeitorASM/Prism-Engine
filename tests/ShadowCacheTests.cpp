// ShadowCacheTests.cpp - assinatura e cache das camadas de sombra (Renderer/ShadowCache.h, so std).
#include <Prism/Renderer/ShadowCache.h>

#include <cstdio>

using namespace Prism::ShadowCache;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void Identity(float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

int main() {
    float a[16], b[16], light[16];
    Identity(a); Identity(b); Identity(light);
    b[12] = 3.0f;           // b = a deslocado em X
    light[14] = -5.0f;

    const uint64_t lightHash = HashFloats(light, 16);

    // --- hash de floats: deterministico e sensivel a qualquer bit ---
    CHECK(HashFloats(a, 16) == HashFloats(a, 16));
    CHECK(HashFloats(a, 16) != HashFloats(b, 16));
    float tiny[16]; Identity(tiny); tiny[12] = 1e-7f;
    CHECK(HashFloats(a, 16) != HashFloats(tiny, 16));

    // --- assinatura: mesmos casters (em qualquer ordem) = mesma assinatura ---
    Signature one, two;
    one.AddCaster(10, a); one.AddCaster(20, b);
    two.AddCaster(20, b); two.AddCaster(10, a);
    CHECK(one.Finish(lightHash) == two.Finish(lightHash));
    CHECK(!one.Dynamic);

    // --- qualquer mudanca relevante muda a assinatura ---
    Signature moved;      moved.AddCaster(10, b); moved.AddCaster(20, b);
    Signature otherMesh;  otherMesh.AddCaster(11, a); otherMesh.AddCaster(20, b);
    Signature fewer;      fewer.AddCaster(10, a);
    Signature duplicate;  duplicate.AddCaster(10, a); duplicate.AddCaster(20, b); duplicate.AddCaster(20, b);
    Signature none;
    CHECK(moved.Finish(lightHash) != one.Finish(lightHash));       // um objeto andou
    CHECK(otherMesh.Finish(lightHash) != one.Finish(lightHash));   // outra malha no lugar
    CHECK(fewer.Finish(lightHash) != one.Finish(lightHash));       // um objeto saiu do volume
    CHECK(duplicate.Finish(lightHash) != one.Finish(lightHash));   // um objeto a mais
    CHECK(none.Finish(lightHash) != one.Finish(lightHash));
    CHECK(one.Finish(lightHash) != one.Finish(HashFloats(a, 16))); // a luz mexeu

    // --- caster animado marca a camada como dinamica ---
    Signature animated = one;
    animated.MarkDynamic();
    CHECK(animated.Dynamic);

    // --- cache de camadas ---
    LayerCache cache(4);
    const uint64_t signature = one.Finish(lightHash);
    CHECK(!cache.Matches(0, signature));            // nada desenhado ainda
    cache.Store(0, signature);
    CHECK(cache.Matches(0, signature));
    CHECK(!cache.Matches(1, signature));            // outra camada
    CHECK(!cache.Matches(0, moved.Finish(lightHash)));
    cache.Store(1, signature);
    cache.Invalidate(0);
    CHECK(!cache.Matches(0, signature) && cache.Matches(1, signature));
    cache.InvalidateAll();
    CHECK(!cache.Matches(1, signature));

    // --- indices fora do intervalo nao quebram ---
    cache.Store(99, signature);
    CHECK(!cache.Matches(99, signature));
    cache.Invalidate(99);
    LayerCache empty;
    CHECK(!empty.Matches(0, signature));

    std::printf("%d verificacoes, %d falha(s).\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
