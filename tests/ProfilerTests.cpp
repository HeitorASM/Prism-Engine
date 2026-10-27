// ProfilerTests.cpp - Core/Profiler.h (so std, relogio falso para ser deterministico).
#include <Prism/Core/Profiler.h>
#include <cstdio>
#include <cmath>
#include <string>

using Prism::Profiler;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static double g_now = 0.0;
static void Tick(double seconds) { g_now += seconds; }

static const Profiler::ZoneStat* Find(const char* name) {
    for (int i = 0; i < Profiler::ZoneCount(); i++)
        if (std::string(Profiler::Zone(i).Name) == name) return &Profiler::Zone(i);
    return nullptr;
}

int main() {
    Profiler::SetClockForTesting([] { return g_now; });

    // desligado: nada e medido
    Profiler::Reset();
    Profiler::SetEnabled(false);
    Profiler::BeginFrame();
    { PRISM_PROFILE_SCOPE("X"); Tick(0.01); }
    Profiler::Count(Profiler::Counter::DrawCalls, 5);
    Profiler::EndFrame();
    CHECK(Profiler::ZoneCount() == 0 && Profiler::FrameCount() == 0 && Profiler::CounterValue(Profiler::Counter::DrawCalls) == 0);

    // um quadro: zona soma chamadas repetidas e aninhadas
    Profiler::SetEnabled(true);
    Profiler::BeginFrame();
    {
        PRISM_PROFILE_SCOPE("Outer");
        Tick(0.002);
        { PRISM_PROFILE_SCOPE("Inner"); Tick(0.003); }
        { PRISM_PROFILE_SCOPE("Inner"); Tick(0.001); }
    }
    Tick(0.004); // fora de qualquer zona
    Profiler::CountDraw(300);
    Profiler::CountDraw(30);
    Profiler::CountShadowDraw(90);
    Profiler::EndFrame();

    CHECK(Profiler::FrameCount() == 1);
    CHECK(Near(Profiler::Frame().LastMs, 10.0f));
    const auto* outer = Find("Outer");
    const auto* inner = Find("Inner");
    CHECK(outer && inner);
    if (outer && inner) {
        CHECK(Near(outer->LastMs, 6.0f) && outer->LastCalls == 1);
        CHECK(Near(inner->LastMs, 4.0f) && inner->LastCalls == 2);
        CHECK(!outer->Gpu);
    }
    CHECK(Profiler::CounterValue(Profiler::Counter::DrawCalls) == 2);
    CHECK(Profiler::CounterValue(Profiler::Counter::Triangles) == 110);
    CHECK(Profiler::CounterValue(Profiler::Counter::ShadowDrawCalls) == 1);
    CHECK(Profiler::CounterValue(Profiler::Counter::ShadowTriangles) == 30);

    // zona de GPU
    Profiler::BeginFrame();
    Profiler::SetGpuZone("GPU.Main", 2.5f);
    Tick(0.005);
    Profiler::EndFrame();
    const auto* gpu = Find("GPU.Main");
    CHECK(gpu && gpu->Gpu && Near(gpu->LastMs, 2.5f) && gpu->LastCalls == 1);
    // contadores zeram a cada quadro
    CHECK(Profiler::CounterValue(Profiler::Counter::DrawCalls) == 0);
    // zona que nao rodou neste quadro volta a 0
    CHECK(Find("Outer") && Find("Outer")->LastMs == 0.0f && Find("Outer")->LastCalls == 0);

    // media e maximo
    Profiler::Reset();
    for (int i = 0; i < 50; i++) {
        Profiler::BeginFrame();
        { PRISM_PROFILE_SCOPE("A"); Tick(i == 25 ? 0.040 : 0.010); }
        Profiler::EndFrame();
    }
    const auto* a = Find("A");
    CHECK(a && Near(a->LastMs, 10.0f));
    CHECK(a && a->MaxMs > 30.0f);          // o pico de 40 ms ainda aparece (decaindo devagar)
    CHECK(a && a->AvgMs > 9.0f && a->AvgMs < 14.0f);
    CHECK(Profiler::Frame().MaxMs > 39.0f && Near(Profiler::Frame().LastMs, 10.0f));

    // historico: anel com kHistory entradas, i=0 e o mais antigo
    Profiler::Reset();
    for (int i = 0; i < Profiler::kHistory + 20; i++) {
        Profiler::BeginFrame();
        Tick(0.001 * (i + 1));
        Profiler::EndFrame();
    }
    CHECK(Profiler::HistoryCount() == Profiler::kHistory);
    CHECK(Near(Profiler::HistoryMs(0), 21.0f));                      // quadro 21 (indice 20) e o mais antigo
    CHECK(Near(Profiler::HistoryMs(Profiler::kHistory - 1), (float)(Profiler::kHistory + 20)));
    CHECK(Profiler::HistoryMs(-1) == 0.0f && Profiler::HistoryMs(Profiler::kHistory) == 0.0f);

    // mais zonas que o limite: ignora as extras, sem crash
    Profiler::Reset();
    static const char* names[Profiler::kMaxZones + 8] = {};
    static char storage[Profiler::kMaxZones + 8][8];
    Profiler::BeginFrame();
    for (int i = 0; i < Profiler::kMaxZones + 8; i++) {
        std::snprintf(storage[i], sizeof(storage[i]), "z%d", i);
        names[i] = storage[i];
        Profiler::AddZone(names[i], 0.001);
    }
    Profiler::EndFrame();
    CHECK(Profiler::ZoneCount() == Profiler::kMaxZones);

    // zona fora de BeginFrame/EndFrame e ignorada
    Profiler::Reset();
    Profiler::AddZone("fora", 1.0);
    CHECK(Profiler::ZoneCount() == 0);

    // desligar zera tudo
    Profiler::BeginFrame(); Profiler::AddZone("B", 0.001); Profiler::EndFrame();
    CHECK(Profiler::ZoneCount() == 1);
    Profiler::SetEnabled(false);
    CHECK(Profiler::ZoneCount() == 0 && Profiler::HistoryCount() == 0 && Profiler::FrameCount() == 0);

    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
