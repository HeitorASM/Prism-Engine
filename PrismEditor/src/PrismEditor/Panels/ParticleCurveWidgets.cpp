#include "ParticleCurveWidgets.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PrismEditor {

    namespace {
        constexpr float kPickRadius = 8.0f; // pixels

        float Clamp01(float v) { return std::min(std::max(v, 0.0f), 1.0f); }

        ImU32 ToU32(const float c[4]) {
            return ImGui::GetColorU32(ImVec4(Clamp01(c[0]), Clamp01(c[1]), Clamp01(c[2]), Clamp01(c[3])));
        }

        // Fecha o ciclo do gesto: Started na ativacao, Finished (so se houve edicao) na desativacao.
        // 'editedKey' guarda no estado do ImGui se o gesto atual ja alterou algo.
        ParticleWidgetResult ResolveGesture(ImGuiStorage* storage, ImGuiID editedKey, bool changed) {
            ParticleWidgetResult result;
            result.Changed = changed;
            result.Started = ImGui::IsItemActivated();
            if (result.Started)
                storage->SetBool(editedKey, false);
            if (changed)
                storage->SetBool(editedKey, true);
            if (ImGui::IsItemDeactivated()) {
                result.Finished = storage->GetBool(editedKey, false);
                storage->SetBool(editedKey, false);
            }
            return result;
        }
    }

    // ------------------------------------------------------------------------
    // Curva
    // ------------------------------------------------------------------------
    ParticleWidgetResult ParticleCurveEditor(const char* label, Prism::ParticleCurve& curve, float minValue, float maxValue, float height) {
        ImGui::PushID(label);
        if (!(maxValue > minValue))
            maxValue = minValue + 1.0f;

        const float width = std::max(ImGui::GetContentRegionAvail().x, 80.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 corner(origin.x + width, origin.y + height);
        ImGui::InvisibleButton("##canvas", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const bool started = ImGui::IsItemActivated();

        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID selectedKey = ImGui::GetID("##selected");
        const ImGuiID editedKey = ImGui::GetID("##edited");
        int selected = storage->GetInt(selectedKey, -1);

        curve.Count = std::min<uint32_t>(std::max<uint32_t>(curve.Count, 2u), (uint32_t)Prism::kParticleMaxCurveKeys);
        if (selected >= (int)curve.Count)
            selected = -1;

        const float pad = 8.0f;
        const float plotW = width - 2.0f * pad;
        const float plotH = height - 2.0f * pad;
        auto toScreenX = [&](float t) { return origin.x + pad + t * plotW; };
        auto toScreenY = [&](float v) { return origin.y + pad + (1.0f - (v - minValue) / (maxValue - minValue)) * plotH; };
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float mouseT = Clamp01((mouse.x - origin.x - pad) / plotW);
        const float mouseV = minValue + (1.0f - Clamp01((mouse.y - origin.y - pad) / plotH)) * (maxValue - minValue);

        auto pickKey = [&]() {
            int best = -1;
            float bestDistance = kPickRadius * kPickRadius;
            for (uint32_t i = 0; i < curve.Count; i++) {
                const float dx = toScreenX(curve.T[i]) - mouse.x;
                const float dy = toScreenY(curve.V[i]) - mouse.y;
                const float d = dx * dx + dy * dy;
                if (d <= bestDistance) { bestDistance = d; best = (int)i; }
            }
            return best;
        };

        bool changed = false;
        if (started) {
            const int hit = pickKey();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                if (hit >= 0 && curve.RemoveKey(hit)) {
                    selected = -1;
                    changed = true;
                }
            }
            else if (hit >= 0) {
                selected = hit;
            }
            else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const int added = curve.AddKey(mouseT, std::min(std::max(mouseV, minValue), maxValue));
                if (added >= 0) {
                    selected = added;
                    changed = true;
                }
            }
            else {
                selected = -1;
            }
        }
        // Arrasto: pontos das pontas so mudam de valor; os de dentro ficam entre os vizinhos.
        if (active && selected >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && (uint32_t)selected < curve.Count) {
            const uint32_t i = (uint32_t)selected;
            const float newV = std::min(std::max(mouseV, minValue), maxValue);
            float newT = curve.T[i];
            if (i > 0 && i + 1 < curve.Count)
                newT = std::min(std::max(mouseT, curve.T[i - 1]), curve.T[i + 1]);
            if (newT != curve.T[i] || newV != curve.V[i]) {
                curve.T[i] = newT;
                curve.V[i] = newV;
                changed = true;
            }
        }
        storage->SetInt(selectedKey, selected);

        // Resolve o gesto ANTES de desenhar/tooltips: IsItemActivated/IsItemDeactivated se referem ao ultimo item (o canvas).
        const ParticleWidgetResult result = ResolveGesture(storage, editedKey, changed);

        // --- desenho ---
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, corner, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
        const ImU32 gridColor = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.07f));
        for (int g = 0; g <= 4; g++) {
            const float y = origin.y + pad + plotH * (float)g / 4.0f;
            draw->AddLine(ImVec2(origin.x + pad, y), ImVec2(corner.x - pad, y), gridColor);
            const float x = origin.x + pad + plotW * (float)g / 4.0f;
            draw->AddLine(ImVec2(x, origin.y + pad), ImVec2(x, corner.y - pad), gridColor);
        }
        // Linha do valor 1 (neutro) quando visivel.
        if (1.0f >= minValue && 1.0f <= maxValue) {
            const float y = toScreenY(1.0f);
            draw->AddLine(ImVec2(origin.x + pad, y), ImVec2(corner.x - pad, y), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.18f)));
        }
        const ImU32 lineColor = ImGui::GetColorU32(ImVec4(0.55f, 0.80f, 1.0f, 1.0f));
        draw->AddLine(ImVec2(toScreenX(0.0f), toScreenY(curve.V[0])), ImVec2(toScreenX(curve.T[0]), toScreenY(curve.V[0])), lineColor, 2.0f);
        for (uint32_t i = 1; i < curve.Count; i++)
            draw->AddLine(ImVec2(toScreenX(curve.T[i - 1]), toScreenY(curve.V[i - 1])), ImVec2(toScreenX(curve.T[i]), toScreenY(curve.V[i])), lineColor, 2.0f);
        const uint32_t last = curve.Count - 1;
        draw->AddLine(ImVec2(toScreenX(curve.T[last]), toScreenY(curve.V[last])), ImVec2(toScreenX(1.0f), toScreenY(curve.V[last])), lineColor, 2.0f);
        for (uint32_t i = 0; i < curve.Count; i++) {
            const bool isSelected = (int)i == selected;
            const ImVec2 center(toScreenX(curve.T[i]), toScreenY(curve.V[i]));
            draw->AddCircleFilled(center, isSelected ? 5.5f : 4.0f, isSelected ? IM_COL32(255, 210, 90, 255) : IM_COL32(235, 245, 255, 255));
            draw->AddCircle(center, isSelected ? 5.5f : 4.0f, IM_COL32(0, 0, 0, 200));
        }
        char text[48];
        std::snprintf(text, sizeof(text), "%.2f", maxValue);
        draw->AddText(ImVec2(origin.x + pad + 2.0f, origin.y + 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
        std::snprintf(text, sizeof(text), "%.2f", minValue);
        draw->AddText(ImVec2(origin.x + pad + 2.0f, corner.y - ImGui::GetTextLineHeight() - 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);

        if (hovered || active) {
            const int hit = active ? selected : pickKey();
            if (hit >= 0 && (uint32_t)hit < curve.Count)
                ImGui::SetTooltip("t = %.2f   valor = %.2f", curve.T[hit], curve.V[hit]);
            else if (hovered)
                ImGui::SetTooltip("Duplo clique: adicionar ponto\nArraste: mover\nBotao direito num ponto: remover");
        }

        ImGui::PopID();
        return result;
    }

    // ------------------------------------------------------------------------
    // Gradiente
    // ------------------------------------------------------------------------
    int ParticleGradientSelection(const char* label, const Prism::ParticleGradient& gradient) {
        ImGui::PushID(label);
        int selected = ImGui::GetStateStorage()->GetInt(ImGui::GetID("##selected"), 0);
        ImGui::PopID();
        return std::min(std::max(selected, 0), (int)gradient.Count - 1);
    }

    ParticleWidgetResult ParticleGradientEditor(const char* label, Prism::ParticleGradient& gradient) {
        ImGui::PushID(label);
        const float width = std::max(ImGui::GetContentRegionAvail().x, 80.0f);
        const float barHeight = 22.0f;
        const float markerHeight = 14.0f;
        const float pad = 8.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##canvas", ImVec2(width, barHeight + markerHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const bool started = ImGui::IsItemActivated();

        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID selectedKey = ImGui::GetID("##selected");
        const ImGuiID editedKey = ImGui::GetID("##edited");
        gradient.Count = std::min<uint32_t>(std::max<uint32_t>(gradient.Count, 2u), (uint32_t)Prism::kParticleMaxGradientStops);
        int selected = std::min(std::max(storage->GetInt(selectedKey, 0), 0), (int)gradient.Count - 1);

        const float plotW = width - 2.0f * pad;
        auto toScreenX = [&](float t) { return origin.x + pad + t * plotW; };
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float mouseT = Clamp01((mouse.x - origin.x - pad) / plotW);

        auto pickStop = [&]() {
            int best = -1;
            float bestDistance = kPickRadius;
            for (uint32_t i = 0; i < gradient.Count; i++) {
                const float d = std::fabs(toScreenX(gradient.T[i]) - mouse.x);
                if (d <= bestDistance) { bestDistance = d; best = (int)i; }
            }
            return best;
        };

        bool changed = false;
        if (started) {
            const int hit = pickStop();
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                if (hit >= 0 && gradient.RemoveStop(hit)) {
                    selected = std::min(selected, (int)gradient.Count - 1);
                    changed = true;
                }
            }
            else if (hit >= 0) {
                selected = hit;
            }
            else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const int added = gradient.AddStop(mouseT);
                if (added >= 0) {
                    selected = added;
                    changed = true;
                }
            }
        }
        if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && gradient.Count >= 2 && selected >= 0 && (uint32_t)selected < gradient.Count) {
            if (std::fabs(gradient.T[selected] - mouseT) > 1e-5f) {
                gradient.T[selected] = mouseT;
                selected = gradient.SortKeepingIndex(selected);
                changed = true;
            }
        }
        storage->SetInt(selectedKey, selected);

        // Resolve o gesto ANTES de desenhar/tooltips: IsItemActivated/IsItemDeactivated se referem ao ultimo item (o canvas).
        const ParticleWidgetResult result = ResolveGesture(storage, editedKey, changed);

        // --- desenho ---
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 barMin(origin.x + pad, origin.y);
        const ImVec2 barMax(origin.x + pad + plotW, origin.y + barHeight);
        // Fundo xadrez simples para o alpha ser visivel.
        draw->AddRectFilled(barMin, barMax, IM_COL32(70, 70, 70, 255));
        const float cell = barHeight * 0.5f;
        for (float x = barMin.x; x < barMax.x; x += cell) {
            const int column = (int)((x - barMin.x) / cell);
            const float y0 = barMin.y + ((column & 1) ? 0.0f : cell);
            draw->AddRectFilled(ImVec2(x, y0), ImVec2(std::min(x + cell, barMax.x), y0 + cell), IM_COL32(105, 105, 105, 255));
        }
        float first[4];
        gradient.Evaluate(0.0f, first);
        if (gradient.T[0] > 0.0f)
            draw->AddRectFilled(barMin, ImVec2(toScreenX(gradient.T[0]), barMax.y), ToU32(first));
        for (uint32_t i = 1; i < gradient.Count; i++)
            draw->AddRectFilledMultiColor(ImVec2(toScreenX(gradient.T[i - 1]), barMin.y), ImVec2(toScreenX(gradient.T[i]), barMax.y),
                ToU32(gradient.C[i - 1]), ToU32(gradient.C[i]), ToU32(gradient.C[i]), ToU32(gradient.C[i - 1]));
        const uint32_t last = gradient.Count - 1;
        if (gradient.T[last] < 1.0f)
            draw->AddRectFilled(ImVec2(toScreenX(gradient.T[last]), barMin.y), barMax, ToU32(gradient.C[last]));
        draw->AddRect(barMin, barMax, ImGui::GetColorU32(ImGuiCol_Border));

        for (uint32_t i = 0; i < gradient.Count; i++) {
            const bool isSelected = (int)i == selected;
            const float x = toScreenX(gradient.T[i]);
            const float top = origin.y + barHeight;
            const float bottom = top + markerHeight;
            draw->AddTriangleFilled(ImVec2(x, top), ImVec2(x - 5.0f, bottom), ImVec2(x + 5.0f, bottom), ToU32(gradient.C[i]));
            draw->AddTriangle(ImVec2(x, top), ImVec2(x - 5.0f, bottom), ImVec2(x + 5.0f, bottom),
                isSelected ? IM_COL32(255, 210, 90, 255) : IM_COL32(0, 0, 0, 220), isSelected ? 2.0f : 1.0f);
        }

        if (hovered && !active)
            ImGui::SetTooltip("Clique: selecionar parada (edite a cor abaixo)\nArraste: mover\nDuplo clique: adicionar\nBotao direito numa parada: remover");

        ImGui::PopID();
        return result;
    }

}
