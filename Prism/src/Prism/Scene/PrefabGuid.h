#pragma once

// ============================================================================
// PrefabGuid.h
// Identidade ESTAVEL das entidades dentro de um .prismprefab (so std; testado em
// tests/PrefabGuidTests.cpp).
//
// Antes, a instancia ligava cada entidade ao arquivo pelo INDICE posicional (a n-esima
// entidade da subarvore). Inserir, remover ou reordenar um filho no prefab fazia todos os
// indices depois dele apontarem para a entidade ERRADA (o sync aplicava o component de uma
// entidade em outra). Agora cada entidade do arquivo tem um GUID de 64 bits que nunca muda
// enquanto ela existir: a instancia guarda o GUID e o sync procura por ele.
//
// MIGRACAO (arquivos e cenas antigos, sem GUID): o GUID "de migracao" e uma funcao fixa do
// indice. Um prefab v9 carrega com Migration(i) para a entidade i, e um membro de instancia
// antigo (Guid = 0) usa Migration(IndexInPrefab): os dois concordam sem reescrever nenhum
// arquivo. Ao salvar o prefab (v10), esses GUIDs passam a ficar gravados.
// ============================================================================

#include <cstdint>
#include <random>
#include <unordered_set>
#include <vector>

namespace Prism::PrefabGuid {

    // 0 = "sem GUID". Nunca devolvido por Migration/Generate.
    constexpr uint64_t kNone = 0;

    inline uint64_t Mix(uint64_t x) { // splitmix64
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    // GUID de migracao da entidade de indice 'index' (funcao fixa).
    inline uint64_t Migration(uint32_t index) {
        uint64_t g = Mix(0x50524953u /* "PRIS" */ ^ ((uint64_t)index << 32 | index));
        return g ? g : 1;
    }

    // GUID aleatorio novo (nao zero).
    inline uint64_t Generate() {
        static std::mt19937_64 rng{ std::random_device{}() };
        uint64_t g = 0;
        while (g == 0) g = rng();
        return g;
    }

    // GUID efetivo de um membro de instancia: o gravado, ou o de migracao se ele e antigo (0).
    inline uint64_t Effective(uint64_t stored, uint32_t indexInPrefab) {
        return stored != kNone ? stored : Migration(indexInPrefab);
    }

    // Garante que a lista tenha so GUIDs nao-zero e distintos: zeros e repeticoes (a partir da 2a) viram novos.
    // Devolve quantos foram trocados.
    inline size_t MakeUnique(std::vector<uint64_t>& guids) {
        std::unordered_set<uint64_t> seen;
        size_t replaced = 0;
        for (uint64_t& g : guids) {
            if (g == kNone || !seen.insert(g).second) {
                do { g = Generate(); } while (!seen.insert(g).second);
                replaced++;
            }
        }
        return replaced;
    }

    // Indice do GUID na lista, ou UINT32_MAX.
    inline uint32_t IndexOf(const std::vector<uint64_t>& guids, uint64_t guid) {
        for (uint32_t i = 0; i < (uint32_t)guids.size(); i++)
            if (guids[i] == guid) return i;
        return UINT32_MAX;
    }
}
