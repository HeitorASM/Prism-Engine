#include "PropertiesPanel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "ContentBrowserPanel.h"
#include "ScriptEditorPanel.h"
#include "../Play/PlayWindow.h"
#include "../Commands/EditorCommands.h"
#include "../Core/EntityOps.h"
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
            const char* shapeNames[] = { "Caixa", "Esfera", "Capsula" };
            int shapeIndex = (int)collider.Shape;
            if (ImGui::Combo("Forma", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames)))
                collider.Shape = (Prism::ColliderShape)shapeIndex;

            switch (collider.Shape) {
            case Prism::ColliderShape::Box:
                ImGui::DragFloat3("Half-Extents", glm::value_ptr(collider.Size), 0.05f, 0.01f, 100.0f);
                break;
            case Prism::ColliderShape::Sphere:
                ImGui::DragFloat("Raio", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                break;
            case Prism::ColliderShape::Capsule:
                ImGui::DragFloat("Raio##Capsule", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                ImGui::DragFloat("Altura##Capsule", &collider.Size.y, 0.05f, 0.01f, 100.0f);
                break;
            }

            ImGui::Checkbox("E um Trigger", &collider.IsTrigger);
            ImGui::TextDisabled("Precisa de um Rigid Body para participar da fisica.");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ColliderComponent>>(m_Ctx.SelectedEntity, "Collider"));
    }

}
