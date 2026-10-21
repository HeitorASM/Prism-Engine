#include "EditorIcons.h"
#include "EditorTheme.h"

#include <algorithm>
#include <cmath>

namespace PrismEditor {

    namespace {
        ImVec2 Lerp(ImVec2 a, ImVec2 b, float tx, float ty) {
            return ImVec2(a.x + (b.x - a.x) * tx, a.y + (b.y - a.y) * ty);
        }

        ImU32 WithAlpha(ImU32 color, float alpha) {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
            c.w *= alpha;
            return ImGui::ColorConvertFloat4ToU32(c);
        }

        // Folha de documento com canto dobrado ocupando [min,max].
        void DrawDocument(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color) {
            const float fold = (max.x - min.x) * 0.28f;
            ImVec2 pts[5] = {
                ImVec2(min.x, min.y),
                ImVec2(max.x - fold, min.y),
                ImVec2(max.x, min.y + fold),
                ImVec2(max.x, max.y),
                ImVec2(min.x, max.y),
            };
            dl->AddConvexPolyFilled(pts, 5, WithAlpha(color, 0.30f));
            dl->AddPolyline(pts, 5, color, ImDrawFlags_Closed, 1.5f);
            dl->AddTriangleFilled(ImVec2(max.x - fold, min.y), ImVec2(max.x, min.y + fold), ImVec2(max.x - fold, min.y + fold), WithAlpha(color, 0.75f));
        }

        void DrawBadge(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, const char* label) {
            // Faixa colorida no terco inferior da folha com o rotulo do tipo.
            ImVec2 a = Lerp(min, max, 0.0f, 0.58f);
            ImVec2 b = Lerp(min, max, 1.0f, 0.86f);
            dl->AddRectFilled(a, b, color, 2.0f);
            const float fontSize = std::max(9.0f, (b.y - a.y) * 0.80f);
            ImFont* font = ImGui::GetFont();
            ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
            dl->AddText(font, fontSize, ImVec2((a.x + b.x - size.x) * 0.5f, (a.y + b.y - size.y) * 0.5f), IM_COL32(15, 17, 22, 255), label);
        }
    }

    void DrawFileIcon(ImDrawList* dl, FileIcon icon, ImVec2 min, ImVec2 max, ImU32 color) {
        const float w = max.x - min.x;
        const float h = max.y - min.y;

        if (icon == FileIcon::Folder) {
            // Aba + corpo.
            ImVec2 bodyMin(min.x, min.y + h * 0.20f);
            dl->AddRectFilled(ImVec2(min.x, min.y + h * 0.08f), ImVec2(min.x + w * 0.42f, min.y + h * 0.34f), WithAlpha(color, 0.70f), 3.0f);
            dl->AddRectFilled(bodyMin, max, WithAlpha(color, 0.85f), 4.0f);
            dl->AddRectFilled(Lerp(min, max, 0.0f, 0.34f), max, color, 4.0f);
            return;
        }

        // Os demais: uma folha mais estreita centrada, com faixa de tipo.
        ImVec2 docMin(min.x + w * 0.20f, min.y + h * 0.04f);
        ImVec2 docMax(max.x - w * 0.20f, max.y - h * 0.04f);
        DrawDocument(dl, docMin, docMax, color);

        const char* label = nullptr;
        switch (icon) {
            case FileIcon::Map:      label = "MAP"; break;
            case FileIcon::Prefab:   label = "PFB"; break;
            case FileIcon::Material: label = "MAT"; break;
            case FileIcon::Model:    label = "3D";  break;
            case FileIcon::Audio:    label = "SND"; break;
            case FileIcon::Image:    label = "IMG"; break;
            case FileIcon::Script:   label = "SCR"; break;
            case FileIcon::Project:  label = "PRJ"; break;
            default: break;
        }
        if (label) {
            DrawBadge(dl, docMin, docMax, color, label);
        }
        else {
            // Arquivo generico: linhas de texto.
            for (int i = 0; i < 3; ++i) {
                float y = docMin.y + (docMax.y - docMin.y) * (0.50f + 0.14f * i);
                dl->AddLine(ImVec2(docMin.x + (docMax.x - docMin.x) * 0.2f, y), ImVec2(docMax.x - (docMax.x - docMin.x) * 0.2f, y), WithAlpha(color, 0.8f), 1.5f);
            }
        }
    }

