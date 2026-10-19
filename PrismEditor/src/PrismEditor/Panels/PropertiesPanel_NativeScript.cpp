#include "PropertiesPanel.h"
#include "ComponentEditUtils.h"
#include "../Commands/EditorCommands.h"
#include "../Play/PlayWindow.h"
#include <imgui.h>
#include <Prism/Scripting/NativeScripting.h>
#include <string>

// PropertiesPanel_NativeScript.cpp
// UI do NativeScriptComponent (script em C++): combo com as classes do modulo
// compilado do projeto (botao "Compilar" da barra superior).

namespace PrismEditor {

    void PropertiesPanel::DrawNativeScriptUI() {
        auto& script = m_Ctx.SelectedEntity.GetComponent<Prism::NativeScriptComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Native Script (C++)", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& classNames = Prism::NativeScripting::GetClassNames();
            std::string preview = script.ClassName.empty() ? "(nenhuma)" : script.ClassName;

            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##NativeClass", preview.c_str())) {
                if (ImGui::Selectable("(nenhuma)", script.ClassName.empty()) && !script.ClassName.empty()) {
                    auto before = script;
                    script.ClassName.clear();
                    CommitComponentEdit(m_Ctx, before, script, "Native Script");
                }
                for (const std::string& name : classNames) {
                    bool selected = script.ClassName == name;
                    if (ImGui::Selectable(name.c_str(), selected) && !selected) {
                        auto before = script;
                        script.ClassName = name;
                        CommitComponentEdit(m_Ctx, before, script, "Native Script");
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                if (classNames.empty())
                    ImGui::TextDisabled("Nenhuma classe. Escreva em Source/ e clique em Compilar na barra superior.");
                ImGui::EndCombo();
            }

            if (!script.ClassName.empty() && !Prism::NativeScripting::HasClass(script.ClassName)) {
                ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f),
                    "Classe '%s' nao esta no modulo carregado.", script.ClassName.c_str());
                ImGui::TextDisabled("Clique em Compilar na barra superior.");
            }
            else if (m_Ctx.Play->IsOpen()) {
                ImGui::TextDisabled("Play em andamento - Pare e aperte Play de novo para aplicar mudancas.");
            }
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::NativeScriptComponent>>(m_Ctx.SelectedEntity, "Native Script"));
    }

}
