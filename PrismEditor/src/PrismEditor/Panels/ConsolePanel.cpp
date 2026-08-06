#include "ConsolePanel.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>

namespace PrismEditor {

    // Cores por nivel - usadas no texto de cada linha.
    static ImVec4 LevelColor(Prism::LogLevel level) {
        switch (level) {
            case Prism::LogLevel::Trace:    return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
            case Prism::LogLevel::Info:     return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
            case Prism::LogLevel::Warn:     return ImVec4(0.95f, 0.75f, 0.20f, 1.0f);
            case Prism::LogLevel::Error:    return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
            case Prism::LogLevel::Critical: return ImVec4(1.00f, 0.20f, 0.20f, 1.0f);
        }
        return ImVec4(1, 1, 1, 1);
    }

    static const char* LevelLabel(Prism::LogLevel level) {
        switch (level) {
            case Prism::LogLevel::Trace:    return "TRACE";
            case Prism::LogLevel::Info:     return "INFO";
            case Prism::LogLevel::Warn:     return "WARN";
            case Prism::LogLevel::Error:    return "ERROR";
            case Prism::LogLevel::Critical: return "CRIT";
        }
        return "????";
    }

    // Busca 'needle' dentro de 'haystack' ignorando maiusculas/minusculas -
    // filtro de texto do console deve funcionar sem o usuario se preocupar
    // com case.
    static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
        if (needle.empty())
            return true;
        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
        return it != haystack.end();
    }

    void ConsolePanel::OnImGuiRender() {
        ImGui::Begin("Console");

        // --- barra de ferramentas -------------------------------------
        if (ImGui::Button("Limpar")) {
            Prism::LogBuffer::Clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Trace", &m_ShowTrace);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_ShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warn", &m_ShowWarn);
        ImGui::SameLine();
        // Error engloba Error + Critical - separar um toggle so para
        // Critical seria granularidade demais para o que e, na pratica,
        // uma variante rara e sempre-quero-ver de Error.
        ImGui::Checkbox("Error", &m_ShowError);

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ConsoleFilter", "Filtrar por texto...", m_FilterBuffer, sizeof(m_FilterBuffer));

        ImGui::Separator();

        // --- lista de mensagens -----------------------------------------
        ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        const auto& entries = Prism::LogBuffer::GetEntries();
        std::string filter = m_FilterBuffer;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
        for (const auto& entry : entries) {
            bool levelVisible =
                (entry.Level == Prism::LogLevel::Trace && m_ShowTrace) ||
                (entry.Level == Prism::LogLevel::Info && m_ShowInfo) ||
                (entry.Level == Prism::LogLevel::Warn && m_ShowWarn) ||
                ((entry.Level == Prism::LogLevel::Error || entry.Level == Prism::LogLevel::Critical) && m_ShowError);

            if (!levelVisible)
                continue;
            if (!ContainsCaseInsensitive(entry.Message, filter))
                continue;

            ImVec4 color = LevelColor(entry.Level);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            // [PRISM]/[APP] + nivel + mensagem, tudo em uma linha - formato
            // parecido com o que ja vai pro stdout (ver Log.h), so que
            // colorido e filtravel.
            ImGui::TextUnformatted(("[" + entry.Scope + "] [" + LevelLabel(entry.Level) + "] " + entry.Message).c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();

        // Auto-scroll: so desce automaticamente se ja estavamos perto do
        // fim (ScrollY >= ScrollMaxY - uma margem pequena) - assim, se o
        // usuario rolar para cima para ler algo antigo, novas mensagens
        // nao "puxam" a tela de volta para baixo embaixo dele.
        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();

        ImGui::End();
    }

}
