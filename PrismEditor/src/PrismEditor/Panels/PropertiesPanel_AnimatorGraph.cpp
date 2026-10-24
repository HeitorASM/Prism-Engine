#include "PropertiesPanel.h"
#include <imgui.h>
#include <Prism/Animation/AnimationSystem.h>
#include "../Commands/EditorCommands.h"
#include "ComponentEditUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// PropertiesPanel_AnimatorGraph.cpp
// Editor da maquina de estados do Animator: parametros, estados (nos arrastaveis),
// transicoes (setas clicaveis) com condicoes, eventos de animacao e root motion.
// Convencoes de undo (ver ComponentEditUtils.h): widgets continuos (texto, drag)
// usam TrackContinuousEdit; widgets discretos / mudancas estruturais fazem
// CommitComponentEdit com a copia 'before' tirada no inicio da funcao.

namespace PrismEditor {

    namespace {

        constexpr float kNodeW = 104.0f;
        constexpr float kNodeH = 30.0f;

        float DistanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
            ImVec2 ab(b.x - a.x, b.y - a.y), ap(p.x - a.x, p.y - a.y);
            float len2 = ab.x * ab.x + ab.y * ab.y;
            float t = len2 > 1e-6f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
            float dx = a.x + ab.x * t - p.x, dy = a.y + ab.y * t - p.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // Ponto na borda do retangulo de centro 'c' na direcao de 'toward'.
        ImVec2 RectEdge(ImVec2 c, ImVec2 toward) {
            float dx = toward.x - c.x, dy = toward.y - c.y;
            if (std::abs(dx) < 1e-4f && std::abs(dy) < 1e-4f)
                return c;
            float sx = std::abs(dx) > 1e-4f ? (kNodeW * 0.5f) / std::abs(dx) : 1e9f;
            float sy = std::abs(dy) > 1e-4f ? (kNodeH * 0.5f) / std::abs(dy) : 1e9f;
            float s = std::min(sx, sy);
            return ImVec2(c.x + dx * s, c.y + dy * s);
        }

        void DrawArrow(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float thickness) {
            dl->AddLine(a, b, color, thickness);
            ImVec2 d(b.x - a.x, b.y - a.y);
            float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len < 1.0f)
                return;
            d.x /= len; d.y /= len;
            ImVec2 n(-d.y, d.x);
            dl->AddTriangleFilled(b, ImVec2(b.x - d.x * 9 + n.x * 4.5f, b.y - d.y * 9 + n.y * 4.5f),
                                     ImVec2(b.x - d.x * 9 - n.x * 4.5f, b.y - d.y * 9 - n.y * 4.5f), color);
        }

        const char* CompareLabel(Prism::AnimCompare op) {
            switch (op) {
                case Prism::AnimCompare::Greater: return ">";
                case Prism::AnimCompare::Less: return "<";
                case Prism::AnimCompare::Equal: return "==";
                case Prism::AnimCompare::NotEqual: return "!=";
                case Prism::AnimCompare::IsTrue: return "verdadeiro";
                case Prism::AnimCompare::IsFalse: return "falso";
                case Prism::AnimCompare::Triggered: return "disparado";
            }
            return "?";
        }

        const Prism::AnimParameter* FindParam(const Prism::AnimGraph& g, const std::string& name) {
            for (const auto& p : g.Parameters)
                if (p.Name == name)
                    return &p;
            return nullptr;
        }

        std::string StateName(const Prism::AnimGraph& g, int index) {
            if (index == -1) return "Qualquer estado";
            return (index >= 0 && index < (int)g.States.size()) ? g.States[index].Name : "?";
        }

