#pragma once

// ============================================================================
// Profiler.h
// Medidor de desempenho LEVE e so com a std (testavel sem a engine):
//   - zonas de CPU:   PRISM_PROFILE_SCOPE("Nome");   (soma as chamadas do quadro)
//   - zonas de GPU:   Profiler::SetGpuZone("Nome", ms)  (quem mede e Renderer/GpuTimer)
//   - contadores:     Profiler::Count(Profiler::Counter::DrawCalls, 1)
//   - historico:      media/maximo por zona e tempo de quadro dos ultimos quadros
//
// Uso: Profiler::BeginFrame() / EndFrame() uma vez por quadro (Application::Run).
// DESLIGADO por padrao (Profiler::SetEnabled): com ele desligado cada zona custa
// so a leitura de um bool. So a thread principal mede (nao ha trava).
//
// Nomes de zona devem ser LITERAIS (o ponteiro e guardado, nao copiado).
// ============================================================================

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>

namespace Prism {

    class Profiler {
    public:
        static constexpr int kMaxZones = 48;
        static constexpr int kHistory = 240;   // quadros guardados para o grafico e as medias

        enum class Counter : int {
            DrawCalls = 0,     // chamadas de desenho da cena (passe principal + pre-passe)
            Triangles,         // triangulos enviados nessas chamadas
            ShadowDrawCalls,   // chamadas de desenho dos mapas de sombra
            ShadowTriangles,
            InstancedDraws,    // chamadas de desenho instanciadas (parte de DrawCalls)
            InstancedObjects,  // objetos desenhados por elas
            Count_
        };
        static constexpr int kCounterCount = (int)Counter::Count_;

        struct ZoneStat {
            const char* Name = nullptr;
            float LastMs = 0.0f;     // soma do ultimo quadro
            float AvgMs = 0.0f;      // media movel (EMA) por quadro
            float MaxMs = 0.0f;      // maximo nos ultimos kHistory quadros
            uint32_t LastCalls = 0;  // chamadas no ultimo quadro
            bool Gpu = false;
        };

        // --- controle ------------------------------------------------------
        static void SetEnabled(bool enabled) {
            S().Enabled = enabled;
            if (!enabled) Reset();
        }
        static bool IsEnabled() { return S().Enabled; }

        // Relogio injetavel (so para testes): devolve segundos.
        static void SetClockForTesting(std::function<double()> clock) { S().Clock = std::move(clock); }

        static void Reset() {
            State& s = S();
            s.ZoneCount = 0;
            s.Zones = {};
            s.Frame = {};
            s.Counters = {};
            s.Current = {};
            s.History = {};
            s.HistoryCount = 0;
            s.HistoryHead = 0;
            s.FrameIndex = 0;
            s.InFrame = false;
        }

        // --- quadro --------------------------------------------------------
        static void BeginFrame() {
            State& s = S();
            if (!s.Enabled) return;
            for (int i = 0; i < s.ZoneCount; i++) { s.Acc[i] = 0.0f; s.Calls[i] = 0; }
            s.Current = {};
            s.FrameStart = Now();
            s.InFrame = true;
        }

        static void EndFrame() {
            State& s = S();
            if (!s.Enabled || !s.InFrame) return;
            s.InFrame = false;
            const float frameMs = (float)((Now() - s.FrameStart) * 1000.0);
            s.Frame.LastMs = frameMs;
            s.Frame.LastCalls = 1;
            s.Frame.AvgMs = s.FrameIndex == 0 ? frameMs : s.Frame.AvgMs + (frameMs - s.Frame.AvgMs) * kSmoothing;

            s.History[(size_t)s.HistoryHead] = frameMs;
            s.HistoryHead = (s.HistoryHead + 1) % kHistory;
            if (s.HistoryCount < kHistory) s.HistoryCount++;
            float maxFrame = 0.0f;
            for (int i = 0; i < s.HistoryCount; i++)
                maxFrame = s.History[(size_t)i] > maxFrame ? s.History[(size_t)i] : maxFrame;
            s.Frame.MaxMs = maxFrame;

            for (int i = 0; i < s.ZoneCount; i++) {
                ZoneStat& z = s.Zones[(size_t)i];
                z.LastMs = s.Acc[i];
                z.LastCalls = s.Calls[i];
                z.AvgMs = s.FrameIndex == 0 ? z.LastMs : z.AvgMs + (z.LastMs - z.AvgMs) * kSmoothing;
                // maximo "recente": decai devagar para um pico antigo nao ficar para sempre
                z.MaxMs = z.LastMs > z.MaxMs ? z.LastMs : z.MaxMs * kMaxDecay;
            }
            s.Counters = s.Current;
            s.FrameIndex++;
        }

