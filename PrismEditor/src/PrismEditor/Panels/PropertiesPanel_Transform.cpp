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

// PropertiesPanel_Transform.cpp
// UI do TransformComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawTransformUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawTransformUI() {
        auto& transform = m_Ctx.SelectedEntity.GetComponent<Prism::TransformComponent>();
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Cada DragFloat3 e verificado individualmente logo apos ser
            // desenhado - IsItemActivated()/IsItemDeactivatedAfterEdit()
            // sempre se referem ao ULTIMO item desenhado, entao nao da
            // para checar os tres so no final (so pegaria o de Escala).
            ImGui::DragFloat3("Posicao", glm::value_ptr(transform.Translation), 0.05f);
            if (ImGui::IsItemActivated())
                m_TransformBeforeEdit = transform;
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));

            ImGui::DragFloat3("Rotacao", glm::value_ptr(transform.Rotation), 0.5f);
            if (ImGui::IsItemActivated())
                m_TransformBeforeEdit = transform;
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));

            ImGui::DragFloat3("Escala", glm::value_ptr(transform.Scale), 0.05f, 0.01f, 100.0f);
            if (ImGui::IsItemActivated())
                m_TransformBeforeEdit = transform;
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));
        }
        // Transform nao tem botao de remover - toda entidade tem um por
        // definicao (ver Scene::CreateEntity) e o resto da engine
        // assume isso (ex: RenderScene le GetTransform() sem checar
        // HasComponent primeiro).
    }

}