        void RemoveState(Prism::AnimGraph& g, int index) {
            g.States.erase(g.States.begin() + index);
            for (size_t i = 0; i < g.Transitions.size();) {
                auto& t = g.Transitions[i];
                if (t.From == index || t.To == index) {
                    g.Transitions.erase(g.Transitions.begin() + i);
                    continue;
                }
                if (t.From > index) t.From--;
                if (t.To > index) t.To--;
                i++;
            }
            if (g.EntryState == index) g.EntryState = 0;
            else if (g.EntryState > index) g.EntryState--;
        }

    }

    void PropertiesPanel::DrawAnimatorGraphUI(Prism::AnimatorComponent& animator, const std::vector<std::string>& clips) {
        Prism::AnimGraph& g = animator.Graph;
        const Prism::AnimatorComponent before = animator;
        auto commit = [&]() { CommitComponentEdit(m_Ctx, before, animator, "Animator"); };
        const int stateCount = (int)g.States.size();

        if (m_AnimSelState >= stateCount) m_AnimSelState = -1;
        if (m_AnimSelTransition >= (int)g.Transitions.size()) m_AnimSelTransition = -1;

        if (!ImGui::TreeNodeEx("Maquina de estados", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
            return;
        ImGui::TextDisabled("Com estados, o grafo decide qual clipe toca (o 'Clipe' acima so vale sem grafo).");

        // ---- Parametros -----------------------------------------------------
        if (ImGui::TreeNode("Parametros")) {
            static const char* kTypes[] = { "Float", "Bool", "Trigger" };
            int removeParam = -1;
            for (int i = 0; i < (int)g.Parameters.size(); i++) {
                ImGui::PushID(i);
                auto& p = g.Parameters[i];
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s", p.Name.c_str());
                ImGui::SetNextItemWidth(110);
                static std::string s_OldName;
                if (ImGui::InputText("##n", buf, sizeof(buf)))
                    p.Name = buf;
                if (ImGui::IsItemActivated()) {
                    m_AnimatorBeforeEdit = before; // 'before' = estado do inicio deste frame
                    s_OldName = before.Graph.Parameters[i].Name;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    for (auto& t : g.Transitions) // renomear tambem nas condicoes
                        for (auto& c : t.Conditions)
                            if (c.Param == s_OldName) c.Param = p.Name;
                    CommitComponentEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
                }
                ImGui::SameLine();
                int type = (int)p.Type;
                ImGui::SetNextItemWidth(80);
                if (ImGui::Combo("##t", &type, kTypes, 3)) { p.Type = (Prism::AnimParamType)type; commit(); }
                if (p.Type != Prism::AnimParamType::Trigger) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70);
                    if (p.Type == Prism::AnimParamType::Bool) {
                        bool v = p.Default > 0.5f;
                        if (ImGui::Checkbox("##d", &v)) { p.Default = v ? 1.0f : 0.0f; commit(); }
                    }
                    else {
                        ImGui::DragFloat("##d", &p.Default, 0.05f);
                        TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) removeParam = i;
                ImGui::PopID();
            }
            if (removeParam >= 0) { g.Parameters.erase(g.Parameters.begin() + removeParam); commit(); }
            if (ImGui::SmallButton("+ Parametro")) {
                Prism::AnimParameter p;
                p.Name = "param" + std::to_string(g.Parameters.size());
                g.Parameters.push_back(p);
                commit();
            }
            ImGui::TreePop();
        }

        // ---- Barra de ferramentas ------------------------------------------
        if (ImGui::SmallButton("+ Estado")) {
            Prism::AnimState st;
            st.Name = "Estado" + std::to_string(g.States.size());
            if (!clips.empty()) st.Clip = clips[0];
            st.EditorPos = glm::vec2(30.0f + 130.0f * (g.States.size() % 4), 60.0f + 60.0f * (g.States.size() / 4));
            g.States.push_back(st);
            m_AnimSelState = (int)g.States.size() - 1;
            m_AnimSelTransition = -1;
            commit();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_AnimSelState == -1 && m_AnimSelTransition < 0);
        if (ImGui::SmallButton("+ Transicao a partir do selecionado")) {
            // Destino: o proximo estado diferente do selecionado.
            if (stateCount >= 1) {
                Prism::AnimTransition t;
                t.From = m_AnimSelState == -2 ? -1 : (m_AnimSelState >= 0 ? m_AnimSelState : (m_AnimSelTransition >= 0 ? g.Transitions[m_AnimSelTransition].From : -1));
                t.To = 0;
                for (int i = 0; i < stateCount; i++)
                    if (i != t.From) { t.To = i; break; }
                g.Transitions.push_back(t);
                m_AnimSelTransition = (int)g.Transitions.size() - 1;
                m_AnimSelState = -1;
                commit();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_AnimSelState < 0 && m_AnimSelTransition < 0);
        if (ImGui::SmallButton("Remover selecionado")) {
            if (m_AnimSelTransition >= 0) g.Transitions.erase(g.Transitions.begin() + m_AnimSelTransition);
            else RemoveState(g, m_AnimSelState);
            m_AnimSelState = -1;
            m_AnimSelTransition = -1;
            commit();
        }
        ImGui::EndDisabled();

        // ---- Canvas ---------------------------------------------------------
        float maxX = 400.0f, maxY = 200.0f;
        for (const auto& st : g.States) {
            maxX = std::max(maxX, st.EditorPos.x + kNodeW + 40.0f);
            maxY = std::max(maxY, st.EditorPos.y + kNodeH + 40.0f);
        }
        ImGui::BeginChild("##AnimGraphCanvas", ImVec2(-1, 230), true, ImGuiWindowFlags_HorizontalScrollbar);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(maxX, maxY));
            const ImVec2 mouse = ImGui::GetMousePos();
            const bool canvasHovered = ImGui::IsWindowHovered();

            auto nodeCenter = [&](int i) { return ImVec2(origin.x + g.States[i].EditorPos.x + kNodeW * 0.5f, origin.y + g.States[i].EditorPos.y + kNodeH * 0.5f); };
            const ImVec2 anyCenter(origin.x + 10.0f + kNodeW * 0.5f, origin.y + 8.0f + kNodeH * 0.5f);
            auto fromCenter = [&](int from) { return from < 0 ? anyCenter : nodeCenter(from); };

            const ImU32 colArrow = ImGui::GetColorU32(ImVec4(0.7f, 0.7f, 0.7f, 1));
            const ImU32 colSel = ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.2f, 1));

            // Setas (separadas quando ha par de ida e volta).
            int hitTransition = -1;
            float hitDist = 7.0f;
            for (int i = 0; i < (int)g.Transitions.size(); i++) {
                const auto& t = g.Transitions[i];
                if (t.From >= stateCount || t.To < 0 || t.To >= stateCount) continue;
                ImVec2 a = fromCenter(t.From), b = nodeCenter(t.To);
                ImVec2 d(b.x - a.x, b.y - a.y);
                float len = std::max(std::sqrt(d.x * d.x + d.y * d.y), 1.0f);
                ImVec2 n(-d.y / len * 5.0f, d.x / len * 5.0f);
                a = ImVec2(a.x + n.x, a.y + n.y); b = ImVec2(b.x + n.x, b.y + n.y);
                ImVec2 ea = RectEdge(a, b), eb = RectEdge(b, a);
                bool selected = i == m_AnimSelTransition;
                DrawArrow(dl, ea, eb, selected ? colSel : colArrow, selected ? 2.5f : 1.5f);
                float dist = DistanceToSegment(mouse, ea, eb);
                if (canvasHovered && dist < hitDist) { hitDist = dist; hitTransition = i; }
            }

            // Caixa "Qualquer estado" (fixa).
            {
                ImVec2 p0(anyCenter.x - kNodeW * 0.5f, anyCenter.y - kNodeH * 0.5f), p1(anyCenter.x + kNodeW * 0.5f, anyCenter.y + kNodeH * 0.5f);
                dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(0.25f, 0.18f, 0.3f, 1)), 4.0f);
                dl->AddRect(p0, p1, m_AnimSelState == -2 ? colSel : ImGui::GetColorU32(ImVec4(0.6f, 0.4f, 0.7f, 1)), 4.0f, 0, m_AnimSelState == -2 ? 2.5f : 1.0f);
                dl->AddText(ImVec2(p0.x + 8, p0.y + 7), IM_COL32_WHITE, "Qualquer estado");
                ImGui::SetCursorScreenPos(p0);
                ImGui::InvisibleButton("##any", ImVec2(kNodeW, kNodeH));
                if (ImGui::IsItemClicked()) { m_AnimSelState = -2; m_AnimSelTransition = -1; }
            }

            // Estados.
            bool clickedNode = false;
            for (int i = 0; i < stateCount; i++) {
                auto& st = g.States[i];
                ImVec2 p0(origin.x + st.EditorPos.x, origin.y + st.EditorPos.y), p1(p0.x + kNodeW, p0.y + kNodeH);
                const bool entry = g.EntryState == i;
                const bool running = animator.Runtime.Ptr && animator.Runtime.Ptr->State == i && animator.Runtime.Ptr->Playing;
                dl->AddRectFilled(p0, p1, ImGui::GetColorU32(running ? ImVec4(0.2f, 0.4f, 0.25f, 1) : ImVec4(0.2f, 0.25f, 0.33f, 1)), 4.0f);
                dl->AddRect(p0, p1, i == m_AnimSelState ? colSel : (entry ? ImGui::GetColorU32(ImVec4(0.3f, 0.85f, 0.4f, 1)) : ImGui::GetColorU32(ImVec4(0.5f, 0.6f, 0.75f, 1))),
                            4.0f, 0, (i == m_AnimSelState || entry) ? 2.5f : 1.0f);
                dl->PushClipRect(p0, p1, true);
                dl->AddText(ImVec2(p0.x + 6, p0.y + 7), IM_COL32_WHITE, st.Name.c_str());
                dl->PopClipRect();

                ImGui::SetCursorScreenPos(p0);
                ImGui::PushID(i);
                ImGui::InvisibleButton("##node", ImVec2(kNodeW, kNodeH));
                if (ImGui::IsItemClicked()) { m_AnimSelState = i; m_AnimSelTransition = -1; clickedNode = true; }
                if (ImGui::IsItemActivated()) m_AnimatorBeforeEdit = before;
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    st.EditorPos.x = std::clamp(st.EditorPos.x + delta.x, 0.0f, 4000.0f);
                    st.EditorPos.y = std::clamp(st.EditorPos.y + delta.y, 0.0f, 4000.0f);
                }
                if (ImGui::IsItemDeactivated() && i < (int)m_AnimatorBeforeEdit.Graph.States.size()
                    && m_AnimatorBeforeEdit.Graph.States[i].EditorPos != st.EditorPos)
                    CommitComponentEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
                ImGui::PopID();
            }
            if (!clickedNode && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hitTransition >= 0) {
                m_AnimSelTransition = hitTransition;
                m_AnimSelState = -1;
            }
        }
        ImGui::EndChild();

        // ---- Inspetor do estado / transicao ---------------------------------
        if (m_AnimSelState >= 0 && m_AnimSelState < stateCount) {
            auto& st = g.States[m_AnimSelState];
            ImGui::SeparatorText("Estado");
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", st.Name.c_str());
            if (ImGui::InputText("Nome", buf, sizeof(buf)))
                st.Name = buf;
            TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
            if (ImGui::BeginCombo("Clipe##state", st.Clip.empty() ? "(nenhum)" : st.Clip.c_str())) {
                for (const std::string& c : clips)
                    if (ImGui::Selectable(c.c_str(), c == st.Clip)) { st.Clip = c; commit(); }
                ImGui::EndCombo();
            }
            if (ImGui::Checkbox("Repetir##state", &st.Loop)) commit();
            ImGui::SliderFloat("Velocidade##state", &st.Speed, 0.0f, 4.0f, "%.2fx");
            TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
            if (g.EntryState != m_AnimSelState) {
                if (ImGui::SmallButton("Definir como estado de entrada")) { g.EntryState = m_AnimSelState; commit(); }
            }
            else ImGui::TextDisabled("Estado de entrada (borda verde).");
        }
        else if (m_AnimSelTransition >= 0 && m_AnimSelTransition < (int)g.Transitions.size()) {
            auto& t = g.Transitions[m_AnimSelTransition];
            ImGui::SeparatorText("Transicao");
            if (ImGui::BeginCombo("De", StateName(g, t.From).c_str())) {
                if (ImGui::Selectable("Qualquer estado", t.From == -1)) { t.From = -1; commit(); }
                for (int i = 0; i < stateCount; i++)
                    if (ImGui::Selectable(g.States[i].Name.c_str(), t.From == i)) { t.From = i; commit(); }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("Para", StateName(g, t.To).c_str())) {
                for (int i = 0; i < stateCount; i++)
                    if (ImGui::Selectable(g.States[i].Name.c_str(), t.To == i)) { t.To = i; commit(); }
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Crossfade (s)", &t.Fade, 0.0f, 1.0f, "%.2f");
            TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
            if (ImGui::Checkbox("Esperar o fim do clipe (tempo de saida)", &t.HasExitTime)) commit();
            if (t.HasExitTime) {
                ImGui::SliderFloat("Tempo de saida (0..1)", &t.ExitTime, 0.0f, 1.0f, "%.2f");
                TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
            }
            if (t.Conditions.empty() && !t.HasExitTime)
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1), "Sem condicao nem tempo de saida: esta transicao nunca dispara.");

            int removeCond = -1;
            for (int i = 0; i < (int)t.Conditions.size(); i++) {
                ImGui::PushID(1000 + i);
                auto& c = t.Conditions[i];
                const Prism::AnimParameter* param = FindParam(g, c.Param);
                ImGui::SetNextItemWidth(100);
                if (ImGui::BeginCombo("##p", c.Param.empty() ? "(parametro)" : c.Param.c_str())) {
                    for (const auto& p : g.Parameters)
                        if (ImGui::Selectable(p.Name.c_str(), p.Name == c.Param)) {
                            c.Param = p.Name;
                            c.Op = p.Type == Prism::AnimParamType::Float ? Prism::AnimCompare::Greater
                                 : p.Type == Prism::AnimParamType::Bool ? Prism::AnimCompare::IsTrue : Prism::AnimCompare::Triggered;
                            commit();
                        }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (!param) {
                    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "parametro inexistente");
                }
                else if (param->Type == Prism::AnimParamType::Float) {
                    static const Prism::AnimCompare kOps[] = { Prism::AnimCompare::Greater, Prism::AnimCompare::Less, Prism::AnimCompare::Equal, Prism::AnimCompare::NotEqual };
                    ImGui::SetNextItemWidth(60);
                    if (ImGui::BeginCombo("##o", CompareLabel(c.Op))) {
                        for (auto op : kOps)
                            if (ImGui::Selectable(CompareLabel(op), c.Op == op)) { c.Op = op; commit(); }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70);
                    ImGui::DragFloat("##v", &c.Value, 0.05f);
                    TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
                }
                else if (param->Type == Prism::AnimParamType::Bool) {
                    ImGui::SetNextItemWidth(90);
                    if (ImGui::BeginCombo("##o", CompareLabel(c.Op))) {
                        if (ImGui::Selectable("verdadeiro", c.Op == Prism::AnimCompare::IsTrue)) { c.Op = Prism::AnimCompare::IsTrue; commit(); }
                        if (ImGui::Selectable("falso", c.Op == Prism::AnimCompare::IsFalse)) { c.Op = Prism::AnimCompare::IsFalse; commit(); }
                        ImGui::EndCombo();
                    }
                }
                else {
                    ImGui::TextDisabled("disparado");
                    c.Op = Prism::AnimCompare::Triggered;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) removeCond = i;
                ImGui::PopID();
            }
            if (removeCond >= 0) { t.Conditions.erase(t.Conditions.begin() + removeCond); commit(); }
            if (ImGui::SmallButton("+ Condicao") && !g.Parameters.empty()) {
                Prism::AnimCondition c;
                c.Param = g.Parameters[0].Name;
                c.Op = g.Parameters[0].Type == Prism::AnimParamType::Float ? Prism::AnimCompare::Greater
                     : g.Parameters[0].Type == Prism::AnimParamType::Bool ? Prism::AnimCompare::IsTrue : Prism::AnimCompare::Triggered;
                t.Conditions.push_back(c);
                commit();
            }
        }
        else if (m_AnimSelState == -2) {
            ImGui::TextDisabled("\"Qualquer estado\": use '+ Transicao' para criar uma saida valida de qualquer estado (ex.: pular).");
        }

        // ---- Previa / valores ao vivo ---------------------------------------
        ImGui::SeparatorText("Teste");
        if (!m_Ctx.ActiveScene->IsRunning()) {
            if (ImGui::SmallButton("Tocar grafo (previa)##g")) Prism::AnimationSystem::StartGraph(m_Ctx.SelectedEntity);
            ImGui::SameLine();
            if (ImGui::SmallButton("Parar##g")) Prism::AnimationSystem::Stop(m_Ctx.SelectedEntity);
        }
        const std::string current = Prism::AnimationSystem::GetState(m_Ctx.SelectedEntity);
        ImGui::TextDisabled("Estado atual: %s", current.empty() ? "(grafo inativo)" : current.c_str());
        for (const auto& p : g.Parameters) {
            ImGui::PushID(p.Name.c_str());
            if (p.Type == Prism::AnimParamType::Float) {
                float v = Prism::AnimationSystem::GetFloat(m_Ctx.SelectedEntity, p.Name);
                if (ImGui::DragFloat(p.Name.c_str(), &v, 0.05f)) Prism::AnimationSystem::SetFloat(m_Ctx.SelectedEntity, p.Name, v);
            }
            else if (p.Type == Prism::AnimParamType::Bool) {
                bool v = Prism::AnimationSystem::GetFloat(m_Ctx.SelectedEntity, p.Name) > 0.5f;
                if (ImGui::Checkbox(p.Name.c_str(), &v)) Prism::AnimationSystem::SetBool(m_Ctx.SelectedEntity, p.Name, v);
            }
            else if (ImGui::SmallButton(("Disparar " + p.Name).c_str())) {
                Prism::AnimationSystem::SetTrigger(m_Ctx.SelectedEntity, p.Name);
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

}
