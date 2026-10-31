#pragma once

// ============================================================================
// LightSelection.h
// Escolhe QUAIS luzes a camera usa neste quadro quando a cena tem mais do que o
// shader comporta (MAX_LIGHTS). So std + Culling.h: sem OpenGL, EnTT nem GLM -
// compila e e testado isoladamente (tests/LightSelectionTests.cpp).
//
// POR QUE EXISTE: antes, CollectGPULights ficava com as primeiras MAX_LIGHTS
// luzes na ordem do registro EnTT, sem olhar a camera. Numa cena com mais luzes
// que o limite, sumiam luzes PERTO da camera enquanto luzes distantes eram
// mantidas, e quem ganhava sombra (ShadowSystem pega as primeiras da lista) era
// igualmente arbitrario. Aqui a lista sai ORDENADA da mais para a menos
// relevante; o ShadowSystem, que percorre a lista em ordem, passa a dar sombra
// as luzes mais relevantes sem nenhuma mudanca nele.
//
// CRITERIOS (nesta ordem):
//   1. Directional entra sempre (nao tem posicao nem alcance).
//   2. Luz cuja esfera de alcance nao toca o frustum da camera e descartada:
//      a atenuacao do shader chega a ZERO em Range, entao ela nao iluminaria
//      nenhum pixel visivel. Luz sem alcance (Range <= 0) nunca e descartada.
//   3. Luz sem intensidade (<= 0 ou NaN) e descartada.
//   4. As demais sao ordenadas por  Score = Intensidade * R^2 / (R^2 + d^2),
//      onde d = distancia da camera ao centro da luz. Perto da camera ou com
//      alcance grande pontua mais; a curva e suave (sem degrau).
//   5. Luzes ja escolhidas no quadro anterior ganham um bonus (histerese), para
//      duas luzes de pontuacao parecida nao ficarem trocando de lugar a cada
//      pequeno movimento da camera (piscar).
//   6. Empate: menor Id primeiro, para o resultado ser deterministico.
// ============================================================================

#include "Culling.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Prism::LightSelection {

    struct Candidate {
        uint32_t Id = 0;                 // identidade estavel da luz (GPULight::SourceEntityId)
        int Type = 0;                    // 0 Point, 1 Spot, 2 Directional (igual a GPULight::Type)
        float Position[3] = { 0, 0, 0 };
        float Range = 0.0f;              // <= 0: sem limite de alcance
        float Intensity = 1.0f;          // Intensity * maior componente da cor
    };

    // Quanto uma luz ja escolhida no quadro anterior e favorecida (1.25 = +25%).
    constexpr float kHysteresisBonus = 1.25f;

    // Pontuacao de relevancia de uma luz Point/Spot para a camera (maior = mais relevante).
    inline float Score(const Candidate& light, const float cameraPosition[3]) {
        if (!(light.Intensity > 0.0f))
            return 0.0f; // zero, negativa ou NaN
        if (!(light.Range > 0.0f))
            return light.Intensity; // sem alcance: vale como se estivesse sobre a camera
        const float dx = light.Position[0] - cameraPosition[0];
        const float dy = light.Position[1] - cameraPosition[1];
        const float dz = light.Position[2] - cameraPosition[2];
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        const float rangeSquared = light.Range * light.Range;
        return light.Intensity * rangeSquared / (rangeSquared + distanceSquared);
    }

    // Devolve os INDICES (em 'lights') das luzes escolhidas, da mais para a menos
    // relevante, no maximo 'maxLights'. 'previousIds' = Ids escolhidos no quadro
    // anterior para ESTA camera, em ordem crescente (pode ser vazio).
    inline std::vector<uint32_t> Select(const std::vector<Candidate>& lights,
                                        const Culling::Frustum& frustum,
                                        const float cameraPosition[3],
                                        size_t maxLights,
                                        const std::vector<uint32_t>& previousIds) {
        struct Ranked {
            uint32_t Index;
            uint32_t Id;
            bool Directional;
            float Score;
        };
        std::vector<Ranked> ranked;
        ranked.reserve(lights.size());

        for (uint32_t i = 0; i < (uint32_t)lights.size(); ++i) {
            const Candidate& light = lights[i];
            Ranked entry{ i, light.Id, light.Type == 2, 0.0f };

            if (entry.Directional) {
                entry.Score = std::isfinite(light.Intensity) ? light.Intensity : 0.0f; // so desempata entre direcionais
            } else {
                if (!(light.Intensity > 0.0f))
                    continue;
                if (light.Range > 0.0f && !frustum.IntersectsSphere(light.Position, light.Range))
                    continue;
                entry.Score = Score(light, cameraPosition);
                if (std::binary_search(previousIds.begin(), previousIds.end(), light.Id))
                    entry.Score *= kHysteresisBonus;
            }
            ranked.push_back(entry);
        }

        std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
            if (a.Directional != b.Directional) return a.Directional;
            if (a.Score != b.Score) return a.Score > b.Score;
            return a.Id < b.Id;
        });

        if (ranked.size() > maxLights)
            ranked.resize(maxLights);

        std::vector<uint32_t> result;
        result.reserve(ranked.size());
        for (const Ranked& entry : ranked)
            result.push_back(entry.Index);
        return result;
    }

}
