// DrawBatchingTests.cpp - agrupamento de desenhos (Renderer/DrawBatching.h, so std).
#include <Prism/Renderer/DrawBatching.h>
#include <algorithm>
#include <cstdio>
#include <set>

using namespace Prism::Batching;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int A, B, C; // identidades de malha
static Item Make(const void* mesh, uint32_t material, uint32_t index, bool skinned = false) {
    Item i; i.Mesh = mesh; i.MaterialId = material; i.Index = index; i.Skinned = skinned; return i;
}

// Todo item aparece em exatamente um lote, e lotes cobrem 'items' sem buracos.
static void CheckCoverage(const std::vector<Item>& items, const std::vector<Batch>& batches, size_t originalCount) {
    CHECK(items.size() == originalCount);
    size_t next = 0;
    std::set<uint32_t> seen;
    for (const Batch& b : batches) {
        CHECK(b.First == next);
        CHECK(b.Count >= 1);
        if (b.Instanced) CHECK(b.Count >= 2);
        next += b.Count;
        for (size_t k = 0; k < b.Count; k++) {
            CHECK(seen.insert(items[b.First + k].Index).second);
            // dentro de um lote: tudo igual
            CHECK(items[b.First + k].Mesh == items[b.First].Mesh);
            CHECK(items[b.First + k].MaterialId == items[b.First].MaterialId);
            CHECK(items[b.First + k].Skinned == items[b.First].Skinned);
        }
    }
    CHECK(next == items.size());
}

