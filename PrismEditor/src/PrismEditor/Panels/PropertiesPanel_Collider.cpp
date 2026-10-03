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

// PropertiesPanel_Collider.cpp
// UI do ColliderComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawColliderUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawColliderUI() {
        auto& collider = m_Ctx.SelectedEntity.GetComponent<Prism::ColliderComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Collider", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            // Copia "antes" para os widgets discretos - ver ComponentEditUtils.h.
            const Prism::ColliderComponent before = collider;

            const char* shapeNames[] = { "Caixa", "Esfera", "Capsula", "Convex Hull (da malha)", "Triangle Mesh (da malha)" };
            int shapeIndex = (int)collider.Shape;
            if (ImGui::Combo("Forma", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames))) {
                collider.Shape = (Prism::ColliderShape)shapeIndex;
                CommitComponentEdit(m_Ctx, before, collider, "Collider");
            }

            const bool meshBased = collider.Shape == Prism::ColliderShape::ConvexHull
                                || collider.Shape == Prism::ColliderShape::TriangleMesh;

            switch (collider.Shape) {
            case Prism::ColliderShape::Box:
                ImGui::DragFloat3("Half-Extents", glm::value_ptr(collider.Size), 0.05f, 0.01f, 100.0f);
                TrackContinuousEdit(m_Ctx, m_ColliderBeforeEdit, collider, "Collider");
                break;
            case Prism::ColliderShape::Sphere:
                ImGui::DragFloat("Raio", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                TrackContinuousEdit(m_Ctx, m_ColliderBeforeEdit, collider, "Collider");
                break;
            case Prism::ColliderShape::Capsule:
                ImGui::DragFloat("Raio##Capsule", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                TrackContinuousEdit(m_Ctx, m_ColliderBeforeEdit, collider, "Collider");
                ImGui::DragFloat("Altura (cilindro)##Capsule", &collider.Size.y, 0.05f, 0.01f, 100.0f);
                TrackContinuousEdit(m_Ctx, m_ColliderBeforeEdit, collider, "Collider");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Altura so da parte cilindrica. A altura total da capsula e Altura + 2 x Raio (as calotas ficam por fora).");
                break;
            case Prism::ColliderShape::ConvexHull:
            case Prism::ColliderShape::TriangleMesh:
                break;
            }

            // --- Malha da entidade (usada por "Ajustar a malha" e pelas formas baseadas em malha) ---
            const Prism::Mesh* mesh = m_Ctx.SelectedEntity.HasComponent<Prism::MeshRendererComponent>()
                ? Prism::Renderer::ResolveMesh(m_Ctx.SelectedEntity.GetComponent<Prism::MeshRendererComponent>()) : nullptr;

            if (meshBased) {
                ImGui::TextDisabled("A forma vem da malha do Mesh Renderer, com a escala da entidade aplicada.");
                if (!mesh)
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Sem Mesh Renderer - a fisica vai usar uma caixa de Size.");
                if (collider.Shape == Prism::ColliderShape::TriangleMesh) {
                    const bool dynamicBody = m_Ctx.SelectedEntity.HasComponent<Prism::RigidBodyComponent>()
                        && m_Ctx.SelectedEntity.GetComponent<Prism::RigidBodyComponent>().Type == Prism::BodyType::Dynamic;
                    if (dynamicBody)
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Triangle Mesh nao funciona em Rigid Body Dynamic - a fisica vai usar Convex Hull.");
                    else
                        ImGui::TextDisabled("Malha exata: so para Static/Kinematic (cenario).");
                }
            }
            else {
                // Size e SEMPRE em unidades de mundo, independente da Scale
                // (ver ColliderComponent, Components.h) - este botao copia o
                // tamanho atual da malha (bounds x escala de mundo) para Size.
                ImGui::BeginDisabled(mesh == nullptr);
                if (ImGui::Button("Ajustar a malha")) {
                    glm::mat4 world = m_Ctx.ActiveScene->GetWorldTransform(m_Ctx.SelectedEntity);
                    glm::vec3 worldScale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
                    glm::vec3 full = (mesh->GetLocalBoundsMax() - mesh->GetLocalBoundsMin()) * worldScale; // tamanho total em mundo
                    constexpr float kMin = 0.05f; // mesmo minimo imposto pela fisica (PhysicsEngine.cpp)
                    switch (collider.Shape) {
                    case Prism::ColliderShape::Box:
                        collider.Size = glm::max(full * 0.5f, glm::vec3(kMin));
                        break;
                    case Prism::ColliderShape::Sphere:
                        collider.Size.x = glm::max(glm::max(full.x, glm::max(full.y, full.z)) * 0.5f, kMin);
                        break;
                    case Prism::ColliderShape::Capsule: {
                        float radius = glm::max(glm::max(full.x, full.z) * 0.5f, kMin);
                        collider.Size.x = radius;
                        collider.Size.y = glm::max(full.y - 2.0f * radius, kMin);
                        break;
                    }
                    default:
                        break;
                    }
                    CommitComponentEdit(m_Ctx, before, collider, "Collider");
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", mesh ? "Copia o tamanho da malha (com a escala da entidade) para Size.\nO collider continua com tamanho absoluto: mudar a Scale depois nao o redimensiona."
                                           : "Precisa de um Mesh Renderer nesta entidade.");
            }

            if (ImGui::Checkbox("E um Trigger", &collider.IsTrigger))
                CommitComponentEdit(m_Ctx, before, collider, "Collider");
            ImGui::TextDisabled("Precisa de um Rigid Body para participar da fisica.");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ColliderComponent>>(m_Ctx.SelectedEntity, "Collider"));
    }

}
