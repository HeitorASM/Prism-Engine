#include "Prism/Scene/PrefabGuid.h"
#include <cstdio>
#include <set>
using namespace Prism::PrefabGuid;
static int g_n = 0, g_fail = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FALHA linha %d: %s\n", __LINE__, #c); } } while (0)
int main() {
    // Migration: fixa, nao-zero, sem colisao em 100k indices
    std::set<uint64_t> all;
    for (uint32_t i = 0; i < 100000; i++) { uint64_t g = Migration(i); CHECK(g != kNone && g == Migration(i)); all.insert(g); }
    CHECK(all.size() == 100000);
    // Effective: antigo (0) usa migracao do indice; gravado vence
    CHECK(Effective(0, 7) == Migration(7) && Effective(123, 7) == 123);
    // Generate: nao-zero e variado
    std::set<uint64_t> gen; for (int i = 0; i < 1000; i++) { uint64_t g = Generate(); CHECK(g != kNone); gen.insert(g); }
    CHECK(gen.size() == 1000);
    // MakeUnique: preserva os bons, troca zeros e repeticoes
    { std::vector<uint64_t> v = { 5, 0, 5, 9, 0 };
      CHECK(MakeUnique(v) == 3);
      CHECK(v[0] == 5 && v[3] == 9);
      std::set<uint64_t> s(v.begin(), v.end()); CHECK(s.size() == 5 && !s.count(0)); }
    { std::vector<uint64_t> v = { 1, 2, 3 }; CHECK(MakeUnique(v) == 0 && v[0] == 1 && v[2] == 3); }
    // Cenario real: reordenar/inserir no prefab nao desloca a identidade
    { std::vector<uint64_t> file = { Migration(0), Migration(1), Migration(2) };   // prefab antigo migrado
      uint64_t memberGuid = Effective(0, 2);                                        // instancia antiga apontava para o indice 2
      CHECK(IndexOf(file, memberGuid) == 2);
      std::vector<uint64_t> edited = { file[0], Generate(), file[1], file[2] };     // filho novo inserido no meio
      CHECK(IndexOf(edited, memberGuid) == 3);                                      // continua achando a MESMA entidade
      edited.erase(edited.begin() + 2);                                             // outro filho removido
      CHECK(IndexOf(edited, memberGuid) == 2 && IndexOf(edited, file[1]) == UINT32_MAX); }
    std::printf("%d checks, %d falhas\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
