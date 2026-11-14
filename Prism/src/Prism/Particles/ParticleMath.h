#pragma once

// ============================================================================
// ParticleMath.h
// Matematica pura do sistema de particulas, so std (sem glm, EnTT nem OpenGL):
// compila e e testada isoladamente (tests/ParticleMathTests.cpp).
//
//  - ParticleCurve:    curva escalar ao longo da vida (t = 0..1), ate 8 chaves,
//                      interpolacao linear. Usada como MULTIPLICADOR de tamanho
//                      e de velocidade.
//  - ParticleGradient: gradiente RGBA ao longo da vida, ate 8 paradas. Usado como
//                      MULTIPLICADOR da cor (cor do emissor * gradiente).
//  - SampleShape:      posicao/direcao de nascimento no espaco LOCAL do emissor
//                      (eixo de emissao = +Y).
//  - FlipbookFrame:    quadro atual de uma folha de sprites (sprite sheet).
//
// Os tipos sao trivialmente copiaveis e com padrao "neutro" (curva = 1,
// gradiente = branco): um emissor antigo, sem essas configuracoes, desenha
// exatamente como antes.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Prism {

    // --- Enums (valores gravados no arquivo: nao reordene) -------------------

    enum class ParticleSimulationSpace : int {
        World = 0,   // particulas ficam no mundo; se a entidade anda, as ja emitidas ficam para tras
        Local = 1    // particulas acompanham a entidade (movem, giram e escalam com ela)
    };

    enum class ParticleShape : int {
        Cone = 0,        // ponto de origem; direcao num cone ao redor de +Y (comportamento original)
        Sphere = 1,      // esfera (ou casca); direcao para fora do centro
        Hemisphere = 2,  // metade de cima (+Y) da esfera; direcao para fora do centro
        Box = 3,         // caixa (ShapeBox); direcao em cone ao redor de +Y
        Circle = 4,      // disco/anel no plano XZ; direcao em cone ao redor de +Y
        Edge = 5         // segmento no eixo X, de -Raio a +Raio; direcao em cone ao redor de +Y
    };

    enum class ParticleBlendMode : int {
        Alpha = 0,         // alpha normal (fumaca, poeira); ordenado de tras para frente
        Additive = 1,      // soma luz (fogo, faiscas, brilho)
        SoftAdditive = 2,  // soma luz sem estourar o branco (1 - cor): neblina luminosa, brilhos suaves
        Multiply = 3       // escurece o fundo (sombras, manchas, fuligem)
    };

    enum class ParticleFlipbookMode : int {
        OverLifetime = 0,  // percorre todos os quadros durante a vida da particula
        FixedFPS = 1,      // avanca FlipbookFPS quadros por segundo (repete)
        RandomStatic = 2   // cada particula escolhe um quadro aleatorio e o mantem
    };

    // --- Curva ---------------------------------------------------------------

    constexpr int kParticleMaxCurveKeys = 8;
    constexpr int kParticleMaxGradientStops = 8;

    struct ParticleCurve {
        uint32_t Count = 2;
        float T[kParticleMaxCurveKeys] = { 0.0f, 1.0f };   // 0..1, em ordem crescente
        float V[kParticleMaxCurveKeys] = { 1.0f, 1.0f };

        static ParticleCurve Constant(float value) {
            ParticleCurve c;
            c.V[0] = value;
            c.V[1] = value;
            return c;
        }

        // Valor em 't' (limitado a 0..1). Antes da primeira chave / depois da ultima: segura o valor.
        float Evaluate(float t) const {
            if (Count == 0)
                return 1.0f;
            const uint32_t count = std::min<uint32_t>(Count, kParticleMaxCurveKeys);
            if (!(t > 0.0f)) t = 0.0f;       // tambem trata NaN
            if (t > 1.0f) t = 1.0f;
            if (t <= T[0] || count == 1)
                return V[0];
            for (uint32_t i = 1; i < count; i++) {
                if (t <= T[i]) {
                    const float span = T[i] - T[i - 1];
                    if (!(span > 1e-6f))
                        return V[i];
                    const float k = (t - T[i - 1]) / span;
                    return V[i - 1] + (V[i] - V[i - 1]) * k;
                }
            }
            return V[count - 1];
        }

        // Insere uma chave mantendo a ordem. Devolve o indice, ou -1 se a curva esta cheia.
        int AddKey(float t, float value) {
            if (Count >= (uint32_t)kParticleMaxCurveKeys)
                return -1;
            t = std::min(std::max(std::isfinite(t) ? t : 0.0f, 0.0f), 1.0f);
            uint32_t at = Count;
            while (at > 0 && T[at - 1] > t)
                at--;
            for (uint32_t i = Count; i > at; i--) {
                T[i] = T[i - 1];
                V[i] = V[i - 1];
            }
            T[at] = t;
            V[at] = value;
            Count++;
            return (int)at;
        }

        // Remove a chave 'index'. Sempre sobram pelo menos 2.
        bool RemoveKey(int index) {
            if (Count <= 2 || index < 0 || (uint32_t)index >= Count)
                return false;
            for (uint32_t i = (uint32_t)index; i + 1 < Count; i++) {
                T[i] = T[i + 1];
                V[i] = V[i + 1];
            }
            Count--;
            return true;
        }

        // Corrige dados vindos de arquivo (ou de edicao): quantidade, NaN, faixa e ordem.
        void Sanitize(float minValue, float maxValue) {
            if (Count < 1) Count = 1;
            if (Count > (uint32_t)kParticleMaxCurveKeys) Count = kParticleMaxCurveKeys;
            for (uint32_t i = 0; i < Count; i++) {
                T[i] = std::min(std::max(std::isfinite(T[i]) ? T[i] : 0.0f, 0.0f), 1.0f);
                V[i] = std::min(std::max(std::isfinite(V[i]) ? V[i] : 1.0f, minValue), maxValue);
            }
            // Ordenacao por insercao (poucas chaves; estavel).
            for (uint32_t i = 1; i < Count; i++) {
                const float t = T[i], v = V[i];
                uint32_t j = i;
                while (j > 0 && T[j - 1] > t) {
                    T[j] = T[j - 1];
                    V[j] = V[j - 1];
                    j--;
                }
                T[j] = t;
                V[j] = v;
            }
            if (Count == 1) { // normaliza para 2 chaves (o editor sempre trabalha com >= 2)
                T[1] = 1.0f;
                V[1] = V[0];
                T[0] = 0.0f;
                Count = 2;
            }
        }
    };

    // --- Gradiente -----------------------------------------------------------

    struct ParticleGradient {
        uint32_t Count = 2;
        float T[kParticleMaxGradientStops] = { 0.0f, 1.0f };
        float C[kParticleMaxGradientStops][4] = { { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } };

        static ParticleGradient White() { return ParticleGradient{}; }

        // RGBA em 't' (limitado a 0..1), interpolacao linear. Sem paradas = branco opaco.
        void Evaluate(float t, float out[4]) const {
            const uint32_t count = std::min<uint32_t>(Count, kParticleMaxGradientStops);
            if (count == 0) {
                out[0] = out[1] = out[2] = out[3] = 1.0f;
                return;
            }
            if (!(t > 0.0f)) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            if (t <= T[0] || count == 1) {
                for (int k = 0; k < 4; k++) out[k] = C[0][k];
                return;
            }
            for (uint32_t i = 1; i < count; i++) {
                if (t <= T[i]) {
                    const float span = T[i] - T[i - 1];
                    const float f = span > 1e-6f ? (t - T[i - 1]) / span : 1.0f;
                    for (int k = 0; k < 4; k++)
                        out[k] = C[i - 1][k] + (C[i][k] - C[i - 1][k]) * f;
                    return;
                }
            }
            for (int k = 0; k < 4; k++) out[k] = C[count - 1][k];
        }

        // Insere uma parada em 't' com a cor atual do gradiente nesse ponto. Devolve o indice ou -1.
        int AddStop(float t) {
            if (Count >= (uint32_t)kParticleMaxGradientStops)
                return -1;
            t = std::min(std::max(std::isfinite(t) ? t : 0.0f, 0.0f), 1.0f);
            float color[4];
            Evaluate(t, color);
            uint32_t at = Count;
            while (at > 0 && T[at - 1] > t)
                at--;
            for (uint32_t i = Count; i > at; i--) {
                T[i] = T[i - 1];
                for (int k = 0; k < 4; k++) C[i][k] = C[i - 1][k];
            }
            T[at] = t;
            for (int k = 0; k < 4; k++) C[at][k] = color[k];
            Count++;
            return (int)at;
        }

        // Remove a parada 'index'. Sempre sobram pelo menos 2.
        bool RemoveStop(int index) {
            if (Count <= 2 || index < 0 || (uint32_t)index >= Count)
                return false;
            for (uint32_t i = (uint32_t)index; i + 1 < Count; i++) {
                T[i] = T[i + 1];
                for (int k = 0; k < 4; k++) C[i][k] = C[i + 1][k];
            }
            Count--;
            return true;
        }

        // Reordena apos arrastar uma parada; devolve o novo indice de 'index'.
        int SortKeepingIndex(int index) {
            while (index > 0 && T[index - 1] > T[index]) {
                std::swap(T[index - 1], T[index]);
                for (int k = 0; k < 4; k++) std::swap(C[index - 1][k], C[index][k]);
                index--;
            }
            while ((uint32_t)index + 1 < Count && T[index + 1] < T[index]) {
                std::swap(T[index + 1], T[index]);
                for (int k = 0; k < 4; k++) std::swap(C[index + 1][k], C[index][k]);
                index++;
            }
            return index;
        }

        void Sanitize() {
            if (Count < 1) Count = 1;
            if (Count > (uint32_t)kParticleMaxGradientStops) Count = kParticleMaxGradientStops;
            for (uint32_t i = 0; i < Count; i++) {
                T[i] = std::min(std::max(std::isfinite(T[i]) ? T[i] : 0.0f, 0.0f), 1.0f);
                for (int k = 0; k < 4; k++)
                    C[i][k] = std::min(std::max(std::isfinite(C[i][k]) ? C[i][k] : 1.0f, 0.0f), 4.0f); // ate 4: permite "HDR" leve com bloom
            }
            for (uint32_t i = 1; i < Count; i++) {
                int j = (int)i;
                while (j > 0 && T[j - 1] > T[j]) {
                    std::swap(T[j - 1], T[j]);
                    for (int k = 0; k < 4; k++) std::swap(C[j - 1][k], C[j][k]);
                    j--;
                }
            }
            if (Count == 1) {
                T[0] = 0.0f;
                T[1] = 1.0f;
                for (int k = 0; k < 4; k++) C[1][k] = C[0][k];
                Count = 2;
            }
        }
    };

    // --- Aleatorio -----------------------------------------------------------

    namespace ParticleMath {

        constexpr float kPi = 3.14159265358979f;

        // xorshift32: barato e deterministico por emissor (sem estado global).
        inline float Random01(uint32_t& seed) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return (float)(seed & 0xFFFFFF) / (float)0x1000000;
        }

        inline float RandomRange(uint32_t& seed, float a, float b) {
            if (b < a)
                std::swap(a, b);
            return a + (b - a) * Random01(seed);
        }

        // --- Formas de emissao -------------------------------------------------

        struct ShapeParams {
            ParticleShape Type = ParticleShape::Cone;
            float Radius = 0.5f;          // Sphere/Hemisphere/Circle: raio; Edge: meio-comprimento
            float Thickness = 1.0f;       // 0 = so a superficie/aro, 1 = volume/disco inteiro
            float Box[3] = { 1.0f, 1.0f, 1.0f }; // Box: tamanho total em X, Y, Z
            float SpreadAngleDeg = 25.0f; // meio-angulo do cone (Cone/Box/Circle/Edge)
            float RadialBlend = 0.0f;     // Box/Circle: 0 = direcao do cone, 1 = para fora do centro
        };

        struct ShapeSample {
            float Position[3] = { 0, 0, 0 }; // local ao emissor
            float Direction[3] = { 0, 1, 0 }; // unitaria
        };

        inline void Normalize3(float v[3], const float fallback[3]) {
            const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (length > 1e-6f) {
                v[0] /= length; v[1] /= length; v[2] /= length;
            } else {
                v[0] = fallback[0]; v[1] = fallback[1]; v[2] = fallback[2];
            }
        }

        // Direcao uniforme na calota esferica de meio-angulo 'spreadDeg' ao redor de +Y.
        inline void RandomConeDirection(uint32_t& seed, float spreadDeg, float out[3]) {
            const float spread = std::min(std::max(std::isfinite(spreadDeg) ? spreadDeg : 0.0f, 0.0f), 180.0f) * kPi / 180.0f;
            const float cosMax = std::cos(spread);
            // cos(theta) uniforme entre cosMax e 1 => distribuicao uniforme na calota.
            const float cosTheta = 1.0f - Random01(seed) * (1.0f - cosMax);
            const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
            const float phi = Random01(seed) * 2.0f * kPi;
            out[0] = std::cos(phi) * sinTheta;
            out[1] = cosTheta;
            out[2] = std::sin(phi) * sinTheta;
        }

        inline ShapeSample SampleShape(const ShapeParams& p, uint32_t& seed) {
            ShapeSample s;
            const float up[3] = { 0.0f, 1.0f, 0.0f };
            const float radius = std::max(std::isfinite(p.Radius) ? p.Radius : 0.0f, 0.0f);
            const float thickness = std::min(std::max(std::isfinite(p.Thickness) ? p.Thickness : 1.0f, 0.0f), 1.0f);
            const float inner = 1.0f - thickness; // raio interno relativo (0..1)

            switch (p.Type) {
            case ParticleShape::Sphere:
            case ParticleShape::Hemisphere: {
                // Direcao uniforme na esfera; raio uniforme em VOLUME dentro da casca [inner, 1].
                const float cosTheta = 1.0f - 2.0f * Random01(seed);
                const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                const float phi = Random01(seed) * 2.0f * kPi;
                float dir[3] = { std::cos(phi) * sinTheta, cosTheta, std::sin(phi) * sinTheta };
                if (p.Type == ParticleShape::Hemisphere && dir[1] < 0.0f)
                    dir[1] = -dir[1];
                const float u = Random01(seed);
                const float r3 = inner * inner * inner + (1.0f - inner * inner * inner) * u;
                const float r = std::cbrt(r3) * radius;
                for (int k = 0; k < 3; k++) {
                    s.Direction[k] = dir[k];
                    s.Position[k] = dir[k] * r;
                }
                break;
            }
            case ParticleShape::Box: {
                for (int k = 0; k < 3; k++) {
                    const float size = std::max(std::isfinite(p.Box[k]) ? p.Box[k] : 0.0f, 0.0f);
                    s.Position[k] = (Random01(seed) - 0.5f) * size;
                }
                RandomConeDirection(seed, p.SpreadAngleDeg, s.Direction);
                break;
            }
            case ParticleShape::Circle: {
                // Raio uniforme em AREA dentro do anel [inner, 1].
                const float phi = Random01(seed) * 2.0f * kPi;
                const float u = Random01(seed);
                const float r = std::sqrt(inner * inner + (1.0f - inner * inner) * u) * radius;
                s.Position[0] = std::cos(phi) * r;
                s.Position[1] = 0.0f;
                s.Position[2] = std::sin(phi) * r;
                RandomConeDirection(seed, p.SpreadAngleDeg, s.Direction);
                break;
            }
            case ParticleShape::Edge: {
                s.Position[0] = (Random01(seed) * 2.0f - 1.0f) * radius;
                RandomConeDirection(seed, p.SpreadAngleDeg, s.Direction);
                break;
            }
            case ParticleShape::Cone:
            default:
                RandomConeDirection(seed, p.SpreadAngleDeg, s.Direction);
                break;
            }

            // Mistura com a direcao "para fora do centro" (so Box e Circle).
            if ((p.Type == ParticleShape::Box || p.Type == ParticleShape::Circle) && p.RadialBlend > 0.0f) {
                const float blend = std::min(p.RadialBlend, 1.0f);
                float radial[3] = { s.Position[0], p.Type == ParticleShape::Circle ? 0.0f : s.Position[1], s.Position[2] };
                Normalize3(radial, s.Direction);
                for (int k = 0; k < 3; k++)
                    s.Direction[k] = s.Direction[k] * (1.0f - blend) + radial[k] * blend;
            }
            Normalize3(s.Direction, up);
            return s;
        }

        // --- Folha de sprites ----------------------------------------------------

        // Indice do quadro (0 = canto superior esquerdo da folha, em ordem de leitura) para uma
        // particula. 'lifeT' = idade/vida (0..1); 'ageSeconds' = idade; 'random01' = valor fixo
        // da particula. Folha 1x1 (ou invalida) devolve 0.
        inline uint32_t FlipbookFrame(ParticleFlipbookMode mode, uint32_t columns, uint32_t rows,
                                      float lifeT, float ageSeconds, float fps, float random01) {
            const uint32_t total = std::max(columns, 1u) * std::max(rows, 1u);
            if (total <= 1)
                return 0;
            uint32_t frame = 0;
            switch (mode) {
            case ParticleFlipbookMode::FixedFPS: {
                const float f = std::max(std::isfinite(fps) ? fps : 0.0f, 0.0f) * std::max(ageSeconds, 0.0f);
                frame = (uint32_t)f % total;
                break;
            }
            case ParticleFlipbookMode::RandomStatic:
                frame = std::min<uint32_t>((uint32_t)(std::min(std::max(random01, 0.0f), 1.0f) * (float)total), total - 1);
                break;
            case ParticleFlipbookMode::OverLifetime:
            default:
                frame = std::min<uint32_t>((uint32_t)(std::min(std::max(lifeT, 0.0f), 1.0f) * (float)total), total - 1);
                break;
            }
            return frame;
        }

    }

}
