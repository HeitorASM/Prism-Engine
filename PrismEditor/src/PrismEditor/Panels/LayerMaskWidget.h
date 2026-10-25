#pragma once

// ============================================================================
// LayerMaskWidget.h
// Seletor de camadas de visibilidade (32 bits, nomes por projeto) usado pelo
// Mesh Renderer ("Camadas") e pela Camera ("Enxerga camadas").
//
//   uint32_t mask = ...;
//   if (DrawLayerMask("Camadas##Mesh", mask)) { ... aplicar e registrar undo ... }
//
// Devolve true no frame em que a mascara mudou. O botao mostra um resumo; o popup
// tem uma caixa por camada, "Todas"/"Nenhuma" e o modo "Renomear camadas" (os
// nomes ficam no .prismproj, valem para todo o projeto).
// ============================================================================

#include <Prism/Project/Project.h>
#include <imgui.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace PrismEditor {

    inline std::string LayerName(int index) {
        auto project = Prism::Project::GetActive();
        return project ? project->GetLayerName(index) : "Camada " + std::to_string(index + 1);
    }

    inline std::string LayerMaskSummary(uint32_t mask) {
        if (mask == 0xFFFFFFFFu) return "Todas";
        if (mask == 0u) return "Nenhuma";
        int count = 0, first = -1;
        for (int i = 0; i < 32; i++)
            if (mask & (1u << i)) { count++; if (first < 0) first = i; }
        if (count == 1) return LayerName(first);
        return LayerName(first) + " +" + std::to_string(count - 1);
    }

    inline bool DrawLayerMask(const char* label, uint32_t& mask) {
        bool changed = false;
        std::string summary = LayerMaskSummary(mask);

        ImGui::PushID(label);
        if (ImGui::Button((summary + "##open").c_str(), ImVec2(ImGui::CalcItemWidth(), 0)))
            ImGui::OpenPopup("layer_mask_popup");
        ImGui::SameLine();
        const char* hashes = std::strstr(label, "##"); // "Texto##id": so "Texto" aparece
        ImGui::TextUnformatted(label, hashes ? hashes : label + std::strlen(label));

        static bool s_Rename = false;
        if (ImGui::BeginPopup("layer_mask_popup")) {
            if (ImGui::SmallButton("Todas")) { mask = 0xFFFFFFFFu; changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Nenhuma")) { mask = 0u; changed = true; }
            ImGui::SameLine();
            ImGui::Checkbox("Renomear camadas", &s_Rename);
            ImGui::Separator();

            ImGui::BeginChild("layer_list", ImVec2(260, 260));
            for (int i = 0; i < 32; i++) {
                ImGui::PushID(i);
                bool on = (mask & (1u << i)) != 0u;
                if (ImGui::Checkbox("##on", &on)) {
                    mask = on ? (mask | (1u << i)) : (mask & ~(1u << i));
                    changed = true;
                }
                ImGui::SameLine();
                if (s_Rename) {
                    char buffer[64];
                    auto project = Prism::Project::GetActive();
                    std::snprintf(buffer, sizeof(buffer), "%s", (project && project->HasCustomLayerName(i)) ? project->GetLayerName(i).c_str() : "");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputTextWithHint("##name", ("Camada " + std::to_string(i + 1)).c_str(), buffer, sizeof(buffer)))
                        Prism::Project::SetLayerName(i, buffer); // grava o .prismproj a cada edicao (arquivo pequeno)
                }
                else {
                    ImGui::Text("%2d  %s", i + 1, LayerName(i).c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

}
