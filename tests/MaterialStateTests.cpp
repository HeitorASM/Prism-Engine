// MaterialStateTests.cpp - identidade do estado de material (Renderer/MaterialState.h).
// Depende so de Components.h (glm + EnTT), sem GPU.
#include <Prism/Renderer/MaterialState.h>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace Prism;
using namespace Prism::MaterialState;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    MaterialComponent a;
    a.AlbedoPath = "Textures/pedra.png";
    MaterialComponent b = a;

    // iguais -> mesmo hash e Equal
    CHECK(Hash(&a) == Hash(&b));
    CHECK(Equal(&a, &b));

    // AlbedoTint e LinkedAsset NAO entram no estado (sao por objeto)
    b.AlbedoTint = { 0.1f, 0.2f, 0.3f };
    CHECK(Equal(&a, &b) && Hash(&a) == Hash(&b));

    // sem material e um estado proprio
    CHECK(Equal(nullptr, nullptr));
    CHECK(!Equal(&a, nullptr) && !Equal(nullptr, &a));
    CHECK(Hash(nullptr) != Hash(&a));

    // cada campo que importa muda o estado
    auto differs = [&](auto mutate) { MaterialComponent c = a; mutate(c); return !Equal(&a, &c); };
    CHECK(differs([](MaterialComponent& c) { c.AlbedoPath = "x.png"; }));
    CHECK(differs([](MaterialComponent& c) { c.NormalPath = "n.png"; }));
    CHECK(differs([](MaterialComponent& c) { c.RoughnessMetallicPath = "rm.png"; }));
    CHECK(differs([](MaterialComponent& c) { c.RoughnessFactor = 0.9f; }));
    CHECK(differs([](MaterialComponent& c) { c.MetallicFactor = 0.9f; }));
    CHECK(differs([](MaterialComponent& c) { c.BlendMode = MaterialBlendMode::Mask; }));
    CHECK(differs([](MaterialComponent& c) { c.Opacity = 0.5f; }));
    CHECK(differs([](MaterialComponent& c) { c.AlphaCutoff = 0.1f; }));
    CHECK(differs([](MaterialComponent& c) { c.UVTiling = { 2.0f, 1.0f }; }));
    CHECK(differs([](MaterialComponent& c) { c.UVOffset = { 0.0f, 0.5f }; }));
    CHECK(differs([](MaterialComponent& c) { c.EmissiveColor = { 1.0f, 0.0f, 0.0f }; }));
    CHECK(differs([](MaterialComponent& c) { c.EmissiveIntensity = 3.0f; }));
    CHECK(differs([](MaterialComponent& c) { c.DoubleSided = true; }));
    CHECK(differs([](MaterialComponent& c) { c.ShaderPath = "Shaders/agua.glsl"; }));
    CHECK(differs([](MaterialComponent& c) { c.ShaderParams.push_back({ "u_Speed", glm::vec4(1.0f) }); }));

    // hash tambem separa o que Equal separa (nenhum desses pares colide)
    {
        MaterialComponent c = a; c.RoughnessFactor = 0.9f;
        CHECK(Hash(&a) != Hash(&c));
        MaterialComponent d = a; d.ShaderParams.push_back({ "u_A", glm::vec4(1, 2, 3, 4) });
        MaterialComponent e = a; e.ShaderParams.push_back({ "u_A", glm::vec4(1, 2, 3, 5) });
        CHECK(Hash(&d) != Hash(&e) && !Equal(&d, &e));
    }

    // fronteira entre campos de texto: "ab"+"c" != "a"+"bc"
    {
        MaterialComponent c, d;
        c.AlbedoPath = "ab"; c.NormalPath = "c";
        d.AlbedoPath = "a";  d.NormalPath = "bc";
        CHECK(!Equal(&c, &d) && Hash(&c) != Hash(&d));
    }

    // roughness/metallic fora de [0,1] sao clampados (igual ao BindMaterialState)
    {
        MaterialComponent c = a, d = a;
        c.RoughnessFactor = 1.0f; d.RoughnessFactor = 7.0f;
        c.MetallicFactor = 0.0f;  d.MetallicFactor = -3.0f;
        CHECK(Equal(&c, &d) && Hash(&c) == Hash(&d));
    }

    // NaN nao quebra: o mesmo NaN e igual a si mesmo (compara bits), nao gera um id novo a cada objeto
    {
        MaterialComponent c = a, d = a;
        c.Opacity = d.Opacity = std::numeric_limits<float>::quiet_NaN();
        CHECK(Equal(&c, &d) && Hash(&c) == Hash(&d));
    }

    // IdTable: ids sequenciais na ordem de aparicao, iguais compartilham id
    {
        IdTable table;
        MaterialComponent m1 = a, m2 = a, m3 = a;
        m2.AlbedoPath = "outra.png";
        m3.ShaderParams.push_back({ "u_X", glm::vec4(1.0f) });
        MaterialComponent m1copy = m1; m1copy.AlbedoTint = { 0, 0, 0 };

        CHECK(table.GetOrAdd(nullptr) == 0);
        CHECK(table.GetOrAdd(&m1) == 1);
        CHECK(table.GetOrAdd(&m2) == 2);
        CHECK(table.GetOrAdd(&m1copy) == 1);
        CHECK(table.GetOrAdd(&m3) == 3);
        CHECK(table.GetOrAdd(nullptr) == 0);
        CHECK(table.GetOrAdd(&m2) == 2);
        CHECK(table.Count() == 4);

        table.Clear();
        CHECK(table.Count() == 0);
        CHECK(table.GetOrAdd(&m2) == 0); // recomeca do zero a cada quadro
    }

    // muitos materiais diferentes: nenhum e confundido com outro
    {
        IdTable table;
        std::vector<MaterialComponent> many(500);
        for (size_t i = 0; i < many.size(); i++) many[i].AlbedoPath = "t" + std::to_string(i % 250) + ".png"; // 250 distintos, cada um 2x
        for (size_t i = 0; i < many.size(); i++) table.GetOrAdd(&many[i]);
        CHECK(table.Count() == 250);
        CHECK(table.GetOrAdd(&many[3]) == table.GetOrAdd(&many[253]));
    }

    std::printf("MaterialStateTests: %d verificacoes, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
