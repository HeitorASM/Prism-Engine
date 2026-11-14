#pragma once

// ============================================================================
// ParticleBatcher.h
// Ordem de desenho GLOBAL das particulas de varios emissores + agrupamento em "runs"
// (sequencias consecutivas com o mesmo estado de desenho: blend, textura, flipbook).
// So std: testado sozinho (tests/ParticleBatcherTests.cpp).
//
// Por que global: antes cada emissor ordenava so as proprias particulas, entao a fumaca
// (Alpha) de um emissor nunca ficava corretamente atras/na frente das chamas de outro.
//
// Regras (BuildDrawOrder):
//  - Se NENHUM item pertence a um estado "dependente de ordem" (Alpha, SoftAdditive), a ordem
//    entre particulas nao muda o resultado (Aditivo e Multiplicar sao comutativos entre si dentro
//    do mesmo modo): agrupa por estado, SEM ordenar por profundidade -> poucos draws, sem custo de sort.
//  - Caso contrario: UMA ordenacao de tras para frente (maior distancia primeiro) de todas as
//    particulas; runs = trechos consecutivos de mesmo estado. Empates desfeitos pelo indice de origem
//    (resultado deterministico, sem cintilar entre frames).
// ============================================================================

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Prism {

    struct ParticleSortItem {
        float Depth = 0.0f;     // distancia^2 ate a camera
        uint32_t State = 0;     // indice do estado de desenho (definido pelo chamador)
        uint32_t Source = 0;    // indice da particula na lista do chamador
    };

    struct ParticleRun {
        uint32_t State = 0;
        uint32_t First = 0;     // posicao em 'items' (apos a ordenacao)
        uint32_t Count = 0;
    };

    namespace ParticleBatcher {

        // 'orderDependent[state]' = true se o blend daquele estado depende da ordem.
        // Reordena 'items' no lugar e preenche 'runs' (limpa antes). Devolve true se ordenou por profundidade.
        inline bool BuildDrawOrder(std::vector<ParticleSortItem>& items, const std::vector<uint8_t>& orderDependent, std::vector<ParticleRun>& runs) {
            runs.clear();
            if (items.empty())
                return false;

            bool anyDependent = false;
            for (const ParticleSortItem& item : items)
                if (item.State < orderDependent.size() && orderDependent[item.State]) { anyDependent = true; break; }

            if (anyDependent) {
                std::sort(items.begin(), items.end(), [](const ParticleSortItem& a, const ParticleSortItem& b) {
                    if (a.Depth != b.Depth) return a.Depth > b.Depth;
                    return a.Source < b.Source;
                });
            }
            else {
                std::sort(items.begin(), items.end(), [](const ParticleSortItem& a, const ParticleSortItem& b) {
                    if (a.State != b.State) return a.State < b.State;
                    return a.Source < b.Source;
                });
            }

            ParticleRun current{ items[0].State, 0, 1 };
            for (uint32_t i = 1; i < (uint32_t)items.size(); i++) {
                if (items[i].State == current.State) { current.Count++; continue; }
                runs.push_back(current);
                current = ParticleRun{ items[i].State, i, 1 };
            }
            runs.push_back(current);
            return anyDependent;
        }
    }

}
