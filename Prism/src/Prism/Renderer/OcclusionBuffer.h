#pragma once

// ============================================================================
// OcclusionBuffer.h
// Oclusao por software (CPU), so std - testada em tests/OcclusionBufferTests.cpp.
// Rasteriza triangulos de OCLUSORES opacos num buffer de profundidade pequeno
// (kWidth x kHeight) e responde "esta caixa de mundo esta totalmente atras do
// que ja foi rasterizado?". Conservador: na duvida a caixa e VISIVEL (nunca some
// um objeto que aparece).
//   - triangulo com algum vertice atras do near: ignorado (so perde oclusao);
//   - a caixa so e oculta se TODO pixel que ela pode tocar (retangulo engordado
//     em 1 pixel) tem oclusor mais perto que a face mais proxima da caixa
//     (com folga de profundidade);
//   - caixa que cruza o near: visivel.
// Profundidade = distancia ao longo do olhar (clip.w de uma projecao em
// perspectiva). Camera ortografica: nao use (o chamador nao ativa).
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Prism {

    class OcclusionBuffer {
    public:
        static constexpr int kWidth = 256;
        static constexpr int kHeight = 128;
        static constexpr float kInfinity = 3.0e38f;
        // O buffer cobre a tela com uma margem (banda de guarda): assim o pixel vizinho da borda da tela existe e a
        // checagem "centros vizinhos cobertos" vale tambem para objetos encostados na borda.
        static constexpr float kGuard = 1.1f;

        // 'viewProjection' coluna-major (projecao * view), perspectiva.
        void Begin(const float* viewProjection) {
            m_Depth.assign((size_t)kWidth * kHeight, kInfinity);
            m_Valid = true;
            for (int i = 0; i < 16; i++) {
                m_VP[i] = viewProjection[i];
                if (!std::isfinite(m_VP[i])) m_Valid = false;
            }
            m_Triangles = 0;
        }
        bool Valid() const { return m_Valid; }
        uint32_t TrianglesRasterized() const { return m_Triangles; }

        // Triangulo em espaco de mundo.
        void AddTriangle(const float a[3], const float b[3], const float c[3]) {
            if (!m_Valid) return;
            float sx[3], sy[3], invW[3];
            const float* v[3] = { a, b, c };
            for (int i = 0; i < 3; i++) {
                float clip[4];
                Project(v[i], clip);
                if (!(clip[3] > kNear)) return;              // atras do near: ignora (conservador)
                invW[i] = 1.0f / clip[3];
                sx[i] = (clip[0] * invW[i] / kGuard * 0.5f + 0.5f) * kWidth;
                sy[i] = (clip[1] * invW[i] / kGuard * 0.5f + 0.5f) * kHeight;
                if (!std::isfinite(sx[i]) || !std::isfinite(sy[i])) return;
            }
            const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
            if (std::fabs(area) < 1e-3f) return;
            const int x0 = std::max(0, (int)std::floor(std::min({ sx[0], sx[1], sx[2] })));
            const int x1 = std::min(kWidth - 1, (int)std::ceil(std::max({ sx[0], sx[1], sx[2] })));
            const int y0 = std::max(0, (int)std::floor(std::min({ sy[0], sy[1], sy[2] })));
            const int y1 = std::min(kHeight - 1, (int)std::ceil(std::max({ sy[0], sy[1], sy[2] })));
            if (x0 > x1 || y0 > y1) return;
            m_Triangles++;
            const float inv = 1.0f / area;
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    const float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv;
                    const float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                    const float depth = 1.0f / (w0 * invW[0] + w1 * invW[1] + w2 * invW[2]); // 1/w e linear na tela
                    float& stored = m_Depth[(size_t)y * kWidth + x];
                    if (depth < stored) stored = depth;
                }
            }
        }

        // Caixa alinhada aos eixos do MUNDO. true = totalmente oculta.
        bool IsBoxOccluded(const float mn[3], const float mx[3]) const {
            if (!m_Valid) return false;
            float minX = kInfinity, maxX = -kInfinity, minY = kInfinity, maxY = -kInfinity, nearest = kInfinity;
            for (int i = 0; i < 8; i++) {
                const float p[3] = { (i & 1) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1], (i & 4) ? mx[2] : mn[2] };
                float clip[4];
                Project(p, clip);
                if (!(clip[3] > kNear)) return false;       // cruza (ou esta atras do) near: visivel
                const float iw = 1.0f / clip[3];
                const float sx = (clip[0] * iw / kGuard * 0.5f + 0.5f) * kWidth;
                const float sy = (clip[1] * iw / kGuard * 0.5f + 0.5f) * kHeight;
                if (!std::isfinite(sx) || !std::isfinite(sy)) return false;
                minX = std::min(minX, sx); maxX = std::max(maxX, sx);
                minY = std::min(minY, sy); maxY = std::max(maxY, sy);
                nearest = std::min(nearest, clip[3]);
            }
            const int x0 = std::max(0, (int)std::floor(minX) - 1), x1 = std::min(kWidth - 1, (int)std::ceil(maxX) + 1);
            const int y0 = std::max(0, (int)std::floor(minY) - 1), y1 = std::min(kHeight - 1, (int)std::ceil(maxY) + 1);
            if (x0 > x1 || y0 > y1) return false;            // fora da tela: nao e papel da oclusao decidir
            const float limit = nearest * 0.995f - 0.02f;   // folga contra imprecisao e z-fighting
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                    if (!(m_Depth[(size_t)y * kWidth + x] < limit))
                        return false;
            return true;
        }

    private:
        static constexpr float kNear = 1e-3f;
        void Project(const float p[3], float clip[4]) const {
            for (int r = 0; r < 4; r++)
                clip[r] = m_VP[0 * 4 + r] * p[0] + m_VP[1 * 4 + r] * p[1] + m_VP[2 * 4 + r] * p[2] + m_VP[3 * 4 + r];
        }
        std::vector<float> m_Depth;
        float m_VP[16] = {};
        bool m_Valid = false;
        uint32_t m_Triangles = 0;
    };

}
