#pragma once

// ============================================================================
// ShadowCache.h
// Decide quando o mapa de sombra de uma camada (um spot, ou uma face de point)
// pode ser REAPROVEITADO do quadro anterior em vez de redesenhado. So std: sem
// OpenGL nem GLM - compila e e testado isoladamente (tests/ShadowCacheTests.cpp).
//
// IDEIA: o mapa de profundidade de uma camada e funcao pura de (a) como a luz
// olha (matriz de luz) e (b) quais casters tocam o volume dela, com qual malha
// e qual transform. Se tudo isso e bit a bit igual ao quadro em que a camada foi
// desenhada, a imagem seria identica: pular o desenho e sem perda. Cena estatica
// com luzes estaticas = zero passes de sombra spot/point.
//
// ASSINATURA: hash 64 bits = f(luz) + soma dos hashes de cada caster visivel.
// A soma e COMUTATIVA de proposito: a ordem dos casters muda quando entidades
// sao criadas/destruidas e isso nao deve invalidar nada. O hash de um caster usa
// o id UNICO da malha (nao o ponteiro: um ponteiro liberado pode ser reusado por
// outra malha) e os 16 floats do transform.
//
// CASTER ANIMADO (skinning): a pose muda todo quadro e o desenho depende dos
// ossos, que a assinatura nao cobre. Se algum caster animado esta no volume, a
// camada e marcada DINAMICA (Signature::Dynamic): sempre redesenhada e nunca
// guardada.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Prism::ShadowCache {

    // Misturador de 64 bits (finalizador do splitmix64): dois valores proximos viram hashes distantes.
    inline uint64_t Avalanche(uint64_t x) {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    inline uint64_t Combine(uint64_t seed, uint64_t value) {
        return Avalanche(seed ^ (Avalanche(value) + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2)));
    }

    // Hash dos BITS dos floats (0.0 e -0.0 diferem: so causa um redesenho a mais, nunca um erro).
    inline uint64_t HashFloats(const float* values, size_t count, uint64_t seed = 0) {
        uint64_t hash = seed;
        for (size_t i = 0; i < count; ++i) {
            uint32_t bits;
            std::memcpy(&bits, &values[i], sizeof(bits));
            hash = Combine(hash, bits);
        }
        return hash;
    }

    inline uint64_t CasterHash(uint64_t meshId, const float model[16]) {
        return HashFloats(model, 16, Combine(0x5348414Full, meshId));
    }

    struct Signature {
        uint64_t Sum = 0;
        uint32_t Count = 0;
        bool Dynamic = false;

        void AddCaster(uint64_t meshId, const float model[16]) {
            Sum += CasterHash(meshId, model); // soma: independente da ordem
            ++Count;
        }
        void MarkDynamic() { Dynamic = true; }

        // 'lightHash' = hash do que descreve a luz/camada (matriz de luz etc.).
        uint64_t Finish(uint64_t lightHash) const { return Combine(Combine(lightHash, Sum), Count); }
    };

    // Ultima assinatura desenhada em cada camada de um array de texturas.
    class LayerCache {
    public:
        explicit LayerCache(size_t layers = 0) { Resize(layers); }

        void Resize(size_t layers) { m_Signature.assign(layers, 0); m_Valid.assign(layers, false); }

        // A camada ainda contem o desenho de 'signature'?
        bool Matches(size_t layer, uint64_t signature) const {
            return layer < m_Valid.size() && m_Valid[layer] && m_Signature[layer] == signature;
        }
        void Store(size_t layer, uint64_t signature) {
            if (layer < m_Valid.size()) { m_Signature[layer] = signature; m_Valid[layer] = true; }
        }
        void Invalidate(size_t layer) { if (layer < m_Valid.size()) m_Valid[layer] = false; }
        void InvalidateAll() { m_Valid.assign(m_Valid.size(), false); }

    private:
        std::vector<uint64_t> m_Signature;
        std::vector<bool> m_Valid;
    };

}
