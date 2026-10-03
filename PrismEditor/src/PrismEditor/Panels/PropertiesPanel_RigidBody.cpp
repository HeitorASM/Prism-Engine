#include "PropertiesPanel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "ContentBrowserPanel.h"
#include "ScriptEditorPanel.h"
#include "../Play/PlayWindow.h"
#include "../Commands/EditorCommands.h"
#include "../Core/EntityOps.h"
#include "ComponentEditUtils.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// PropertiesPanel_RigidBody.cpp
// UI do RigidBodyComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawRigidBodyUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawRigidBodyUI() {
        auto& rigidBody = m_Ctx.SelectedEntity.GetComponent<Prism::RigidBodyComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Rigid Body", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!m_Ctx.SelectedEntity.HasComponent<Prism::ColliderComponent>())
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Sem Collider - adicione um para a fisica funcionar.");

            const char* bodyTypeNames[] = { "Static", "Kinematic", "Dynamic" };
            const Prism::RigidBodyComponent before = rigidBody; // ver ComponentEditUtils.h
            int bodyTypeIndex = (int)rigidBody.Type;
            if (ImGui::Combo("Tipo##RigidBody", &bodyTypeIndex, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames))) {
                rigidBody.Type = (Prism::BodyType)bodyTypeIndex;
                CommitComponentEdit(m_Ctx, before, rigidBody, "Rigid Body");
            }

            bool dynamicOnly = (rigidBody.Type == Prism::BodyType::Dynamic);
            ImGui::BeginDisabled(!dynamicOnly);
            ImGui::DragFloat("Massa (kg)", &rigidBody.Mass, 0.1f, 0.01f, 10000.0f);
            TrackContinuousEdit(m_Ctx, m_RigidBodyBeforeEdit, rigidBody, "Rigid Body");
            if (ImGui::Checkbox("Usa Gravidade", &rigidBody.UseGravity))
                CommitComponentEdit(m_Ctx, before, rigidBody, "Rigid Body");
            ImGui::EndDisabled();

            if (ImGui::Checkbox("Colisao Continua (CCD)", &rigidBody.ContinuousCollisionDetection))
                CommitComponentEdit(m_Ctx, before, rigidBody, "Rigid Body");

            // So faz sentido fisicamente para Kinematic/Dynamic (Static
            // nunca gira de qualquer jeito, ja que nunca se move - ver
            // BodyType, Components.h) - mas nao ha necessidade de
            // desabilitar o checkbox para Static: um valor "true" nele
            // e simplesmente ignorado nesse caso (CreateBodyForEntity
            // ainda passa fixedRotation para o Jolt independente do
            // tipo, e um corpo Static ja tem rotacao fixa por natureza).
            if (ImGui::Checkbox("Rotacao Fixa (nao tomba/gira por fisica)", &rigidBody.FixedRotation))
                CommitComponentEdit(m_Ctx, before, rigidBody, "Rigid Body");
            if (rigidBody.FixedRotation)
                ImGui::TextDisabled("Corpo ainda translada normalmente - so a ROTACAO fica travada. Use para camera/player controlado por script.");

            ImGui::Separator();
            ImGui::TextDisabled("Material fisico");
            // Friction/Restitution valem para qualquer BodyType (uma
            // rampa Static com atrito baixo ainda afeta o que desliza
            // nela) - ver comentario em RigidBodyComponent, Components.h.
            ImGui::SliderFloat("Atrito", &rigidBody.Friction, 0.0f, 1.0f);
            TrackContinuousEdit(m_Ctx, m_RigidBodyBeforeEdit, rigidBody, "Rigid Body");
            ImGui::SliderFloat("Restituicao (quique)", &rigidBody.Restitution, 0.0f, 1.0f);
            TrackContinuousEdit(m_Ctx, m_RigidBodyBeforeEdit, rigidBody, "Rigid Body");

            // Damping so tem efeito em corpos Dynamic (o Jolt integra
            // isso a cada step da simulacao - Static/Kinematic nao sao
            // integrados de qualquer forma).
            ImGui::BeginDisabled(!dynamicOnly);
            ImGui::SliderFloat("Amortecimento Linear", &rigidBody.LinearDamping, 0.0f, 1.0f);
            TrackContinuousEdit(m_Ctx, m_RigidBodyBeforeEdit, rigidBody, "Rigid Body");
            ImGui::SliderFloat("Amortecimento Angular", &rigidBody.AngularDamping, 0.0f, 1.0f);
            TrackContinuousEdit(m_Ctx, m_RigidBodyBeforeEdit, rigidBody, "Rigid Body");
            ImGui::EndDisabled();
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::RigidBodyComponent>>(m_Ctx.SelectedEntity, "Rigid Body"));
    }

}
