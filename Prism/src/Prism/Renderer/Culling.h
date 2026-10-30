#pragma once

// ============================================================================
// Culling.h
// Frustum culling e camadas de visibilidade (mascara de bits), so std: sem
// OpenGL, EnTT nem GLM - compila e e testado isoladamente (tests/CullingTests.cpp).
//
// Matrizes sao float[16] em ordem de COLUNA (a mesma de glm::value_ptr).
//
// CAMADAS (estilo Godot): cada objeto tem uma mascara de bits (MeshRenderer::Layers,
// bit 0 = camada 1) dizendo em QUAIS camadas ele esta; cada camera tem uma
// mascara (Camera::CullMask) dizendo QUAIS camadas ela enxerga. O objeto
// aparece se as duas mascaras tem algum bit em comum.
// ============================================================================

#include <cmath>
#include <cstdint>

namespace Prism::Culling {

    constexpr uint32_t kAllLayers = 0xFFFFFFFFu;
    constexpr uint32_t kDefaultLayer = 1u; // camada 1 (bit 0)
    constexpr int kLayerCount = 32;

    inline bool LayerVisible(uint32_t objectLayers, uint32_t cameraMask) {
        return (objectLayers & cameraMask) != 0u;
    }
    inline uint32_t LayerBit(int index) { // index 0..31 (fora disso: 0)
        return (index >= 0 && index < kLayerCount) ? (1u << index) : 0u;
    }

    struct Frustum {
        // ax + by + cz + d >= 0 = dentro. Ordem: esquerda, direita, baixo, cima, perto, longe.
        float Planes[6][4] = {};
        bool Valid = false;

        // 'viewProjection' = projecao * view (coluna-major, profundidade GL -1..1).
        // Matriz com NaN/inf ou degenerada: Valid = false (nada e descartado).
        static Frustum FromViewProjection(const float* m) {
            Frustum f;
            for (int i = 0; i < 16; i++)
                if (!std::isfinite(m[i]))
                    return f;
            auto row = [&](int r, int c) { return m[c * 4 + r]; };
            const float sign[6] = { 1, -1, 1, -1, 1, -1 };
            const int axis[6] = { 0, 0, 1, 1, 2, 2 };
            for (int p = 0; p < 6; p++) {
                for (int c = 0; c < 4; c++)
                    f.Planes[p][c] = row(3, c) + sign[p] * row(axis[p], c);
                float length = std::sqrt(f.Planes[p][0] * f.Planes[p][0] + f.Planes[p][1] * f.Planes[p][1] + f.Planes[p][2] * f.Planes[p][2]);
                if (!(length > 1e-12f))
                    return Frustum{};
                for (int c = 0; c < 4; c++)
                    f.Planes[p][c] /= length;
            }
            f.Valid = true;
            return f;
        }

        // Caixa em espaco de MUNDO. Conservador: so devolve false se esta TOTALMENTE fora de um plano.
        bool IntersectsBox(const float center[3], const float extents[3]) const {
            if (!Valid)
                return true;
            for (int p = 0; p < 6; p++) {
                const float* plane = Planes[p];
                float distance = plane[0] * center[0] + plane[1] * center[1] + plane[2] * center[2] + plane[3];
                float radius = std::fabs(plane[0]) * extents[0] + std::fabs(plane[1]) * extents[1] + std::fabs(plane[2]) * extents[2];
                if (distance < -radius)
                    return false;
            }
            return true;
        }

        // Caixa LOCAL (min/max) transformada por 'model' (coluna-major) - inclui rotacao e escala.
        bool IntersectsLocalBox(const float localMin[3], const float localMax[3], const float* model) const {
            if (!Valid)
                return true;
            for (int i = 0; i < 3; i++)
                if (!std::isfinite(localMin[i]) || !std::isfinite(localMax[i]))
                    return true; // limites invalidos: nao descarta
            for (int i = 0; i < 16; i++)
                if (!std::isfinite(model[i]))
                    return true;
            float c[3], e[3];
            for (int i = 0; i < 3; i++) {
                float lc[3] = { (localMin[0] + localMax[0]) * 0.5f, (localMin[1] + localMax[1]) * 0.5f, (localMin[2] + localMax[2]) * 0.5f };
                float le[3] = { (localMax[0] - localMin[0]) * 0.5f, (localMax[1] - localMin[1]) * 0.5f, (localMax[2] - localMin[2]) * 0.5f };
                c[i] = model[0 * 4 + i] * lc[0] + model[1 * 4 + i] * lc[1] + model[2 * 4 + i] * lc[2] + model[3 * 4 + i];
                e[i] = std::fabs(model[0 * 4 + i]) * std::fabs(le[0]) + std::fabs(model[1 * 4 + i]) * std::fabs(le[1]) + std::fabs(model[2 * 4 + i]) * std::fabs(le[2]);
            }
            return IntersectsBox(c, e);
        }
    };

}
