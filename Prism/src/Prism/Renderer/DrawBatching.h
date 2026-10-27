#pragma once

// ============================================================================
// DrawBatching.h
// Agrupa os objetos visiveis do passe principal em LOTES, so com a std (testavel
// sem GPU). Objetos com a mesma malha e o mesmo material viram UM desenho
// instanciado; o resto desenha um a um. A ordem dos lotes minimiza trocas de
// material (o estado mais caro), depois de malha.
//
//   std::vector<Batching::Item> items = ...;   // um por objeto visivel
//   auto batches = Batching::Build(items, /*allowInstancing=*/true, /*maxInstances=*/1024);
//   for (auto& b : batches)
//       for (size_t i = 0; i < b.Count; i++) use(items[b.First + i]);   // 'items' foi REORDENADO
//
// Determinismo: a ordenacao e estavel e desempata pelo indice original, entao o
// mesmo conjunto de objetos sempre gera os mesmos lotes na mesma ordem.
// ============================================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Prism::Batching {

    struct Item {
        const void* Mesh = nullptr;   // identidade da malha (ponteiro)
        uint32_t MaterialId = 0;      // mesmo id = mesmo estado de material (texturas/fatores)
        bool Skinned = false;         // malha animada: nunca instancia (cada uma tem ossos proprios)
        uint32_t Index = 0;           // indice do objeto na lista original (dados do chamador)
    };

    struct Batch {
        size_t First = 0;             // posicao em 'items' (ja reordenado)
        size_t Count = 0;
        bool Instanced = false;       // true: desenhar os 'Count' objetos com UMA chamada
    };

    inline std::vector<Batch> Build(std::vector<Item>& items, bool allowInstancing, uint32_t maxInstances) {
        std::vector<Batch> batches;
        if (items.empty())
            return batches;
        if (maxInstances < 2)
            allowInstancing = false;

        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.MaterialId != b.MaterialId) return a.MaterialId < b.MaterialId;
            if (a.Mesh != b.Mesh) return std::less<const void*>{}(a.Mesh, b.Mesh);
            if (a.Skinned != b.Skinned) return !a.Skinned; // nao-animadas primeiro
            return a.Index < b.Index;
        });

        size_t i = 0;
        while (i < items.size()) {
            // corrida de itens iguais em (material, malha, animada?)
            size_t j = i + 1;
            while (j < items.size()
                && items[j].MaterialId == items[i].MaterialId
                && items[j].Mesh == items[i].Mesh
                && items[j].Skinned == items[i].Skinned)
                j++;

            if (allowInstancing && !items[i].Skinned && j - i >= 2) {
                for (size_t first = i; first < j; first += maxInstances) {
                    const size_t count = std::min<size_t>(maxInstances, j - first);
                    batches.push_back({ first, count, count >= 2 });
                }
            } else {
                for (size_t k = i; k < j; k++)
                    batches.push_back({ k, 1, false });
            }
            i = j;
        }
        return batches;
    }

}
