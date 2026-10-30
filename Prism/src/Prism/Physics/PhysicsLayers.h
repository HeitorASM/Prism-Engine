#pragma once

// ============================================================================
// PhysicsLayers.h
// Camadas de FISICA e matriz de colisao (estilo Unity), so std - sem Jolt nem
// EnTT, testado isoladamente em tests/PhysicsLayersTests.cpp.
//
// Cada ColliderComponent esta em UMA camada (0..31). A matriz do PROJETO diz
// quais pares de camadas colidem (simetrica). Triggers tambem respeitam a matriz.
// Consultas (Raycast) usam uma mascara de camadas por chamada.
//
// O Jolt recebe um ObjectLayer de 16 bits = (camada << 1) | movel. O bit baixo
// separa corpos estaticos de moveis (o broad-phase do Jolt continua com 2 camadas:
// dois estaticos nunca sao testados entre si).
// ============================================================================

#include <cstdint>

namespace Prism::Physics {

    constexpr int kLayerCount = 32;
    constexpr uint32_t kAllLayers = 0xFFFFFFFFu;

    constexpr uint16_t MakeObjectLayer(int userLayer, bool moving) {
        return (uint16_t)(((userLayer < 0 ? 0 : (userLayer >= kLayerCount ? kLayerCount - 1 : userLayer)) << 1) | (moving ? 1 : 0));
    }
    constexpr int UserLayerOf(uint16_t objectLayer) { return (int)(objectLayer >> 1); }
    constexpr bool IsMovingLayer(uint16_t objectLayer) { return (objectLayer & 1u) != 0u; }
    constexpr uint32_t LayerBit(int userLayer) { return (userLayer >= 0 && userLayer < kLayerCount) ? (1u << userLayer) : 0u; }

    class LayerMatrix {
    public:
        LayerMatrix() { for (uint32_t& mask : m_Masks) mask = kAllLayers; } // padrao: tudo colide com tudo

        // Camadas fora de 0..31 nunca colidem.
        bool ShouldCollide(int a, int b) const {
            if (a < 0 || a >= kLayerCount || b < 0 || b >= kLayerCount)
                return false;
            return ((m_Masks[a] >> b) & 1u) != 0u && ((m_Masks[b] >> a) & 1u) != 0u;
        }

        // Liga/desliga a colisao do par (simetrico: a-b e b-a juntos).
        void SetCollision(int a, int b, bool collide) {
            if (a < 0 || a >= kLayerCount || b < 0 || b >= kLayerCount)
                return;
            if (collide) { m_Masks[a] |= (1u << b); m_Masks[b] |= (1u << a); }
            else         { m_Masks[a] &= ~(1u << b); m_Masks[b] &= ~(1u << a); }
        }

        // Camadas com que 'layer' colide (mascara de bits, ja simetrica).
        uint32_t MaskOf(int layer) const {
            if (layer < 0 || layer >= kLayerCount)
                return 0u;
            uint32_t result = 0;
            for (int other = 0; other < kLayerCount; other++)
                if (ShouldCollide(layer, other))
                    result |= (1u << other);
            return result;
        }

        // Mascara BRUTA gravada (so para salvar em arquivo; ver SetRawMask).
        uint32_t RawMask(int layer) const { return (layer >= 0 && layer < kLayerCount) ? m_Masks[layer] : 0u; }
        void SetRawMask(int layer, uint32_t mask) { if (layer >= 0 && layer < kLayerCount) m_Masks[layer] = mask; }

        bool IsDefault() const { for (uint32_t mask : m_Masks) if (mask != kAllLayers) return false; return true; }

    private:
        uint32_t m_Masks[kLayerCount];
    };

}
