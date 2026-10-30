#include "PropertiesPanel.h"
#include "PhysicsLayerWidget.h"
#include <imgui.h>
#include "../Commands/EditorCommands.h"
#include "ComponentEditUtils.h"
#include <glm/gtc/type_ptr.hpp>

// PropertiesPanel_Joint.cpp - UI do JointComponent (junta fisica; so age no Play).
namespace PrismEditor {

    void PropertiesPanel::DrawJointUI() {
        auto& j = m_Ctx.SelectedEntity.GetComponent<Prism::JointComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Joint", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const Prism::JointComponent before = j;
            const char* types[] = { "Fixa", "Dobradica", "Deslizante", "Distancia", "Ponto" };
            int t = (int)j.Type;
            if (ImGui::Combo("Tipo##Joint", &t, types, IM_ARRAYSIZE(types))) {
                j.Type = (Prism::JointType)t;
                CommitComponentEdit(m_Ctx, before, j, "Joint");
            }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s", j.ConnectedEntity.c_str());
            if (ImGui::InputText("Conectada a (nome)", buf, sizeof(buf))) {
                j.ConnectedEntity = buf;
                CommitComponentEdit(m_Ctx, before, j, "Joint");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Nome (Tag) da outra entidade. Vazio = presa ao mundo.\nAs duas precisam de Rigid Body + Collider.");
            ImGui::DragFloat3("Ancora (local)", glm::value_ptr(j.Anchor), 0.05f);
            TrackContinuousEdit(m_Ctx, m_JointBeforeEdit, j, "Joint");
            if (j.Type == Prism::JointType::Hinge || j.Type == Prism::JointType::Slider) {
                ImGui::DragFloat3("Eixo", glm::value_ptr(j.Axis), 0.02f);
                TrackContinuousEdit(m_Ctx, m_JointBeforeEdit, j, "Joint");
            }
            if (j.Type != Prism::JointType::Fixed && j.Type != Prism::JointType::Point) {
                if (ImGui::Checkbox("Usar limites", &j.UseLimits))
                    CommitComponentEdit(m_Ctx, before, j, "Joint");
                if (j.UseLimits) {
                    ImGui::DragFloat("Limite min", &j.LimitMin, 0.05f);
                    TrackContinuousEdit(m_Ctx, m_JointBeforeEdit, j, "Joint");
                    ImGui::DragFloat("Limite max", &j.LimitMax, 0.05f);
                    TrackContinuousEdit(m_Ctx, m_JointBeforeEdit, j, "Joint");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Dobradica: graus (min <= 0 <= max). Deslizante/Distancia: metros.");
                }
            }
            if (j.Type == Prism::JointType::Distance) {
                if (ImGui::Checkbox("Mola", &j.UseSpring))
                    CommitComponentEdit(m_Ctx, before, j, "Joint");
                if (j.UseSpring) {
                    ImGui::DragFloat("Frequencia (Hz)", &j.SpringFrequency, 0.05f, 0.0f, 100.0f);
                    TrackContinuousEdit(m_Ctx, m_JointBeforeEdit, j, "Joint");
                    ImGui::DragFloat("Amortecimento", &j.SpringDamping, 0.01f, 0.0f, 1.0f);
                    TrackContinuousEdit(m_Ctx, m_JointBeforeEdit, j, "Joint");
                }
            }
            if (ImGui::Checkbox("Colidir entre si", &j.CollideConnected))
                CommitComponentEdit(m_Ctx, before, j, "Joint");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::JointComponent>>(m_Ctx.SelectedEntity, "Joint"));
    }

}
