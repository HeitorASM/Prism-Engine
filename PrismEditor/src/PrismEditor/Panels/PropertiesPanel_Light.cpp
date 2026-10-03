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

// PropertiesPanel_Light.cpp
// UI do LightComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawLightUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawLightUI() {
        auto& light = m_Ctx.SelectedEntity.GetComponent<Prism::LightComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Light", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* typeNames[] = { "Point (Omni)", "Spot", "Directional" };
            int typeIndex = (int)light.Type;
            if (ImGui::Combo("Tipo", &typeIndex, typeNames, IM_ARRAYSIZE(typeNames)))
                light.Type = (Prism::LightType)typeIndex;

            ImGui::ColorEdit3("Cor##Light", glm::value_ptr(light.Color));
            ImGui::DragFloat("Intensidade", &light.Intensity, 0.05f, 0.0f, 100.0f);

            if (light.Type != Prism::LightType::Directional)
                ImGui::DragFloat("Alcance", &light.Range, 0.1f, 0.0f, 1000.0f);

            if (light.Type == Prism::LightType::Spot) {
                // Angulo externo primeiro: se o usuario reduzir o
                // externo abaixo do interno atual, arrasta o interno
                // junto (evita um estado "invalido" visualmente
                // confuso, mesmo que Renderer::CollectGPULights ja
                // clampe isso ao montar o GPULight).
                if (ImGui::DragFloat("Angulo do Cone (Externo)", &light.SpotAngle, 0.5f, 1.0f, 90.0f)) {
                    if (light.InnerSpotAngle > light.SpotAngle)
                        light.InnerSpotAngle = light.SpotAngle;
                }
                ImGui::DragFloat("Angulo do Cone (Interno)", &light.InnerSpotAngle, 0.5f, 0.0f, light.SpotAngle);
                ImGui::TextDisabled("(?) Entre os dois angulos a luz cai suavemente ate a borda.");
            }

            ImGui::Checkbox("Projetar Sombras", &light.CastShadows);
            if (light.Type != Prism::LightType::Directional && light.CastShadows && ImGui::IsItemHovered())
                ImGui::SetTooltip("Shadow mapping hoje so suporta luzes Directional - marcar aqui nao tem efeito visual para Point/Spot ainda.");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::LightComponent>>(m_Ctx.SelectedEntity, "Light"));
    }

}