int main() {
    // vazio
    { std::vector<Item> v; CHECK(Build(v, true, 16).empty()); }

    // um item: desenho normal
    { std::vector<Item> v{ Make(&A, 0, 0) };
      auto b = Build(v, true, 16);
      CHECK(b.size() == 1 && !b[0].Instanced && b[0].Count == 1); }

    // 5 iguais viram 1 lote instanciado
    { std::vector<Item> v; for (uint32_t i = 0; i < 5; i++) v.push_back(Make(&A, 1, i));
      auto b = Build(v, true, 16);
      CHECK(b.size() == 1 && b[0].Instanced && b[0].Count == 5);
      CheckCoverage(v, b, 5); }

    // instancing desligado: 5 desenhos, nenhum instanciado
    { std::vector<Item> v; for (uint32_t i = 0; i < 5; i++) v.push_back(Make(&A, 1, i));
      auto b = Build(v, false, 16);
      CHECK(b.size() == 5);
      for (auto& x : b) CHECK(!x.Instanced && x.Count == 1);
      CheckCoverage(v, b, 5); }

    // malhas ou materiais diferentes nao se misturam
    { std::vector<Item> v{ Make(&A,0,0), Make(&B,0,1), Make(&A,1,2), Make(&A,0,3), Make(&B,0,4), Make(&A,1,5) };
      auto b = Build(v, true, 16);
      CHECK(b.size() == 3);                      // (A,0)x2  (B,0)x2  (A,1)x2
      for (auto& x : b) CHECK(x.Instanced && x.Count == 2);
      CheckCoverage(v, b, 6); }

    // ordem: por material primeiro (menos trocas de material)
    { std::vector<Item> v{ Make(&A,2,0), Make(&B,1,1), Make(&A,1,2), Make(&B,2,3) };
      auto b = Build(v, true, 16);
      for (size_t i = 1; i < v.size(); i++) CHECK(v[i-1].MaterialId <= v[i].MaterialId);
      CheckCoverage(v, b, 4); }

    // malhas animadas nunca instanciam, mesmo iguais
    { std::vector<Item> v{ Make(&A,0,0,true), Make(&A,0,1,true), Make(&A,0,2,true) };
      auto b = Build(v, true, 16);
      CHECK(b.size() == 3);
      for (auto& x : b) CHECK(!x.Instanced);
      CheckCoverage(v, b, 3); }

    // animada e nao animada da mesma malha/material ficam em lotes separados
    { std::vector<Item> v{ Make(&A,0,0), Make(&A,0,1,true), Make(&A,0,2), Make(&A,0,3,true) };
      auto b = Build(v, true, 16);
      CHECK(b.size() == 3);                      // 1 instanciado (2 estaticas) + 2 animadas soltas
      CHECK(b[0].Instanced && b[0].Count == 2 && !v[b[0].First].Skinned);
      CheckCoverage(v, b, 4); }

    // limite de instancias por lote: 10 itens, max 4 => 4 + 4 + 2
    { std::vector<Item> v; for (uint32_t i = 0; i < 10; i++) v.push_back(Make(&A, 0, i));
      auto b = Build(v, true, 4);
      CHECK(b.size() == 3 && b[0].Count == 4 && b[1].Count == 4 && b[2].Count == 2);
      for (auto& x : b) CHECK(x.Instanced);
      CheckCoverage(v, b, 10); }

    // sobra de 1 no ultimo pedaco vira desenho normal: 9 itens, max 4 => 4 + 4 + 1
    { std::vector<Item> v; for (uint32_t i = 0; i < 9; i++) v.push_back(Make(&A, 0, i));
      auto b = Build(v, true, 4);
      CHECK(b.size() == 3 && b[2].Count == 1 && !b[2].Instanced);
      CheckCoverage(v, b, 9); }

    // maxInstances < 2 equivale a desligado
    { std::vector<Item> v; for (uint32_t i = 0; i < 4; i++) v.push_back(Make(&A, 0, i));
      auto b = Build(v, true, 1);
      CHECK(b.size() == 4);
      CheckCoverage(v, b, 4); }

    // determinismo: a ordem de entrada nao muda o resultado (desempate pelo indice)
    { std::vector<Item> v1{ Make(&A,0,0), Make(&A,0,1), Make(&B,0,2), Make(&A,1,3) };
      std::vector<Item> v2{ Make(&A,1,3), Make(&B,0,2), Make(&A,0,1), Make(&A,0,0) };
      auto b1 = Build(v1, true, 8), b2 = Build(v2, true, 8);
      CHECK(b1.size() == b2.size());
      for (size_t i = 0; i < v1.size(); i++) CHECK(v1[i].Index == v2[i].Index);
      for (size_t i = 0; i < b1.size() && i < b2.size(); i++)
          CHECK(b1[i].First == b2[i].First && b1[i].Count == b2[i].Count && b1[i].Instanced == b2[i].Instanced); }

    // carga grande: 10 000 objetos, 3 malhas x 4 materiais => 12 grupos, poucos lotes
    { std::vector<Item> v; int* meshes[3] = { &A, &B, &C };
      for (uint32_t i = 0; i < 10000; i++) v.push_back(Make(meshes[i % 3], (i / 3) % 4, i));
      auto b = Build(v, true, 1024);
      CHECK(b.size() <= 12 * 4);                 // 10000/12 ~ 833 por grupo: 1 lote cada
      CHECK(b.size() >= 12);
      CheckCoverage(v, b, 10000); }

    // GroupSorted: ordena UMA vez e cada passe (sombras) agrupa so um subconjunto - igual a Build do subconjunto
    { std::vector<Item> all;
      int* meshes[3] = { &A, &B, &C };
      for (uint32_t i = 0; i < 300; i++) all.push_back(Make(meshes[i % 3], 0, i, i % 7 == 0));
      std::sort(all.begin(), all.end(), ItemLess);
      std::vector<Item> subset;                                   // "visiveis para a luz": 2 de cada 3, na mesma ordem
      for (size_t i = 0; i < all.size(); i++) if (i % 3 != 0) subset.push_back(all[i]);

      std::vector<Item> viaBuild = subset;
      auto expected = Build(viaBuild, true, 16);                  // ordena de novo
      auto grouped = GroupSorted(subset, true, 16);               // usa a ordem que ja tem
      CHECK(grouped.size() == expected.size());
      for (size_t i = 0; i < grouped.size() && i < expected.size(); i++)
          CHECK(grouped[i].First == expected[i].First && grouped[i].Count == expected[i].Count && grouped[i].Instanced == expected[i].Instanced);
      for (size_t i = 0; i < subset.size(); i++) CHECK(subset[i].Index == viaBuild[i].Index);
      CheckCoverage(subset, grouped, subset.size());
      CHECK(GroupSorted(std::vector<Item>{}, true, 16).empty()); }

    // GroupSorted respeita o limite por lote e o desligamento do instancing
    { std::vector<Item> v; for (uint32_t i = 0; i < 10; i++) v.push_back(Make(&A, 0, i));
      auto b = GroupSorted(v, true, 4);                           // 4 + 4 + 2
      CHECK(b.size() == 3 && b[0].Count == 4 && b[1].Count == 4 && b[2].Count == 2);
      for (auto& x : b) CHECK(x.Instanced);
      auto off = GroupSorted(v, false, 4);
      CHECK(off.size() == 10);
      for (auto& x : off) CHECK(!x.Instanced); }

    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
