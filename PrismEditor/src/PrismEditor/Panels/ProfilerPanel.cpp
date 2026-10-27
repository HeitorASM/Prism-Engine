#include "ProfilerPanel.h"

#include <Prism/Core/Profiler.h>
#include <Prism/Renderer/Renderer.h>

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace PrismEditor {

    namespace {
        // Cor do tempo: verde ate ~16.6 ms (60 FPS), amarelo ate ~33 ms, vermelho acima.
        ImVec4 MsColor(float ms) {
            if (ms <= 16.7f) return ImVec4(0.45f, 0.9f, 0.5f, 1.0f);
            if (ms <= 33.4f) return ImVec4(0.95f, 0.8f, 0.3f, 1.0f);
            return ImVec4(0.95f, 0.4f, 0.4f, 1.0f);
        }

        void DrawZoneTable(const char* id, bool gpu) {
            using Prism::Profiler;
            if (!ImGui::BeginTable(id, 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
                return;
            ImGui::TableSetupColumn("Zona", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Ultimo", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Media", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Chamadas", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();

            // Ordena por media (a mais cara primeiro).
            std::vector<int> order;
            for (int i = 0; i < Profiler::ZoneCount(); i++)
                if (Profiler::Zone(i).Gpu == gpu)
                    order.push_back(i);
            std::sort(order.begin(), order.end(), [](int a, int b) { return Profiler::Zone(a).AvgMs > Profiler::Zone(b).AvgMs; });

            for (int i : order) {
                const Profiler::ZoneStat& z = Profiler::Zone(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(z.Name);
                ImGui::TableNextColumn(); ImGui::Text("%.2f ms", z.LastMs);
                ImGui::TableNextColumn(); ImGui::Text("%.2f ms", z.AvgMs);
                ImGui::TableNextColumn(); ImGui::Text("%.2f ms", z.MaxMs);
                ImGui::TableNextColumn(); ImGui::Text("%u", z.LastCalls);
            }
            ImGui::EndTable();
        }
    }

    void ProfilerPanel::OnImGuiRender() {
        using Prism::Profiler;

        // Liga/desliga o Profiler conforme o painel esta aberto.
        if (m_Open && !Profiler::IsEnabled()) {
            Profiler::SetEnabled(true);
            m_WasEnabledByUs = true;
        }
        if (!m_Open) {
            if (m_WasEnabledByUs) {
                Profiler::SetEnabled(false);
                m_WasEnabledByUs = false;
            }
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(520, 560), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Desempenho", &m_Open)) {
            ImGui::End();
            return;
        }

        const Profiler::ZoneStat& frame = Profiler::Frame();
        const float avg = frame.AvgMs;
        ImGui::TextColored(MsColor(avg), "%.1f FPS   (%.2f ms medio, %.2f ms ultimo, %.2f ms pior recente)",
            avg > 0.0f ? 1000.0f / avg : 0.0f, avg, frame.LastMs, frame.MaxMs);
        ImGui::Checkbox("Pausar leitura", &m_Paused);
        ImGui::SameLine();
        ImGui::TextDisabled("(o tempo de quadro inclui a espera do V-Sync em \"Window.Swap\")");

        // Grafico do tempo de quadro.
        static float plot[Profiler::kHistory];
        const int count = Profiler::HistoryCount();
        for (int i = 0; i < count; i++)
            plot[i] = Profiler::HistoryMs(i);
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "%.1f ms", frame.LastMs);
        ImGui::PlotLines("##frame", plot, count, 0, overlay, 0.0f, std::max(33.4f, frame.MaxMs), ImVec2(-1, 70));

        ImGui::SeparatorText("Contadores (ultimo quadro)");
        using C = Profiler::Counter;
        ImGui::Text("Desenhos: %u   Triangulos: %u", Profiler::CounterValue(C::DrawCalls), Profiler::CounterValue(C::Triangles));
        ImGui::Text("Sombras: %u desenhos, %u triangulos", Profiler::CounterValue(C::ShadowDrawCalls), Profiler::CounterValue(C::ShadowTriangles));
        const Prism::Renderer::CullStats& cull = Prism::Renderer::GetCullStats();
        ImGui::Text("Malhas: %u desenhadas, %u fora do frustum, %u em camadas escondidas", cull.Drawn, cull.FrustumCulled, cull.LayerHidden);

        ImGui::SeparatorText("CPU");
        DrawZoneTable("##cpu", false);
        ImGui::SeparatorText("GPU (com ~3 quadros de atraso)");
        DrawZoneTable("##gpu", true);
        ImGui::TextDisabled("As zonas aninhadas se sobrepoem (ex.: Renderer.Shadows esta dentro de Renderer.DrawScene).");

        ImGui::End();
    }

}
