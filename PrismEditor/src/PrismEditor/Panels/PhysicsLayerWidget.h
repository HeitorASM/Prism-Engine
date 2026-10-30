#pragma once

// ============================================================================
// PhysicsLayerWidget.h
// Widgets das camadas de FISICA (0..31, nomes e matriz de colisao por projeto):
//   DrawPhysicsLayerCombo  - escolhe a camada de UM collider
//   DrawPhysicsLayerMask   - mascara de camadas de uma consulta (Raycast)
//   DrawPhysicsMatrixButton- botao que abre a janela "Matriz de colisao"
// ============================================================================

#include <Prism/Project/Project.h>
#include <imgui.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace PrismEditor {

    inline std::string PhysicsLayerLabel(int i) {
        auto project = Prism::Project::GetActive();
        return project ? project->GetPhysicsLayerName(i) : (i == 0 ? "Padrao" : "Camada " + std::to_string(i));
    }

    // true no frame em que 'layer' mudou.
    inline bool DrawPhysicsLayerCombo(const char* label, int32_t& layer) {
        bool changed = false;
        const std::string current = PhysicsLayerLabel(layer);
        if (ImGui::BeginCombo(label, current.c_str())) {
            for (int i = 0; i < Prism::Physics::kLayerCount; i++) {
                const std::string name = PhysicsLayerLabel(i) + "##pl" + std::to_string(i);
                if (ImGui::Selectable(name.c_str(), layer == i)) { layer = i; changed = true; }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    inline bool DrawPhysicsLayerMask(const char* label, uint32_t& mask) {
        bool changed = false;
        std::string summary;
        if (mask == 0xFFFFFFFFu) summary = "Todas";
        else if (mask == 0u) summary = "Nenhuma";
        else {
            int count = 0, first = 0;
            for (int i = 0; i < 32; i++) if (mask & (1u << i)) { if (!count) first = i; count++; }
            summary = PhysicsLayerLabel(first) + (count > 1 ? " +" + std::to_string(count - 1) : "");
        }
        const std::string btn = summary + "##" + label;
        if (ImGui::Button(btn.c_str())) ImGui::OpenPopup(label);
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        if (ImGui::BeginPopup(label)) {
            if (ImGui::SmallButton("Todas")) { mask = 0xFFFFFFFFu; changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Nenhuma")) { mask = 0u; changed = true; }
            for (int i = 0; i < 32; i++) {
                bool on = (mask >> i) & 1u;
                const std::string n = PhysicsLayerLabel(i) + "##pm" + std::to_string(i);
                if (ImGui::Checkbox(n.c_str(), &on)) {
                    mask = on ? (mask | (1u << i)) : (mask & ~(1u << i));
                    changed = true;
                }
            }
            ImGui::EndPopup();
        }
        return changed;
    }

    // Janela modal com os nomes (editaveis) e a grade de colisao (simetrica).
    inline void DrawPhysicsMatrixButton() {
        auto project = Prism::Project::GetActive();
        if (!project) return;
        if (ImGui::Button("Camadas de fisica / matriz de colisao..."))
            ImGui::OpenPopup("Matriz de colisao##phys");
        ImGui::SetNextWindowSizeConstraints(ImVec2(420, 200), ImVec2(1400, 900));
        if (ImGui::BeginPopupModal("Matriz de colisao##phys", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextDisabled("Marque os pares de camadas que colidem. Vale para o projeto todo (salvo no .prismproj).");
            // Nomes
            if (ImGui::CollapsingHeader("Nomes das camadas")) {
                for (int i = 0; i < Prism::Physics::kLayerCount; i++) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s", project->HasCustomPhysicsLayerName(i) || i == 0 ? PhysicsLayerLabel(i).c_str() : "");
                    const std::string id = "##pn" + std::to_string(i);
                    ImGui::SetNextItemWidth(220);
                    if (ImGui::InputTextWithHint(id.c_str(), ("Camada " + std::to_string(i)).c_str(), buf, sizeof(buf)))
                        Prism::Project::SetPhysicsLayerName(i, buf);
                }
            }
            // Grade triangular: linha i x coluna j (j >= i)
            int used = 1;
            for (int i = 0; i < Prism::Physics::kLayerCount; i++)
                if (project->HasCustomPhysicsLayerName(i)) used = i + 1;
            const int rows = used < 2 ? 2 : used;
            for (int i = 0; i < rows; i++) {
                ImGui::TextUnformatted(PhysicsLayerLabel(i).c_str());
                for (int j = i; j < rows; j++) {
                    ImGui::SameLine(110.0f + j * 28.0f);
                    bool on = project->GetPhysicsMatrix().ShouldCollide(i, j);
                    const std::string id = "##pc" + std::to_string(i) + "_" + std::to_string(j);
                    if (ImGui::Checkbox(id.c_str(), &on))
                        Prism::Project::SetPhysicsCollision(i, j, on);
                }
            }
            ImGui::TextDisabled("(Mostra so as camadas com nome. De nome a uma camada para ela aparecer aqui.)");
            if (ImGui::Button("Fechar")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

}
