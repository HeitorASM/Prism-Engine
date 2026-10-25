#include "PropertiesPanel.h"
#include "ComponentEditUtils.h"
#include "ScriptEditorPanel.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

// PropertiesPanel_UIDocument.cpp
// UI do UIDocumentComponent: aponta para um .prismui (XML) que descreve a UI inteira.
// O arquivo e relido sozinho quando e salvo (hot reload) - edite no editor de
// codigo e veja o resultado no viewport/Game.

namespace PrismEditor {

    void PropertiesPanel::DrawUIDocumentUI() {
        auto& doc = m_Ctx.SelectedEntity.GetComponent<Prism::UIDocumentComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("UI Document", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* title = "UI Document";

            // Arquivo: arraste um .prismui do Conteudo do Projeto.
            std::string label = doc.Path.empty() ? "Arquivo: arraste um .prismui aqui" : "Arquivo: " + doc.Path;
            ImGui::Button(label.c_str(), ImVec2(-1, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MOVE_PATH")) {
                    std::filesystem::path dropped((const char*)payload->Data);
                    if (dropped.extension() == ".prismui") {
                        auto before = doc;
                        std::string relativePath = dropped.generic_string();
                        if (auto project = Prism::Project::GetActive()) {
                            std::error_code ec;
                            auto relative = std::filesystem::relative(dropped, project->GetProjectDirectory(), ec);
                            if (!ec)
                                relativePath = relative.generic_string();
                        }
                        doc.Path = relativePath;
                        CommitComponentEdit(m_Ctx, before, doc, title);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::BeginDisabled(doc.Path.empty());
            if (ImGui::Button("Editar")) {
                auto project = Prism::Project::GetActive();
                std::filesystem::path full = project ? project->GetProjectDirectory() / doc.Path : std::filesystem::path(doc.Path);
                if (m_Ctx.ScriptEditor && m_Ctx.ScriptEditor->Open(full))
                    ImGui::SetWindowFocus("###ScriptEditorPanel");
            }
            ImGui::EndDisabled();
            if (!doc.Path.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Limpar arquivo##UIDoc")) {
                    auto before = doc;
                    doc.Path.clear();
                    CommitComponentEdit(m_Ctx, before, doc, title);
                }
            }

            {
                auto before = doc;
                if (ImGui::Checkbox("Visivel", &doc.Visible))
                    CommitComponentEdit(m_Ctx, before, doc, title);
            }
            int layer = doc.Layer;
            if (ImGui::DragInt("Camada", &layer, 0.2f, -1000, 1000))
                doc.Layer = layer;
            TrackContinuousEdit(m_Ctx, m_UIDocBeforeEdit, doc, title);

            ImGui::TextDisabled("Erros do arquivo aparecem no Console (arquivo:linha:coluna).");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::UIDocumentComponent>>(m_Ctx.SelectedEntity, "UI Document"));
    }

}
