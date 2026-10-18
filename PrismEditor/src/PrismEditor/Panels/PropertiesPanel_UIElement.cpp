#include "PropertiesPanel.h"
#include "ComponentEditUtils.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <string>

// PropertiesPanel_UIElement.cpp
// UI do UIElementComponent (UI in-game). O resultado aparece no viewport e na
// janela de Play, por cima da cena.

namespace PrismEditor {

    void PropertiesPanel::DrawUIElementUI() {
        auto& ui = m_Ctx.SelectedEntity.GetComponent<Prism::UIElementComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("UI Element", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* title = "UI Element";
            using Prism::UIElementType;

            auto checkbox = [&](const char* label, bool& value) {
                auto before = ui;
                if (ImGui::Checkbox(label, &value))
                    CommitComponentEdit(m_Ctx, before, ui, title);
            };
            auto combo = [&](const char* label, int& value, const char* const* names, int count) {
                auto before = ui;
                if (ImGui::Combo(label, &value, names, count))
                    CommitComponentEdit(m_Ctx, before, ui, title);
            };
            auto dragFloat = [&](const char* label, float& value, float speed, float low, float high) {
                ImGui::DragFloat(label, &value, speed, low, high);
                TrackContinuousEdit(m_Ctx, m_UIBeforeEdit, ui, title);
            };
            auto dragFloat2 = [&](const char* label, glm::vec2& value, float speed, float low, float high) {
                ImGui::DragFloat2(label, &value.x, speed, low, high);
                TrackContinuousEdit(m_Ctx, m_UIBeforeEdit, ui, title);
            };
            auto color = [&](const char* label, glm::vec4& value) {
                ImGui::ColorEdit4(label, &value.x);
                TrackContinuousEdit(m_Ctx, m_UIBeforeEdit, ui, title);
            };
            auto textInput = [&](const char* label, std::string& value, bool multiline) {
                char buffer[1024];
                std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
                bool changed = multiline
                    ? ImGui::InputTextMultiline(label, buffer, sizeof(buffer), ImVec2(-1, 60))
                    : ImGui::InputText(label, buffer, sizeof(buffer));
                if (changed)
                    value = buffer;
                TrackContinuousEdit(m_Ctx, m_UIBeforeEdit, ui, title);
            };

            static const char* kTypes[] = { "Panel", "Texto", "Imagem", "Botao" };
            int type = (int)ui.Type;
            combo("Tipo", type, kTypes, 4);
            ui.Type = (UIElementType)std::min(std::max(type, 0), 3);

            checkbox("Visivel", ui.Visible);
            int layer = ui.Layer;
            if (ImGui::DragInt("Camada (raiz)", &layer, 0.2f, -1000, 1000))
                ui.Layer = layer;
            TrackContinuousEdit(m_Ctx, m_UIBeforeEdit, ui, title);

            ImGui::Separator(); ImGui::TextDisabled("Layout (pixels)");
            static const char* kAnchors[] = { "Topo esquerda", "Topo", "Topo direita", "Esquerda", "Centro", "Direita",
                                              "Base esquerda", "Base", "Base direita", "Preencher o pai" };
            int anchor = (int)ui.Anchor;
            combo("Ancora", anchor, kAnchors, 10);
            ui.Anchor = (Prism::UIAnchor)std::min(std::max(anchor, 0), 9);
            dragFloat2(ui.Anchor == Prism::UIAnchor::Stretch ? "Margem" : "Deslocamento", ui.Offset, 0.5f, -10000.0f, 10000.0f);
            if (ui.Anchor != Prism::UIAnchor::Stretch)
                dragFloat2("Tamanho", ui.Size, 0.5f, 0.0f, 10000.0f);
            ImGui::TextDisabled("Filhos de UI se ancoram dentro do pai (arraste na hierarquia).");

            ImGui::Separator(); ImGui::TextDisabled("Aparencia");
            color(ui.Type == UIElementType::Text ? "Cor do texto" : "Cor", ui.Color);

            if (ui.Type != UIElementType::Text) {
                std::string label = ui.TexturePath.empty() ? "Textura: arraste uma imagem (opcional)" : "Textura: " + ui.TexturePath;
                ImGui::Button(label.c_str(), ImVec2(-1, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_IMAGE_PATH")) {
                        auto before = ui;
                        std::filesystem::path dropped((const char*)payload->Data);
                        if (auto project = Prism::Project::GetActive()) {
                            std::error_code ec;
                            auto relative = std::filesystem::relative(dropped, project->GetProjectDirectory(), ec);
                            ui.TexturePath = !ec ? relative.generic_string() : dropped.generic_string();
                        }
                        else {
                            ui.TexturePath = dropped.generic_string();
                        }
                        CommitComponentEdit(m_Ctx, before, ui, title);
                    }
                    ImGui::EndDragDropTarget();
                }
                if (!ui.TexturePath.empty() && ImGui::SmallButton("Limpar textura##UI")) {
                    auto before = ui;
                    ui.TexturePath.clear();
                    CommitComponentEdit(m_Ctx, before, ui, title);
                }
            }

            if (ui.Type == UIElementType::Text || ui.Type == UIElementType::Button) {
                ImGui::Separator(); ImGui::TextDisabled("Texto");
                textInput("Texto", ui.Text, ui.Type == UIElementType::Text);
                dragFloat("Tamanho da fonte", ui.FontSize, 0.25f, 4.0f, 400.0f);
                if (ui.Type == UIElementType::Text) {
                    static const char* kAligns[] = { "Esquerda", "Centro", "Direita" };
                    int align = (int)ui.Align;
                    combo("Alinhamento", align, kAligns, 3);
                    ui.Align = (Prism::UITextAlign)std::min(std::max(align, 0), 2);
                }
                else {
                    color("Cor do rotulo", ui.TextColor);
                }
            }

            if (ui.Type == UIElementType::Button) {
                ImGui::Separator(); ImGui::TextDisabled("Botao");
                checkbox("Interagivel", ui.Interactable);
                color("Cor com mouse em cima", ui.HoverColor);
                color("Cor ao apertar", ui.PressedColor);
                ImGui::TextDisabled("Ao clicar, chama OnClick() no script Lua/C++ da entidade.");
            }

            if (ui.Type != UIElementType::Text)
                checkbox("Barra o mouse (UI.IsPointerOverUI)", ui.BlocksPointer);
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::UIElementComponent>>(m_Ctx.SelectedEntity, "UI Element"));
    }

}
