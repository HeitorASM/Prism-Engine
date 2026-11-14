#pragma once

// ============================================================================
// ParticleForces.h
// Forcas e colisao das particulas, sem estado e sem engine (so std + glm): testado sozinho
// (tests/ParticleForcesTests.cpp).
//  - Noise3 / TurbulenceField: ruido de valor 3D suave (continuo no espaco e no tempo) e um campo
//    vetorial feito de 3 amostras descorrelacionadas, com media ~0, em [-1, 1] por eixo.
//  - CollideGround: colisao de uma particula (esfera de raio r) com um plano horizontal de MUNDO
//    (y = altura): quica com coeficiente de restituicao, atrito tangencial e repouso sem tremer.
// Tudo e deterministico (sem rand): o mesmo estado da o mesmo resultado.
// ============================================================================

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace Prism::ParticleForces {

    inline uint32_t HashLattice(int32_t x, int32_t y, int32_t z, uint32_t salt) {
        uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u ^ (uint32_t)z * 0xCB1AB31Fu ^ salt * 0x9E3779B9u;
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
        return h;
    }

    // Valor de rede em [-1, 1].
    inline float Lattice(int32_t x, int32_t y, int32_t z, uint32_t salt) {
        return (float)(HashLattice(x, y, z, salt) & 0xFFFFFFu) / (float)0x7FFFFFu - 1.0f;
    }

    // Ruido de valor 3D com interpolacao suave (quintica): continuo, em [-1, 1].
    inline float Noise3(float x, float y, float z, uint32_t salt = 0) {
        const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
        const int32_t ix = (int32_t)fx, iy = (int32_t)fy, iz = (int32_t)fz;
        auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
        const float u = fade(x - fx), v = fade(y - fy), w = fade(z - fz);
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        auto L = [&](int dx, int dy, int dz) { return Lattice(ix + dx, iy + dy, iz + dz, salt); };
        const float x00 = lerp(L(0,0,0), L(1,0,0), u), x10 = lerp(L(0,1,0), L(1,1,0), u);
        const float x01 = lerp(L(0,0,1), L(1,0,1), u), x11 = lerp(L(0,1,1), L(1,1,1), u);
        return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
    }

    // Campo vetorial de turbulencia em 'position' (mundo) no instante 'time'. 'frequency' = ciclos por unidade
    // de mundo; o tempo anda ao longo de um eixo diagonal do ruido, entao o campo evolui suavemente.
    inline glm::vec3 TurbulenceField(const glm::vec3& position, float time, float frequency) {
        const float f = frequency;
        const float px = position.x * f, py = position.y * f, pz = position.z * f;
        return glm::vec3(Noise3(px, py, pz + time, 11u),
                         Noise3(px + 31.4f, py - 17.2f, pz + time, 23u),
                         Noise3(px - 53.7f, py + 8.9f, pz + time, 37u));
    }

    // Colisao com o plano y = groundY. Devolve true se houve impacto.
    //  bounce   0..1 : fracao da velocidade vertical devolvida
    //  friction 0..1 : fracao da velocidade horizontal perdida no impacto
    // Repouso: se a velocidade vertical apos o quique for pequena, zera (nao fica tremendo no chao).
    inline bool CollideGround(glm::vec3& position, glm::vec3& velocity, float groundY, float radius, float bounce, float friction) {
        if (position.y - radius >= groundY)
            return false;
        position.y = groundY + radius;
        if (velocity.y < 0.0f) {
            velocity.y = -velocity.y * bounce;
            if (velocity.y < 0.3f)
                velocity.y = 0.0f;
            velocity.x *= (1.0f - friction);
            velocity.z *= (1.0f - friction);
        }
        return true;
    }
}
