#include "LevelEditTool.h"
#include "../Core/LevelEditing.h"
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace PrismEditor::LevelEditTool {

    namespace {
        using Mode = LevelEditState::Mode;

        struct Projection2D {
            glm::mat4 Mvp{ 1.0f };
            ImVec2 Min{ 0, 0 };
            ImVec2 Size{ 1, 1 };
        };
        struct Point2D {
            ImVec2 P{ 0, 0 };
            float Depth = 0.0f;
            bool Ok = false;
        };

        Point2D Project(const Projection2D& c, const glm::vec3& local) {
            const glm::vec4 clip = c.Mvp * glm::vec4(local, 1.0f);
            Point2D out;
            if (clip.w <= 1e-4f)
                return out;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            out.P = ImVec2(c.Min.x + (ndc.x * 0.5f + 0.5f) * c.Size.x, c.Min.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * c.Size.y);
            out.Depth = ndc.z;
            out.Ok = true;
            return out;
        }

        float DistanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
            const float abx = b.x - a.x, aby = b.y - a.y;
            const float lengthSq = abx * abx + aby * aby;
            float t = lengthSq > 1e-6f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSq : 0.0f;
            t = std::min(std::max(t, 0.0f), 1.0f);
            const float dx = p.x - (a.x + abx * t), dy = p.y - (a.y + aby * t);
            return std::sqrt(dx * dx + dy * dy);
        }

        const ImU32 kEdge = IM_COL32(150, 165, 190, 130);
        const ImU32 kVertex = IM_COL32(190, 200, 220, 200);
        const ImU32 kSelected = IM_COL32(255, 150, 40, 255);
        const ImU32 kSelectedFill = IM_COL32(255, 150, 40, 70);
        const ImU32 kHover = IM_COL32(255, 235, 90, 255);
        const ImU32 kHoverFill = IM_COL32(255, 235, 90, 45);

        void FillPolygon(ImDrawList* drawList, const std::vector<ImVec2>& points, ImU32 color) {
            if (points.size() < 3 || points.size() > 12)
                return;   // n-gons grandes (tampas de cilindro) ficam so no contorno
            float area = 0.0f;
            for (size_t i = 0; i < points.size(); ++i) {
                const ImVec2& a = points[i];
                const ImVec2& b = points[(i + 1) % points.size()];
                area += a.x * b.y - b.x * a.y;
            }
            std::vector<ImVec2> ordered = points;
            if (area < 0.0f)
                std::reverse(ordered.begin(), ordered.end());   // ImGui espera horario na tela
            drawList->AddConvexPolyFilled(ordered.data(), (int)ordered.size(), color);
        }

        struct Hover {
            std::vector<uint32_t> Indices;   // o que um clique selecionaria (vazio = nada)
        };
    }

    bool IsEditing(EditorContext& ctx) {
        return LevelEditing::Sync(ctx) != nullptr;
    }

    void DrawToolbar(EditorContext& ctx) {
        if (!ctx.SelectedEntity || !ctx.SelectedEntity.HasComponent<Prism::LevelMeshComponent>())
            return;
        const bool editing = IsEditing(ctx);
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        if (ImGui::Button(editing ? "Sair da edicao (Tab)" : "Editar nivel (Tab)"))
            LevelEditing::SetActive(ctx, !editing);
        if (!editing)
            return;
        const struct { const char* Label; Mode M; } modes[] = { { "Vertice (1)", Mode::Vertex }, { "Aresta (2)", Mode::Edge }, { "Face (3)", Mode::Face } };
        for (const auto& entry : modes) {
            ImGui::SameLine();
            const bool current = ctx.Level.SelectionMode == entry.M;
            if (current) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(entry.Label))
                LevelEditing::SetMode(ctx, entry.M);
            if (current) ImGui::PopStyleColor();
        }
    }

    void HandleToggleShortcut(EditorContext& ctx) {
        if (ImGui::IsAnyItemActive() || !ImGui::IsKeyPressed(ImGuiKey_Tab, false))
            return;
        if (IsEditing(ctx))
            LevelEditing::SetActive(ctx, false);
        else
            LevelEditing::SetActive(ctx, true);
    }

    bool Render(EditorContext& ctx, const glm::mat4& view, const glm::mat4& projection,
                const ImVec2& imageMin, const ImVec2& size, bool hovered, bool focused) {
        Prism::LevelMeshComponent* level = LevelEditing::Sync(ctx);
        if (!level)
            return false;
        LevelEditState& state = ctx.Level;
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 imageMax(imageMin.x + size.x, imageMin.y + size.y);

        // --- atalhos ---------------------------------------------------------------------------
        if (focused && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGuizmo::IsUsing()) {
            if (ImGui::IsKeyPressed(ImGuiKey_1, false)) LevelEditing::SetMode(ctx, Mode::Vertex);
            if (ImGui::IsKeyPressed(ImGuiKey_2, false)) LevelEditing::SetMode(ctx, Mode::Edge);
            if (ImGui::IsKeyPressed(ImGuiKey_3, false)) LevelEditing::SetMode(ctx, Mode::Face);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { LevelEditing::SetActive(ctx, false); return false; }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) LevelEditing::DeleteSelectedFaces(ctx);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) LevelEditing::Extrude(ctx);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) LevelEditing::Inset(ctx);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) LevelEditing::SelectAll(ctx);
            level = LevelEditing::Sync(ctx);   // as operacoes acima podem ter mexido na malha
            if (!level)
                return false;
        }
        const Prism::Level::EditableMesh& current = level->Mesh;

        const glm::mat4 model = ctx.ActiveScene->GetWorldTransform(state.Target);
        Projection2D proj;
        proj.Mvp = projection * view * model;
        proj.Min = imageMin;
        proj.Size = size;

        std::vector<Point2D> points(current.Vertices.size());
        for (size_t i = 0; i < points.size(); ++i)
            points[i] = Project(proj, current.Vertices[i]);

        std::vector<char> vertexSelected(current.Vertices.size(), 0), faceSelected(current.Faces.size(), 0);
        for (uint32_t v : LevelEditing::EffectiveVertices(current, state)) vertexSelected[v] = 1;
        for (uint32_t f : LevelEditing::EffectiveFaces(current, state)) faceSelected[f] = 1;

        // --- o que esta sob o mouse ------------------------------------------------------------
        const bool mouseFree = hovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && !ImGui::IsAnyItemHovered()
                            && !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const std::vector<std::pair<uint32_t, uint32_t>> edges = current.UniqueEdges();
        Hover hover;
        if (mouseFree) {
            const ImVec2 mouse = io.MousePos;
            if (state.SelectionMode == Mode::Vertex) {
                float best = 12.0f;
                for (uint32_t v = 0; v < points.size(); ++v) {
                    if (!points[v].Ok) continue;
                    const float dx = points[v].P.x - mouse.x, dy = points[v].P.y - mouse.y;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    if (d < best) { best = d; hover.Indices = { v }; }
                }
            }
            else if (state.SelectionMode == Mode::Edge) {
                float best = 8.0f;
                for (const auto& edge : edges) {
                    const Point2D &a = points[edge.first], &b = points[edge.second];
                    if (!a.Ok || !b.Ok) continue;
                    const float d = DistanceToSegment(mouse, a.P, b.P);
                    if (d < best) { best = d; hover.Indices = { edge.first, edge.second }; }
                }
            }
            else {
                const float ndcX = ((mouse.x - imageMin.x) / size.x) * 2.0f - 1.0f;
                const float ndcY = 1.0f - ((mouse.y - imageMin.y) / size.y) * 2.0f;
                const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
                glm::vec4 nearWorld = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 farWorld = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                if (std::fabs(nearWorld.w) > 1e-8f && std::fabs(farWorld.w) > 1e-8f) {
                    nearWorld /= nearWorld.w;
                    farWorld /= farWorld.w;
                    const glm::mat4 inverseModel = glm::inverse(model);
                    const glm::vec3 origin = glm::vec3(inverseModel * nearWorld);
                    const glm::vec3 direction = glm::vec3(inverseModel * glm::vec4(glm::vec3(farWorld - nearWorld), 0.0f));
                    float distance = 0.0f;
                    int face = -1;
                    if (current.Raycast(origin, direction, distance, face, true) && face >= 0)
                        hover.Indices = { (uint32_t)face };
                }
            }
        }

        // --- desenho ---------------------------------------------------------------------------
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(imageMin, imageMax, true);

        auto facePoints = [&](uint32_t f, std::vector<ImVec2>& out) {
            out.clear();
            for (uint32_t index : current.Faces[f].Indices) {
                if (!points[index].Ok) { out.clear(); return false; }
                out.push_back(points[index].P);
            }
            return out.size() >= 3;
        };

        std::vector<ImVec2> polygon;
        for (uint32_t f = 0; f < current.Faces.size(); ++f)
            if (faceSelected[f] && facePoints(f, polygon))
                FillPolygon(drawList, polygon, kSelectedFill);
        if (state.SelectionMode == Mode::Face && hover.Indices.size() == 1 && hover.Indices[0] < current.Faces.size()
            && facePoints(hover.Indices[0], polygon))
            FillPolygon(drawList, polygon, kHoverFill);

        for (const auto& edge : edges) {
            const Point2D &a = points[edge.first], &b = points[edge.second];
            if (!a.Ok || !b.Ok) continue;
            const bool selected = vertexSelected[edge.first] && vertexSelected[edge.second];
            drawList->AddLine(a.P, b.P, selected ? kSelected : kEdge, selected ? 2.0f : 1.0f);
        }
        if (state.SelectionMode == Mode::Edge && hover.Indices.size() == 2)
            drawList->AddLine(points[hover.Indices[0]].P, points[hover.Indices[1]].P, kHover, 3.0f);

        if (state.SelectionMode != Mode::Face) {
            for (uint32_t v = 0; v < points.size(); ++v) {
                if (!points[v].Ok) continue;
                const float r = vertexSelected[v] ? 4.5f : 3.0f;
                drawList->AddRectFilled(ImVec2(points[v].P.x - r, points[v].P.y - r), ImVec2(points[v].P.x + r, points[v].P.y + r),
                                        vertexSelected[v] ? kSelected : kVertex);
            }
            if (state.SelectionMode == Mode::Vertex && hover.Indices.size() == 1) {
                const ImVec2 p = points[hover.Indices[0]].P;
                drawList->AddRect(ImVec2(p.x - 6, p.y - 6), ImVec2(p.x + 6, p.y + 6), kHover, 0.0f, 0, 2.0f);
            }
        }

        const char* modeLabel = state.SelectionMode == Mode::Vertex ? "Vertice" : state.SelectionMode == Mode::Edge ? "Aresta" : "Face";
        char status[200];
        std::snprintf(status, sizeof(status), "EDITANDO NIVEL - %s | %zu vert. / %zu faces selecionados | Ctrl+E extrudar, Ctrl+I inset, Del excluir, Esc sair",
                      modeLabel, LevelEditing::EffectiveVertices(current, state).size(), LevelEditing::EffectiveFaces(current, state).size());
        drawList->AddText(ImVec2(imageMin.x + 10.0f, imageMax.y - 22.0f), IM_COL32(255, 200, 120, 255), status);
        drawList->PopClipRect();

        // --- gizmo de mover ---------------------------------------------------------------------
        bool consumedClick = false;
        if (LevelEditing::HasSelection(state) && !LevelEditing::EffectiveVertices(current, state).empty()) {
            const glm::vec3 pivot = LevelEditing::SelectionCenter(current, state);
            glm::mat4 gizmoMatrix = model * glm::translate(glm::mat4(1.0f), pivot);

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(imageMin.x, imageMin.y, size.x, size.y);

            const bool snap = state.SnapEnabled != io.KeyCtrl;   // Ctrl inverte o snap
            const float step = std::max(state.SnapSize, 0.001f);
            const float snapValues[3] = { step, step, step };
            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), ImGuizmo::TRANSLATE, ImGuizmo::LOCAL,
                                 glm::value_ptr(gizmoMatrix), nullptr, snap ? snapValues : nullptr);

            if (ImGuizmo::IsUsing()) {
                if (!state.Dragging)
                    LevelEditing::BeginMove(ctx, pivot);
                const glm::vec3 newPivot = glm::vec3(glm::inverse(model) * gizmoMatrix[3]);
                LevelEditing::UpdateMove(ctx, newPivot);
                consumedClick = true;
            }
            else if (state.Dragging) {
                LevelEditing::EndMove(ctx);
            }
        }
        else if (state.Dragging) {
            LevelEditing::EndMove(ctx);
        }

        // --- clique de selecao ------------------------------------------------------------------
        if (mouseFree && !ImGuizmo::IsOver() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            LevelEditing::Pick(ctx, hover.Indices, io.KeyShift);
            consumedClick = true;
        }
        return consumedClick || ImGuizmo::IsOver();
    }

}
