#include "PropertiesPanel.h"
#include "PhysicsLayerWidget.h"
#include <imgui.h>
#include "../Commands/EditorCommands.h"
#include "ComponentEditUtils.h"
#include <glm/gtc/type_ptr.hpp>

// PropertiesPanel_CharacterController.cpp - UI do CharacterControllerComponent.
namespace PrismEditor {

    void PropertiesPanel::DrawCharacterControllerUI() {
        auto& c = m_Ctx.SelectedEntity.GetComponent<Prism::CharacterControllerComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Character Controller", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const Prism::CharacterControllerComponent before = c;
            ImGui::DragFloat("Raio", &c.Radius, 0.01f, 0.05f, 5.0f);
            TrackContinuousEdit(m_Ctx, m_CharacterBeforeEdit, c, "Character Controller");
            ImGui::DragFloat("Altura", &c.Height, 0.02f, 0.2f, 10.0f);
            TrackContinuousEdit(m_Ctx, m_CharacterBeforeEdit, c, "Character Controller");
            ImGui::DragFloat("Altura de degrau", &c.StepHeight, 0.01f, 0.0f, 2.0f);
            TrackContinuousEdit(m_Ctx, m_CharacterBeforeEdit, c, "Character Controller");
            ImGui::DragFloat("Inclinacao maxima (graus)", &c.MaxSlopeAngle, 0.5f, 0.0f, 89.0f);
            TrackContinuousEdit(m_Ctx, m_CharacterBeforeEdit, c, "Character Controller");
            ImGui::DragFloat("Massa", &c.Mass, 0.5f, 1.0f, 1000.0f);
            TrackContinuousEdit(m_Ctx, m_CharacterBeforeEdit, c, "Character Controller");
            ImGui::DragFloat("Escala da gravidade", &c.GravityScale, 0.02f, 0.0f, 10.0f);
            TrackContinuousEdit(m_Ctx, m_CharacterBeforeEdit, c, "Character Controller");
            if (ImGui::Checkbox("Colar no chao", &c.StickToFloor))
                CommitComponentEdit(m_Ctx, before, c, "Character Controller");
            ImGui::TextDisabled("Mova pelo script: entity:CharacterMove(x,y,z), CharacterJump(v), IsGrounded().");
            ImGui::TextDisabled("Nao use junto com Rigid Body. A camada vem do Collider (se houver).");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::CharacterControllerComponent>>(m_Ctx.SelectedEntity, "Character Controller"));
    }

}
