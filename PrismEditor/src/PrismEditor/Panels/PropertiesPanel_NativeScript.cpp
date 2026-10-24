#include "PropertiesPanel.h"
#include "ComponentEditUtils.h"
#include "../Commands/EditorCommands.h"
#include "../Play/PlayWindow.h"
#include <imgui.h>
#include <Prism/Scripting/NativeScripting.h>
#include <algorithm>
#include <cstdio>
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
            else if (m_Ctx.IsPlaying()) {
                ImGui::TextDisabled("Play em andamento - Compilar troca o codigo sem parar (os scripts recomecam).");
            }

            // --- Variaveis publicas (ScriptableEntity::OnProperties) ---
            if (!script.ClassName.empty() && Prism::NativeScripting::HasClass(script.ClassName)) {
                const auto& declared = Prism::NativeScripting::ReadClassProperties(script.ClassName);
                if (!declared.empty()) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Variaveis (OnProperties)");
                    for (const auto& defaults : declared) {
                        ImGui::PushID(defaults.Name.c_str());

                        auto findOverride = [&]() -> Prism::ScriptPropertyValue* {
                            for (auto& value : script.Properties)
                                if (value.Name == defaults.Name && value.Type == defaults.Type)
                                    return &value;
                            return nullptr;
                        };
                        auto getOrCreate = [&]() -> Prism::ScriptPropertyValue& {
                            if (auto* existing = findOverride())
                                return *existing;
                            script.Properties.push_back(defaults);
                            return script.Properties.back();
                        };

                        Prism::ScriptPropertyValue* current = findOverride();
                        const Prism::ScriptPropertyValue& shown = current ? *current : defaults;
                        const bool overridden = current != nullptr;

                        if (defaults.Type == Prism::ScriptPropertyType::Number) {
                            float number = (float)shown.Number;
                            if (ImGui::DragFloat(defaults.Name.c_str(), &number, 0.05f))
                                getOrCreate().Number = number;
                            TrackContinuousEdit(m_Ctx, m_NativeScriptBeforeEdit, script, "Native Script");
                        }
                        else if (defaults.Type == Prism::ScriptPropertyType::Bool) {
                            bool flag = shown.Bool;
                            auto before = script;
                            if (ImGui::Checkbox(defaults.Name.c_str(), &flag)) {
                                getOrCreate().Bool = flag;
                                CommitComponentEdit(m_Ctx, before, script, "Native Script");
                            }
                        }
                        else {
                            char buffer[256];
                            std::snprintf(buffer, sizeof(buffer), "%s", shown.String.c_str());
                            if (ImGui::InputText(defaults.Name.c_str(), buffer, sizeof(buffer)))
                                getOrCreate().String = buffer;
                            TrackContinuousEdit(m_Ctx, m_NativeScriptBeforeEdit, script, "Native Script");
                        }

                        if (overridden) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Reset")) {
                                auto before = script;
                                script.Properties.erase(
                                    std::remove_if(script.Properties.begin(), script.Properties.end(),
                                        [&](const Prism::ScriptPropertyValue& v) { return v.Name == defaults.Name && v.Type == defaults.Type; }),
                                    script.Properties.end());
                                CommitComponentEdit(m_Ctx, before, script, "Native Script");
                            }
                        }
                        ImGui::PopID();
                    }
                }
            }
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::NativeScriptComponent>>(m_Ctx.SelectedEntity, "Native Script"));
    }

}