        // --- zonas ---------------------------------------------------------
        static void AddZone(const char* name, double seconds, bool gpu = false) {
            State& s = S();
            if (!s.Enabled || !s.InFrame || !name) return;
            const int i = FindOrCreate(s, name, gpu);
            if (i < 0) return;
            s.Acc[i] += (float)(seconds * 1000.0);
            s.Calls[i]++;
        }

        // Tempo de GPU medido por fora (ms de um quadro anterior); vale como uma "chamada".
        static void SetGpuZone(const char* name, float ms) {
            AddZone(name, (double)ms / 1000.0, true);
        }

        class Scope {
        public:
            explicit Scope(const char* name) : m_Name(name), m_Active(S().Enabled && S().InFrame) {
                if (m_Active) m_Start = Now();
            }
            ~Scope() { Stop(); }
            // Encerra a medicao antes do fim do bloco (idempotente).
            void Stop() { if (m_Active) { m_Active = false; AddZone(m_Name, Now() - m_Start); } }
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;
        private:
            const char* m_Name;
            bool m_Active;
            double m_Start = 0.0;
        };

        // --- contadores ----------------------------------------------------
        static void Count(Counter c, uint32_t amount = 1) {
            State& s = S();
            if (!s.Enabled) return;
            s.Current[(size_t)c] += amount;
        }
        // Desenho de uma malha com 'indexCount' indices.
        // 'instances' > 1: UMA chamada instanciada desenhando varias copias da malha.
        static void CountDraw(uint32_t indexCount, uint32_t instances = 1) {
            Count(Counter::DrawCalls);
            Count(Counter::Triangles, (indexCount / 3) * instances);
            if (instances > 1) { Count(Counter::InstancedDraws); Count(Counter::InstancedObjects, instances); }
        }
        static void CountShadowDraw(uint32_t indexCount) { Count(Counter::ShadowDrawCalls); Count(Counter::ShadowTriangles, indexCount / 3); }

        // --- leitura (para o painel) ----------------------------------------
        static const ZoneStat& Frame() { return S().Frame; }
        static int ZoneCount() { return S().ZoneCount; }
        static const ZoneStat& Zone(int i) { return S().Zones[(size_t)i]; }
        static uint32_t CounterValue(Counter c) { return S().Counters[(size_t)c]; } // ultimo quadro completo
        static uint64_t FrameCount() { return S().FrameIndex; }
        static int HistoryCount() { return S().HistoryCount; }
        // i = 0 e o quadro mais ANTIGO guardado.
        static float HistoryMs(int i) {
            State& s = S();
            if (i < 0 || i >= s.HistoryCount) return 0.0f;
            const int start = s.HistoryCount < kHistory ? 0 : s.HistoryHead;
            return s.History[(size_t)((start + i) % kHistory)];
        }

    private:
        static constexpr float kSmoothing = 0.1f;
        static constexpr float kMaxDecay = 0.995f;

        struct State {
            bool Enabled = false;
            bool InFrame = false;
            std::function<double()> Clock;
            double FrameStart = 0.0;
            uint64_t FrameIndex = 0;
            int ZoneCount = 0;
            std::array<ZoneStat, kMaxZones> Zones{};
            std::array<float, kMaxZones> Acc{};
            std::array<uint32_t, kMaxZones> Calls{};
            ZoneStat Frame{};
            std::array<uint32_t, kCounterCount> Current{};
            std::array<uint32_t, kCounterCount> Counters{};
            std::array<float, kHistory> History{};
            int HistoryCount = 0;
            int HistoryHead = 0;
        };

        static State& S() { static State s; return s; }

        static double Now() {
            State& s = S();
            if (s.Clock) return s.Clock();
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        static int FindOrCreate(State& s, const char* name, bool gpu) {
            for (int i = 0; i < s.ZoneCount; i++) {
                const char* n = s.Zones[(size_t)i].Name;
                if (n == name || std::strcmp(n, name) == 0) return i;
            }
            if (s.ZoneCount >= kMaxZones) return -1;
            const int i = s.ZoneCount++;
            s.Zones[(size_t)i] = ZoneStat{};
            s.Zones[(size_t)i].Name = name;
            s.Zones[(size_t)i].Gpu = gpu;
            s.Acc[(size_t)i] = 0.0f;
            s.Calls[(size_t)i] = 0;
            return i;
        }
    };

}

#define PRISM_PROFILE_CONCAT2(a, b) a##b
#define PRISM_PROFILE_CONCAT(a, b) PRISM_PROFILE_CONCAT2(a, b)
// Mede o resto do bloco atual como a zona 'name' (literal).
#define PRISM_PROFILE_SCOPE(name) ::Prism::Profiler::Scope PRISM_PROFILE_CONCAT(_prismProfile, __LINE__)(name)
