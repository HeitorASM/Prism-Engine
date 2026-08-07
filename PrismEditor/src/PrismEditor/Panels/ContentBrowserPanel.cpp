#include "ContentBrowserPanel.h"
#include <imgui.h>
#include <algorithm>

namespace PrismEditor {

    void ContentBrowserPanel::ResetToProjectRoot() {
        auto project = Prism::Project::GetActive();
        if (!project) {
            m_CurrentDirectory.clear();
            m_CachedEntries.clear();
            return;
        }
        NavigateTo(project->GetProjectDirectory());
    }

    void ContentBrowserPanel::NavigateTo(const std::filesystem::path& directory) {
        m_CurrentDirectory = directory;
        m_SelectedPath.clear();
        RefreshEntries();
    }

    void ContentBrowserPanel::RefreshEntries() {
        m_CachedEntries.clear();

        std::error_code ec;
        if (m_CurrentDirectory.empty() || !std::filesystem::exists(m_CurrentDirectory, ec))
            return;

        for (const auto& dirEntry : std::filesystem::directory_iterator(m_CurrentDirectory, ec)) {
            if (ec) break;

            Entry entry;
            entry.Path = dirEntry.path();
            entry.Name = dirEntry.path().filename().string();
            entry.IsDirectory = dirEntry.is_directory();
            m_CachedEntries.push_back(std::move(entry));
        }

        // Pastas primeiro, depois arquivos - dentro de cada grupo, ordem
        // alfabetica (case-insensitive nao vale a pena aqui, nomes de
        // pasta/arquivo do projeto ja seguem uma convencao previsivel).
        std::sort(m_CachedEntries.begin(), m_CachedEntries.end(), [](const Entry& a, const Entry& b) {
            if (a.IsDirectory != b.IsDirectory)
                return a.IsDirectory > b.IsDirectory;
            return a.Name < b.Name;
        });
    }

    void ContentBrowserPanel::OnImGuiRender() {
        ImGui::Begin("Conteudo do Projeto");

        auto project = Prism::Project::GetActive();
        if (!project) {
            ImGui::TextDisabled("Nenhum projeto ativo.");
            ImGui::End();
            return;
        }

        // Primeira vez que este painel roda com um projeto ativo (ou depois
        // de ResetToProjectRoot nao ter sido chamado ainda por algum motivo)
        // - navega para a raiz automaticamente em vez de mostrar vazio.
        if (m_CurrentDirectory.empty())
            NavigateTo(project->GetProjectDirectory());

        RenderToolbar();
        ImGui::Separator();
        RenderGrid();

        ImGui::End();
    }

    void ContentBrowserPanel::RenderToolbar() {
        auto project = Prism::Project::GetActive();

        // Comparacao via caminhos canonicalizados (resolve diferenças de
        // barra/trailing slash entre o que veio de directory_iterator e o
        // que Project::GetProjectDirectory() retorna) - comparar
        // std::filesystem::path diretamente com == e sensivel a essas
        // diferencas de forma textual mesmo quando os caminhos apontam para
        // o mesmo lugar no disco.
        std::error_code ec;
        bool atRoot = std::filesystem::equivalent(m_CurrentDirectory, project->GetProjectDirectory(), ec) && !ec;

        ImGui::BeginDisabled(atRoot);
        if (ImGui::Button("< Voltar")) {
            NavigateTo(m_CurrentDirectory.parent_path());
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Atualizar")) {
            RefreshEntries();
        }

        ImGui::SameLine();

        // Breadcrumb: caminho relativo a raiz do projeto, com "/" como
        // separador visual - mais curto e legivel que o caminho absoluto
        // inteiro, que pode ser bem longo (C:\dev\...\MeuProjeto\Assets\Models).
        std::filesystem::path relative = std::filesystem::relative(m_CurrentDirectory, project->GetProjectDirectory());
        std::string breadcrumb = (relative == ".") ? project->GetConfig().Name : (project->GetConfig().Name + " / " + relative.generic_string());
        ImGui::TextDisabled("%s", breadcrumb.c_str());
    }

    void ContentBrowserPanel::RenderGrid() {
        // Grid simples de botoes quadrados com quebra de linha automatica -
        // ImGui nao tem um "flow layout" pronto, entao calculamos manualmente
        // quantas colunas cabem na largura disponivel do painel.
        constexpr float cellSize = 84.0f;
        constexpr float cellPadding = 8.0f;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, (int)(panelWidth / (cellSize + cellPadding)));

        if (m_CachedEntries.empty()) {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::TextDisabled("Esta pasta esta vazia.");
            return;
        }

        ImGui::Columns(columns, nullptr, false);

        for (const auto& entry : m_CachedEntries) {
            ImGui::PushID(entry.Path.string().c_str());

            bool isSelected = (entry.Path == m_SelectedPath);
            bool isMapFile = !entry.IsDirectory && entry.Path.extension() == ".prismmap";
            bool isProjectFile = !entry.IsDirectory && entry.Path.extension() == ".prismproj";

            // Cor do "icone" (por enquanto so um retangulo colorido - um
            // icone de verdade por tipo de arquivo e um candidato natural
            // quando o sistema de import de assets/texturas existir) -
            // pasta em azul claro, mapa em laranja (combina com a cor do
            // cubo de teste da viewport), projeto em roxo, resto em cinza.
            ImVec4 iconColor = entry.IsDirectory ? ImVec4(0.45f, 0.65f, 0.90f, 1.0f)
                              : isMapFile        ? ImVec4(0.85f, 0.55f, 0.20f, 1.0f)
                              : isProjectFile    ? ImVec4(0.65f, 0.45f, 0.85f, 1.0f)
                                                 : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(iconColor.x, iconColor.y, iconColor.z, isSelected ? 0.55f : 0.25f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(iconColor.x, iconColor.y, iconColor.z, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(iconColor.x, iconColor.y, iconColor.z, 0.65f));

            const char* icon = entry.IsDirectory ? "[Pasta]" : isMapFile ? "[Mapa]" : isProjectFile ? "[Proj]" : "[Arq]";
            std::string buttonLabel = std::string(icon) + "\n" + entry.Name;

            if (ImGui::Button(buttonLabel.c_str(), ImVec2(cellSize, cellSize))) {
                m_SelectedPath = entry.Path;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (entry.IsDirectory) {
                    NavigateTo(entry.Path);
                } else if (isMapFile && m_OnMapDoubleClicked) {
                    m_OnMapDoubleClicked(entry.Path);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", entry.Path.string().c_str());
            }

            ImGui::PopStyleColor(3);
            ImGui::PopID();

            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

}