    void DrawToolIcon(ImDrawList* dl, ToolIcon icon, ImVec2 min, ImVec2 max, ImU32 color) {
        const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float r = std::min(max.x - min.x, max.y - min.y) * 0.5f;
        const float t = std::max(1.5f, r * 0.14f); // espessura do traco

        // Arco de ~235 graus com ponta de seta no fim. 'mirror' espelha em X
        // (Desfazer = Refazer espelhado).
        auto arrow = [&](bool mirror) {
            const float pi = 3.14159265f;
            const float a0 = pi * 0.85f, a1 = pi * 2.15f;
            const float rad = r * 0.62f;
            const float sx = mirror ? -1.0f : 1.0f;
            auto point = [&](float ang) { return ImVec2(c.x + sx * std::cos(ang) * rad, c.y + std::sin(ang) * rad); };
            constexpr int N = 16;
            ImVec2 pts[N + 1];
            for (int i = 0; i <= N; ++i)
                pts[i] = point(a0 + (a1 - a0) * (float)i / (float)N);
            dl->AddPolyline(pts, N + 1, color, ImDrawFlags_None, t);

            ImVec2 tip = pts[N];
            ImVec2 tangent(-std::sin(a1) * sx, std::cos(a1));
            ImVec2 normal(-tangent.y, tangent.x);
            const float s = r * 0.40f;
            dl->AddTriangleFilled(
                ImVec2(tip.x + tangent.x * s, tip.y + tangent.y * s),
                ImVec2(tip.x + normal.x * s * 0.85f, tip.y + normal.y * s * 0.85f),
                ImVec2(tip.x - normal.x * s * 0.85f, tip.y - normal.y * s * 0.85f), color);
        };

        switch (icon) {
            case ToolIcon::Save: {
                ImVec2 a(c.x - r * 0.7f, c.y - r * 0.7f), b(c.x + r * 0.7f, c.y + r * 0.7f);
                dl->AddRect(a, b, color, 2.0f, 0, t);
                dl->AddRectFilled(ImVec2(c.x - r * 0.38f, a.y), ImVec2(c.x + r * 0.30f, c.y - r * 0.18f), color);
                dl->AddRect(ImVec2(c.x - r * 0.38f, c.y + r * 0.10f), ImVec2(c.x + r * 0.38f, b.y), color, 1.0f, 0, t * 0.8f);
                break;
            }
            case ToolIcon::Undo:    arrow(true); break;
            case ToolIcon::Redo:    arrow(false); break;
            case ToolIcon::Refresh: arrow(false); break;
            case ToolIcon::Back: {
                dl->AddLine(ImVec2(c.x + r * 0.30f, c.y - r * 0.55f), ImVec2(c.x - r * 0.30f, c.y), color, t);
                dl->AddLine(ImVec2(c.x - r * 0.30f, c.y), ImVec2(c.x + r * 0.30f, c.y + r * 0.55f), color, t);
                break;
            }
            case ToolIcon::NewFolder: {
                dl->AddRect(ImVec2(c.x - r * 0.8f, c.y - r * 0.45f), ImVec2(c.x + r * 0.8f, c.y + r * 0.6f), color, 2.0f, 0, t);
                dl->AddLine(ImVec2(c.x, c.y - r * 0.15f), ImVec2(c.x, c.y + r * 0.40f), color, t);
                dl->AddLine(ImVec2(c.x - r * 0.28f, c.y + r * 0.125f), ImVec2(c.x + r * 0.28f, c.y + r * 0.125f), color, t);
                break;
            }
            case ToolIcon::Play: {
                dl->AddTriangleFilled(ImVec2(c.x - r * 0.45f, c.y - r * 0.65f), ImVec2(c.x - r * 0.45f, c.y + r * 0.65f), ImVec2(c.x + r * 0.70f, c.y), color);
                break;
            }
            case ToolIcon::Stop: {
                dl->AddRectFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.55f), ImVec2(c.x + r * 0.55f, c.y + r * 0.55f), color, 2.0f);
                break;
            }
        }
    }

    bool IconButton(const char* id, ToolIcon icon, const char* tooltip, ImU32 tint, float size) {
        const EditorTheme& theme = EditorTheme::Get();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const float alpha = ImGui::GetStyle().Alpha;
        const bool disabled = alpha < 0.99f; // BeginDisabled reduz o Alpha do estilo

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 max(pos.x + size, pos.y + size);
        if (hovered || held)
            dl->AddRectFilled(pos, max, ToU32(held ? theme.SurfaceDown : theme.SurfaceHot), theme.Rounding);

        ImU32 color = tint ? tint : ToU32(theme.Text);
        if (disabled)
            color = WithAlpha(color, alpha);
        const float pad = size * 0.22f;
        DrawToolIcon(dl, icon, ImVec2(pos.x + pad, pos.y + pad), ImVec2(max.x - pad, max.y - pad), color);

        if (hovered && tooltip && *tooltip)
            ImGui::SetTooltip("%s", tooltip);
        return pressed && !disabled;
    }

}
