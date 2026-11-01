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
            // Botao "R": volta o campo ao valor padrao (desabilitado se ja esta nele).
            // Passa pelo TransformCommand, entao Ctrl+Z desfaz.
            auto resetButton = [&](const char* id, glm::vec3& field, const glm::vec3& def) {
                ImGui::SameLine();
                ImGui::BeginDisabled(field == def);
                ImGui::PushID(id);
                if (ImGui::SmallButton("R")) {
                    Prism::TransformComponent before = transform;
                    field = def;
                    m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, before, transform));
                }
                ImGui::PopID();
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Resetar para o valor padrao");
            };

            ImGui::DragFloat3("Posicao", glm::value_ptr(transform.Translation), 0.05f);
            if (ImGui::IsItemActivated())
                m_TransformBeforeEdit = transform;
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));
            resetButton("rp", transform.Translation, glm::vec3(0.0f));

            ImGui::DragFloat3("Rotacao", glm::value_ptr(transform.Rotation), 0.5f);
            if (ImGui::IsItemActivated())
                m_TransformBeforeEdit = transform;
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));
            resetButton("rr", transform.Rotation, glm::vec3(0.0f));

            // Escala proporcional (estilo Godot): ao mexer em um eixo, os outros
            // acompanham pela mesma razao.
            const glm::vec3 scaleBefore = transform.Scale;
            ImGui::DragFloat3("Escala", glm::value_ptr(transform.Scale), 0.05f, 0.01f, 100.0f);
            const bool scaleItemActivated = ImGui::IsItemActivated();
            const bool scaleItemDone = ImGui::IsItemDeactivatedAfterEdit();
            if (m_ScaleLinked && transform.Scale != scaleBefore) {
                for (int i = 0; i < 3; i++) {
                    if (transform.Scale[i] == scaleBefore[i] || scaleBefore[i] <= 0.0f)
                        continue;
                    const float ratio = transform.Scale[i] / scaleBefore[i];
                    for (int j = 0; j < 3; j++)
                        if (j != i)
                            transform.Scale[j] = std::clamp(scaleBefore[j] * ratio, 0.01f, 100.0f);
                    break;
                }
            }
            if (scaleItemActivated)
                m_TransformBeforeEdit = transform;
            if (scaleItemDone)
                m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));
            resetButton("rs", transform.Scale, glm::vec3(1.0f));
            ImGui::Checkbox("Escala proporcional", &m_ScaleLinked);
        }
        // Transform nao tem botao de remover - toda entidade tem um por
        // definicao (ver Scene::CreateEntity) e o resto da engine
        // assume isso (ex: RenderScene le GetTransform() sem checar
        // HasComponent primeiro).
    }

}
