#include "Prism/Particles/ParticleBatcher.h"
#include <cstdio>
#include <random>
using namespace Prism;
static int g_n = 0, g_fail = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FALHA linha %d: %s\n", __LINE__, #c); } } while (0)

int main() {
    std::vector<ParticleRun> runs;
    { // vazio
        std::vector<ParticleSortItem> items;
        CHECK(!ParticleBatcher::BuildDrawOrder(items, {}, runs) && runs.empty());
    }
    { // so estados comutativos: agrupa por estado, sem ordenar por profundidade; Source crescente dentro do estado
        std::vector<ParticleSortItem> items = { {5,1,0}, {9,0,1}, {1,1,2}, {3,0,3} };
        std::vector<uint8_t> dep = { 0, 0 };
        CHECK(!ParticleBatcher::BuildDrawOrder(items, dep, runs));
        CHECK(runs.size() == 2 && runs[0].State == 0 && runs[0].Count == 2 && runs[1].State == 1 && runs[1].Count == 2);
        CHECK(items[0].Source == 1 && items[1].Source == 3 && items[2].Source == 0 && items[3].Source == 2);
    }
    { // com estado dependente: ordem global de tras para frente; runs seguem as trocas de estado
        std::vector<ParticleSortItem> items = { {1,0,0}, {9,1,1}, {5,0,2}, {7,1,3}, {3,0,4} };
        std::vector<uint8_t> dep = { 0, 1 };
        CHECK(ParticleBatcher::BuildDrawOrder(items, dep, runs));
        const uint32_t expected[] = { 1, 3, 2, 4, 0 };
        for (int i = 0; i < 5; i++) CHECK(items[i].Source == expected[i]);
        // estados em ordem: 1,1,0,0,0 -> 2 runs
        CHECK(runs.size() == 2 && runs[0].State == 1 && runs[0].Count == 2 && runs[1].State == 0 && runs[1].Count == 3 && runs[1].First == 2);
    }
    { // empate: desempata por Source (deterministico)
        std::vector<ParticleSortItem> items = { {2,0,7}, {2,0,3}, {2,0,5} };
        ParticleBatcher::BuildDrawOrder(items, { 1 }, runs);
        CHECK(items[0].Source == 3 && items[1].Source == 5 && items[2].Source == 7 && runs.size() == 1 && runs[0].Count == 3);
    }
    { // aleatorio: runs cobrem tudo exatamente uma vez, sem buracos; ordem de profundidade nao-crescente quando ordenado
        std::mt19937 rng(42);
        for (int round = 0; round < 200; round++) {
            const int n = 1 + (int)(rng() % 300);
            std::vector<ParticleSortItem> items(n);
            for (int i = 0; i < n; i++) items[i] = { (float)(rng() % 1000), (uint32_t)(rng() % 4), (uint32_t)i };
            std::vector<uint8_t> dep(4);
            for (auto& d : dep) d = (uint8_t)(rng() % 2);
            const bool sorted = ParticleBatcher::BuildDrawOrder(items, dep, runs);
            uint32_t covered = 0; bool contiguous = true;
            for (const ParticleRun& r : runs) { if (r.First != covered) contiguous = false; covered += r.Count; for (uint32_t k = 0; k < r.Count; k++) if (items[r.First + k].State != r.State) contiguous = false; }
            CHECK(contiguous && covered == (uint32_t)n);
            std::vector<int> seen(n, 0);
            for (auto& it : items) seen[it.Source]++;
            bool perm = true; for (int c : seen) if (c != 1) perm = false;
            CHECK(perm);
            if (sorted) { bool ok = true; for (int i = 1; i < n; i++) if (items[i].Depth > items[i-1].Depth) ok = false; CHECK(ok); }
        }
    }
    std::printf("%d checks, %d falhas\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
