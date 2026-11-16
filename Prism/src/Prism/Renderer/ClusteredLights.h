#pragma once

// ============================================================================
// ClusteredLights.h
// Grade de clusters (X x Y x fatias em Z logaritmicas) para iluminacao com
// MUITAS luzes. So std: sem OpenGL/GLM - testado em tests/ClusteredLightsTests.cpp.
//
// Como entra no motor: as MAX_LIGHTS luzes mais relevantes (diretas, com
// sombra) continuam no array de uniforms de sempre. As que SOBRAM (pontuais e
// spots, sem sombra) viram "luzes extras": Build() reparte cada uma pelos
// clusters que a esfera de alcance toca; o shader (basic.frag) acha o cluster
// do fragmento e soma so as luzes da lista dele.
//
// A matematica de cluster (SliceOf / tile) tem que bater com basic.frag.
// A esfera e engordada em ~1% (+1 cm) para absorver diferenca de precisao
// float entre CPU e GPU nas bordas de fatia/tile: o resultado e sempre um
// SUPERCONJUNTO do necessario (nunca falta uma luz), no maximo sobra uma.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Prism::ClusteredLights {

    constexpr int kTilesX = 16;
    constexpr int kTilesY = 9;
    constexpr int kSlices = 24;
    constexpr int kClusterCount = kTilesX * kTilesY * kSlices;
    constexpr uint32_t kMaxExtraLights = 1024;
    constexpr uint32_t kMaxPerCluster = 64;

    struct Light {
        float Position[3] = { 0, 0, 0 };   // mundo
        float Range = 0.0f;                // > 0
    };

    struct Grid {
        bool Valid = false;                // false: projecao ortografica/invalida (sem clusters)
        float Near = 0.1f, Far = 100.0f;
        float P00 = 1, P11 = 1, P20 = 0, P21 = 0;
        float View[16] = {};               // coluna-major, mundo -> view
        float InvLogRatio = 0.0f;          // 1 / ln(Far / Near)
    };

    // 'view' e 'projection' coluna-major (GL). Projecao em perspectiva padrao (profundidade -1..1).
    inline Grid MakeGrid(const float* view, const float* projection) {
        Grid g;
        for (int i = 0; i < 16; i++)
            if (!std::isfinite(view[i]) || !std::isfinite(projection[i]))
                return g;
        // Em perspectiva P[3][3] == 0 e P[2][3] == -1 (indices coluna*4+linha).
        if (std::fabs(projection[15]) > 1e-6f || std::fabs(projection[11] + 1.0f) > 1e-4f)
            return g;
        const float a = projection[10], b = projection[14];
        const float denomNear = a - 1.0f, denomFar = a + 1.0f;
        if (std::fabs(denomNear) < 1e-12f || std::fabs(denomFar) < 1e-12f)
            return g;
        g.Near = b / denomNear;
        g.Far = b / denomFar;
        if (!(g.Near > 1e-6f) || !(g.Far > g.Near * 1.0001f) || !std::isfinite(g.Far))
            return g;
        g.P00 = projection[0]; g.P11 = projection[5];
        g.P20 = projection[8]; g.P21 = projection[9];
        if (!(g.P00 > 1e-9f) || !(g.P11 > 1e-9f))
            return g;
        for (int i = 0; i < 16; i++) g.View[i] = view[i];
        g.InvLogRatio = 1.0f / std::log(g.Far / g.Near);
        g.Valid = true;
        return g;
    }

    inline int SliceOf(const Grid& g, float depth) {
        if (!(depth > g.Near)) return 0;
        if (depth >= g.Far) return kSlices - 1;
        const int s = (int)std::floor(std::log(depth / g.Near) * g.InvLogRatio * (float)kSlices);
        return std::min(std::max(s, 0), kSlices - 1);
    }

    inline int TileOf(float ndc, int tiles) {
        const int t = (int)std::floor((ndc * 0.5f + 0.5f) * (float)tiles);
        return std::min(std::max(t, 0), tiles - 1);
    }

    // Posicao de mundo -> profundidade (distancia ao longo do olhar, positiva a frente).
    inline float DepthOf(const Grid& g, const float p[3]) {
        return -(g.View[2] * p[0] + g.View[6] * p[1] + g.View[10] * p[2] + g.View[14]);
    }

    // Cluster de um ponto de mundo (para testes). -1 se a grade e invalida.
    inline int ClusterOfPoint(const Grid& g, const float p[3]) {
        if (!g.Valid) return -1;
        const float vx = g.View[0] * p[0] + g.View[4] * p[1] + g.View[8] * p[2] + g.View[12];
        const float vy = g.View[1] * p[0] + g.View[5] * p[1] + g.View[9] * p[2] + g.View[13];
        const float d = DepthOf(g, p);
        const float dd = std::max(d, g.Near);
        const float ndcX = g.P00 * vx / dd - g.P20;
        const float ndcY = g.P11 * vy / dd - g.P21;
        return (SliceOf(g, d) * kTilesY + TileOf(ndcY, kTilesY)) * kTilesX + TileOf(ndcX, kTilesX);
    }

    struct Result {
        // Por cluster: [2*i] = deslocamento em Indices, [2*i+1] = quantidade. Tamanho 2 * kClusterCount (ou vazio se a grade e invalida).
        std::vector<uint32_t> Clusters;
        std::vector<uint32_t> Indices;     // indices em 'lights' (mais relevantes primeiro dentro de cada cluster)
        uint32_t Overflowed = 0;           // luzes descartadas por cluster cheio (kMaxPerCluster)
    };

    namespace detail {
        struct Range { int x0, x1, y0, y1, z0, z1; bool Any; };

        inline Range RangeOf(const Grid& g, const Light& l) {
            Range r{ 0, -1, 0, -1, 0, -1, false };
            if (!(l.Range > 0.0f) || !std::isfinite(l.Range))
                return r;
            for (int i = 0; i < 3; i++)
                if (!std::isfinite(l.Position[i])) return r;
            const float rad = l.Range * 1.01f + 0.01f;
            const float* m = g.View;
            const float cx = m[0] * l.Position[0] + m[4] * l.Position[1] + m[8] * l.Position[2] + m[12];
            const float cy = m[1] * l.Position[0] + m[5] * l.Position[1] + m[9] * l.Position[2] + m[13];
            const float cd = -(m[2] * l.Position[0] + m[6] * l.Position[1] + m[10] * l.Position[2] + m[14]);
            const float dmin = std::max(cd - rad, g.Near);
            const float dmax = std::min(cd + rad, g.Far);
            if (dmin > dmax) return r;                       // inteira atras do near / alem do far
            r.z0 = SliceOf(g, dmin);
            r.z1 = SliceOf(g, dmax);
            // x/d e monotono em x e em d (d > 0): os extremos estao nos cantos da caixa.
            float nxLo = 1e30f, nxHi = -1e30f, nyLo = 1e30f, nyHi = -1e30f;
            const float ds[2] = { dmin, dmax };
            for (int i = 0; i < 2; i++) {
                const float d = ds[i];
                for (int s = -1; s <= 1; s += 2) {
                    const float nx = g.P00 * (cx + s * rad) / d - g.P20;
                    const float ny = g.P11 * (cy + s * rad) / d - g.P21;
                    nxLo = std::min(nxLo, nx); nxHi = std::max(nxHi, nx);
                    nyLo = std::min(nyLo, ny); nyHi = std::max(nyHi, ny);
                }
            }
            if (nxHi < -1.0f || nxLo > 1.0f || nyHi < -1.0f || nyLo > 1.0f) return r; // fora da tela
            r.x0 = TileOf(std::max(nxLo, -1.0f), kTilesX); r.x1 = TileOf(std::min(nxHi, 1.0f), kTilesX);
            r.y0 = TileOf(std::max(nyLo, -1.0f), kTilesY); r.y1 = TileOf(std::min(nyHi, 1.0f), kTilesY);
            r.Any = true;
            return r;
        }
    }

    // 'lights' em ORDEM DE RELEVANCIA (a primeira e a mais importante): se um cluster enche, as ultimas caem.
    inline Result Build(const Grid& g, const std::vector<Light>& lights) {
        Result out;
        if (!g.Valid)
            return out;
        out.Clusters.assign((size_t)kClusterCount * 2, 0u);
        std::vector<detail::Range> ranges(lights.size());
        std::vector<uint32_t> counts(kClusterCount, 0u);
        for (size_t i = 0; i < lights.size(); i++) {
            ranges[i] = detail::RangeOf(g, lights[i]);
            if (!ranges[i].Any) continue;
            const auto& r = ranges[i];
            for (int z = r.z0; z <= r.z1; z++)
                for (int y = r.y0; y <= r.y1; y++)
                    for (int x = r.x0; x <= r.x1; x++)
                        counts[(z * kTilesY + y) * kTilesX + x]++;
        }
        uint32_t total = 0;
        for (int c = 0; c < kClusterCount; c++) {
            const uint32_t kept = std::min(counts[c], kMaxPerCluster);
            out.Clusters[2 * c] = total;
            total += kept;
        }
        out.Indices.assign(total, 0u);
        for (size_t i = 0; i < lights.size(); i++) {
            if (!ranges[i].Any) continue;
            const auto& r = ranges[i];
            for (int z = r.z0; z <= r.z1; z++)
                for (int y = r.y0; y <= r.y1; y++)
                    for (int x = r.x0; x <= r.x1; x++) {
                        const int c = (z * kTilesY + y) * kTilesX + x;
                        uint32_t& n = out.Clusters[2 * c + 1];
                        if (n >= kMaxPerCluster) { out.Overflowed++; continue; }
                        out.Indices[out.Clusters[2 * c] + n] = (uint32_t)i;
                        n++;
                    }
        }
        return out;
    }

}
