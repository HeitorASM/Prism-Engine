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

// PropertiesPanel_Raycast.cpp
// UI do RaycastComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawRaycastUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawRaycastUI() {
        auto& raycast = m_Ctx.SelectedEntity.GetComponent<Prism::RaycastComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Raycast", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const Prism::RaycastComponent before = raycast; // ver ComponentEditUtils.h
            if (ImGui::Checkbox("Ativo", &raycast.Enabled))
                CommitComponentEdit(m_Ctx, before, raycast, "Raycast");

            // Mesmo padrao Godot: um PONTO local, nao um vetor
            // direcao + distancia separados (ver comentario grande em
            // RaycastComponent, Components.h). DragFloat3 comum, mesmo
            // widget usado por TransformComponent::Translation na
            // Properties panel - o usuario ja conhece essa UI.
            ImGui::DragFloat3("Alvo (espaco local)", glm::value_ptr(raycast.TargetPosition), 0.05f);
            TrackContinuousEdit(m_Ctx, m_RaycastBeforeEdit, raycast, "Raycast");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Ponto ate onde o raio vai, em espaco LOCAL da entidade (gira/translada junto com ela).\nEx: (0,0,-3) = para frente, 3 unidades. (0,-2,0) = para baixo, 2 unidades (sensor de chao).");

            if (ImGui::Checkbox("Ignorar Pai/Irmas", &raycast.IgnoreParentAndSiblings))
                CommitComponentEdit(m_Ctx, before, raycast, "Raycast");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Se marcado, o raio ignora a entidade PAI (se houver) e todas as entidades IRMAS (que compartilham o mesmo pai) - util para um sensor filho do corpo do personagem nao acertar o proprio corpo/outros colliders do mesmo personagem.");

            ImGui::Separator();
            ImGui::TextDisabled("Resultado (fisico - so atualiza durante o modo Play):");
            if (!m_Ctx.ActiveScene->IsRunning()) {
                ImGui::TextDisabled("(fora do modo Play - sem resultado ainda)");
            }
            else if (raycast.Hit) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Acertou algo");
                std::string hitName = "(entidade invalida)";
                if (m_Ctx.ActiveScene->GetRegistry().valid(raycast.HitEntity)) {
                    Prism::Entity hitEntity(raycast.HitEntity, m_Ctx.ActiveScene.get());
                    if (hitEntity.HasComponent<Prism::TagComponent>())
                        hitName = hitEntity.GetComponent<Prism::TagComponent>().Tag;
                }
                ImGui::Text("Entidade: %s", hitName.c_str());
                ImGui::Text("Distancia: %.2f", raycast.HitDistance);
                ImGui::Text("Ponto: (%.2f, %.2f, %.2f)", raycast.HitPoint.x, raycast.HitPoint.y, raycast.HitPoint.z);
            }
            else {
                ImGui::TextDisabled("Sem acerto");
            }
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::RaycastComponent>>(m_Ctx.SelectedEntity, "Raycast"));
    }

}
