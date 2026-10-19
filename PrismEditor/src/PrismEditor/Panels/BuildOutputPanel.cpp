#include "BuildOutputPanel.h"
#include "../Core/FileDialog.h"

#include <imgui.h>

#include <cstdlib>
#include <regex>
#include <string>

namespace PrismEditor {

    namespace {

        const ImVec4 kErrorColor(0.95f, 0.35f, 0.35f, 1.0f);
        const ImVec4 kWarningColor(0.95f, 0.75f, 0.20f, 1.0f);
        const ImVec4 kNormalColor(0.85f, 0.85f, 0.85f, 1.0f);
        const ImVec4 kSuccessColor(0.45f, 0.85f, 0.50f, 1.0f);

        // MSVC:     C:\proj\Source\Foo.cpp(12,5): error C2065: ...      (a coluna e opcional)
        const std::regex& MsvcDiagnostic() {
            static const std::regex pattern(R"(^\s*(.+?)\((\d+)(?:,\d+)?\)\s*:\s*(fatal error|error|warning)\b)");
            return pattern;
        }

        // GCC/Clang: /proj/Source/Foo.cpp:12:5: error: ...               (a coluna e opcional)
        const std::regex& GccDiagnostic() {
            static const std::regex pattern(R"(^\s*(.+?):(\d+):(?:\d+:)?\s*(fatal error|error|warning)\b)");
            return pattern;
        }

        bool Contains(const std::string& text, const char* needle) {
            return text.find(needle) != std::string::npos;
        }

    }

    BuildOutputPanel::Entry BuildOutputPanel::Classify(const std::string& text) {
        Entry entry;
        entry.Text = text;

        std::smatch match;
        if (std::regex_search(text, match, MsvcDiagnostic()) || std::regex_search(text, match, GccDiagnostic())) {
            entry.File = FileDialog::FromUtf8(match[1].str());
            entry.Line = std::atoi(match[2].str().c_str());
            entry.Level = match[3].str() == "warning" ? Severity::Warning : Severity::Error;
            return entry;
        }

        // Erros sem arquivo: CMake, linker, ninja/make.
        if (Contains(text, "CMake Error") || Contains(text, "error LNK") || Contains(text, "LINK : fatal error")
            || Contains(text, "ld returned") || Contains(text, "undefined reference") || Contains(text, "ninja: build stopped")
            || Contains(text, "gmake") || Contains(text, "fatal error:") || Contains(text, "Nao foi possivel"))
            entry.Level = Severity::Error;
        else if (Contains(text, "CMake Warning"))
            entry.Level = Severity::Warning;

        return entry;
    }

    std::filesystem::path BuildOutputPanel::ResolveFile(const std::filesystem::path& file) const {
        if (file.empty())
            return {};
        std::error_code ec;
        if (file.is_absolute())
            return std::filesystem::exists(file, ec) ? file : std::filesystem::path{};
        std::filesystem::path candidate = m_BaseDirectory / file;
        return std::filesystem::exists(candidate, ec) ? candidate : std::filesystem::path{};
    }

    void BuildOutputPanel::Clear() {
        m_Entries.clear();
        m_ErrorCount = 0;
        m_WarningCount = 0;
        m_HasResult = false;
    }

    void BuildOutputPanel::BeginBuild() {
        Clear();
        m_Building = true;
        m_FocusRequested = true;
    }

    void BuildOutputPanel::AddLine(const std::string& text) {
        Entry entry = Classify(text);
        if (entry.Level == Severity::Error)
            m_ErrorCount++;
        else if (entry.Level == Severity::Warning)
            m_WarningCount++;
        m_Entries.push_back(std::move(entry));
    }

    void BuildOutputPanel::EndBuild(bool success) {
        m_Building = false;
        m_HasResult = true;
        m_LastSuccess = success;
        if (!success)
            m_FocusRequested = true;
    }

    void BuildOutputPanel::OnImGuiRender() {
        if (m_FocusRequested) {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        ImGui::Begin("Saida de Build###BuildOutputPanel");

        if (ImGui::Button("Limpar"))
            Clear();
        ImGui::SameLine();
        ImGui::Checkbox("So erros e avisos", &m_OnlyProblems);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (m_Building) {
            ImGui::TextColored(kWarningColor, "Compilando...");
        }
        else if (m_HasResult) {
            if (m_LastSuccess)
                ImGui::TextColored(kSuccessColor, "Sucesso (%d aviso(s))", m_WarningCount);
            else
                ImGui::TextColored(kErrorColor, "Falhou: %d erro(s), %d aviso(s)", m_ErrorCount, m_WarningCount);
        }
        else {
            ImGui::TextDisabled("Nenhuma compilacao ainda.");
        }

        ImGui::Separator();
        ImGui::BeginChild("##BuildOutputScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        std::vector<int> visible;
        visible.reserve(m_Entries.size());
        for (int i = 0; i < (int)m_Entries.size(); i++) {
            if (m_OnlyProblems && m_Entries[i].Level == Severity::Normal)
                continue;
            visible.push_back(i);
        }

        // Todas as linhas tem a mesma altura: o clipper desenha so as visiveis
        // (saidas de build chegam a milhares de linhas).
        ImGuiListClipper clipper;
        clipper.Begin((int)visible.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const int index = visible[row];
                const Entry& entry = m_Entries[index];

                const ImVec4& color = entry.Level == Severity::Error ? kErrorColor
                    : entry.Level == Severity::Warning ? kWarningColor : kNormalColor;
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::PushID(index);

                std::filesystem::path resolved = ResolveFile(entry.File);
                if (!resolved.empty()) {
                    if (ImGui::Selectable(entry.Text.c_str(), false) && m_OnOpenFile)
                        m_OnOpenFile(resolved);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Abrir %s (linha %d)", FileDialog::ToUtf8(resolved.filename()).c_str(), entry.Line);
                }
                else {
                    ImGui::TextUnformatted(entry.Text.c_str());
                }

                if (ImGui::BeginPopupContextItem("##lineContext")) {
                    if (ImGui::MenuItem("Copiar linha"))
                        ImGui::SetClipboardText(entry.Text.c_str());
                    ImGui::EndPopup();
                }

                ImGui::PopID();
                ImGui::PopStyleColor();
            }
        }

        if (m_AutoScroll && m_Building)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
    }

}
