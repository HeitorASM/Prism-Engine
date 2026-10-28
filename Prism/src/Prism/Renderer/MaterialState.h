#pragma once

// ============================================================================
// MaterialState.h
// Identidade do ESTADO de material para o agrupamento de desenhos
// (DrawBatching.h): dois MaterialComponent com o mesmo estado ganham o mesmo
// id e podem ser instanciados juntos.
//
// Antes, DrawScene montava a cada objeto, a cada quadro, uma chave com varias
// std::string copiadas + snprintf dos parametros do shader e a inseria num
// std::map (comparacao de ~20 campos por no visitado). Aqui o id sai de um
// hash de 64 bits (sem alocar) e, em caso de colisao, de uma comparacao campo
// a campo - o resultado e EXATAMENTE o mesmo agrupamento, so que barato.
//
// O que entra no estado (igual a antiga MaterialKey): caminhos das texturas,
// roughness/metallic (com clamp 0..1), blend, opacidade, cutoff, tiling/offset
// de UV, emissivo, dupla face, ShaderPath e ShaderParams (nome + valor).
// O que NAO entra: AlbedoTint (e por objeto/instancia) e LinkedAsset.
// 'nullptr' (entidade sem MaterialComponent) e um estado proprio.
//
// So depende de Components.h (glm/EnTT): testado em tests/MaterialStateTests.cpp.
// ============================================================================

#include "../Scene/Components.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Prism::MaterialState {

    namespace detail {
        inline uint32_t Bits(float v) { uint32_t b; std::memcpy(&b, &v, sizeof(b)); return b; } // NaN-safe (compara bits)
        inline float Clamp01(float v) { return std::min(std::max(v, 0.0f), 1.0f); }

        struct Hasher {
            uint64_t h = 1469598103934665603ull; // FNV-1a 64
            void Byte(uint8_t b) { h ^= b; h *= 1099511628211ull; }
            void U32(uint32_t v) { for (int i = 0; i < 4; i++) Byte((uint8_t)(v >> (i * 8))); }
            void F(float v) { U32(Bits(v)); }
            void Str(const std::string& s) { for (char c : s) Byte((uint8_t)c); Byte(0xFF); } // 0xFF separa campos adjacentes
        };
    }

    inline uint64_t Hash(const MaterialComponent* m) {
        detail::Hasher x;
        if (!m) { x.Byte(0); return x.h; }
        x.Byte(1);
        x.Str(m->AlbedoPath); x.Str(m->NormalPath); x.Str(m->RoughnessMetallicPath);
        x.F(detail::Clamp01(m->RoughnessFactor)); x.F(detail::Clamp01(m->MetallicFactor));
        x.U32((uint32_t)(int)m->BlendMode); x.F(m->Opacity); x.F(m->AlphaCutoff);
        x.F(m->UVTiling.x); x.F(m->UVTiling.y); x.F(m->UVOffset.x); x.F(m->UVOffset.y);
        x.F(m->EmissiveColor.x); x.F(m->EmissiveColor.y); x.F(m->EmissiveColor.z); x.F(m->EmissiveIntensity);
        x.Byte(m->DoubleSided ? 1 : 0);
        x.Str(m->ShaderPath);
        for (const MaterialShaderParam& p : m->ShaderParams) {
            x.Str(p.Name); x.F(p.Value.x); x.F(p.Value.y); x.F(p.Value.z); x.F(p.Value.w);
        }
        return x.h;
    }

    inline bool Equal(const MaterialComponent* a, const MaterialComponent* b) {
        using detail::Bits; using detail::Clamp01;
        if (a == b) return true;
        if (!a || !b) return false;
        if (a->AlbedoPath != b->AlbedoPath || a->NormalPath != b->NormalPath || a->RoughnessMetallicPath != b->RoughnessMetallicPath) return false;
        if (Bits(Clamp01(a->RoughnessFactor)) != Bits(Clamp01(b->RoughnessFactor))) return false;
        if (Bits(Clamp01(a->MetallicFactor)) != Bits(Clamp01(b->MetallicFactor))) return false;
        if (a->BlendMode != b->BlendMode || a->DoubleSided != b->DoubleSided) return false;
        if (Bits(a->Opacity) != Bits(b->Opacity) || Bits(a->AlphaCutoff) != Bits(b->AlphaCutoff)) return false;
        if (Bits(a->UVTiling.x) != Bits(b->UVTiling.x) || Bits(a->UVTiling.y) != Bits(b->UVTiling.y)) return false;
        if (Bits(a->UVOffset.x) != Bits(b->UVOffset.x) || Bits(a->UVOffset.y) != Bits(b->UVOffset.y)) return false;
        if (Bits(a->EmissiveColor.x) != Bits(b->EmissiveColor.x) || Bits(a->EmissiveColor.y) != Bits(b->EmissiveColor.y)
            || Bits(a->EmissiveColor.z) != Bits(b->EmissiveColor.z) || Bits(a->EmissiveIntensity) != Bits(b->EmissiveIntensity)) return false;
        if (a->ShaderPath != b->ShaderPath) return false;
        if (a->ShaderParams.size() != b->ShaderParams.size()) return false;
        for (size_t i = 0; i < a->ShaderParams.size(); i++) {
            const MaterialShaderParam& pa = a->ShaderParams[i];
            const MaterialShaderParam& pb = b->ShaderParams[i];
            if (pa.Name != pb.Name) return false;
            if (Bits(pa.Value.x) != Bits(pb.Value.x) || Bits(pa.Value.y) != Bits(pb.Value.y)
                || Bits(pa.Value.z) != Bits(pb.Value.z) || Bits(pa.Value.w) != Bits(pb.Value.w)) return false;
        }
        return true;
    }

    // Tabela de ids de UM quadro. Guarda PONTEIROS para os MaterialComponent
    // (que nao se movem durante o DrawScene): chame Clear() no inicio de cada
    // quadro. Os ids saem 0,1,2... na ordem em que os estados aparecem.
    class IdTable {
    public:
        void Clear() { m_ByHash.clear(); m_Representatives.clear(); }

        uint32_t GetOrAdd(const MaterialComponent* material) {
            const uint64_t hash = Hash(material);
            auto range = m_ByHash.equal_range(hash);
            for (auto it = range.first; it != range.second; ++it)
                if (Equal(m_Representatives[it->second], material))
                    return it->second;
            const uint32_t id = (uint32_t)m_Representatives.size();
            m_Representatives.push_back(material);
            m_ByHash.emplace(hash, id);
            return id;
        }

        size_t Count() const { return m_Representatives.size(); }

    private:
        std::unordered_multimap<uint64_t, uint32_t> m_ByHash;
        std::vector<const MaterialComponent*> m_Representatives;
    };

}
