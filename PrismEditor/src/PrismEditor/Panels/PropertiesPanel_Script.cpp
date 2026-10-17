#include "PropertiesPanel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "ContentBrowserPanel.h"
#include "ScriptEditorPanel.h"
#include "../Play/PlayWindow.h"
#include "ComponentEditUtils.h"
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

// PropertiesPanel_Script.cpp
// UI do ScriptComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawScriptUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawScriptUI() {
        auto& script = m_Ctx.SelectedEntity.GetComponent<Prism::ScriptComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Script", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            // Combo com os .lua ja existentes em Scripts/ - evita ter
            // que saber/digitar o nome de cor (ver ListProjectScripts).
            // "(nenhum)" e sempre a primeira opcao, para poder limpar
            // ScriptPath sem sair do combo.
            std::vector<std::string> availableScripts = ListProjectScripts();

            std::string previewLabel = script.ScriptPath.empty() ? "(nenhum)" : script.ScriptPath;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##ScriptSelect", previewLabel.c_str())) {
                bool noneSelected = script.ScriptPath.empty();
                if (ImGui::Selectable("(nenhum)", noneSelected))
                    script.ScriptPath.clear();

                for (const auto& scriptFile : availableScripts) {
                    bool selected = (script.ScriptPath == scriptFile);
                    if (ImGui::Selectable(scriptFile.c_str(), selected))
                        script.ScriptPath = scriptFile;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                if (availableScripts.empty())
                    ImGui::TextDisabled("Nenhum .lua em Scripts/ ainda.");

                ImGui::EndCombo();
            }

            // "Novo..." abre o popup que cria o arquivo (ver
            // RenderNewScriptPopup/CreateNewScript) e ja atribui a esta
            // entidade. "Editar" so aparece com um script ja escolhido -
            // abre o ScriptEditorPanel nele (ver Panels/ScriptEditorPanel.h).
            if (ImGui::Button("Novo...")) {
                m_NewScriptNameBuffer[0] = '\0';
                m_ShowNewScriptPopup = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(script.ScriptPath.empty());
            if (ImGui::Button("Editar")) {
                auto project = Prism::Project::GetActive();
                if (project)
                    m_Ctx.ScriptEditor->Open(project->GetScriptDirectory() / script.ScriptPath);
            }
            ImGui::EndDisabled();

            if (script.ScriptPath.empty()) {
                ImGui::TextDisabled("Nenhum arquivo escolhido ainda.");
            }
            else if (m_Ctx.Play->IsOpen()) {
                // A PlayWindow roda uma COPIA clonada da Scene (ver
                // Play/PlayWindow.h) - EditorContext::SelectedEntity pertence a
                // Scene de EDICAO, uma entidade DIFERENTE (ainda que
                // correspondente) da que esta rodando de verdade la
                // dentro. Nao ha como "recarregar" um script individual
                // remotamente na PlayWindow a partir daqui - o jeito de
                // aplicar uma mudanca no arquivo .lua e Parar e apertar
                // Play de novo (que clona a Scene do zero, incluindo o
                // arquivo .lua atualizado do disco).
                ImGui::TextDisabled("Play em andamento - Pare e aperte Play de novo para recarregar.");
            }
            else {
                ImGui::TextDisabled("Aperte Play (menu bar) para rodar este script.");
            }

            // --- Variaveis publicas (tabela global `Properties` do script) ---
            if (!script.ScriptPath.empty()) {
                auto project = Prism::Project::GetActive();
                if (project) {
                    std::filesystem::path absolutePath = project->GetScriptDirectory() / script.ScriptPath;
                    std::error_code ec;
                    auto writeTime = std::filesystem::last_write_time(absolutePath, ec);
                    if (!ec && (m_ScriptPropsPath != script.ScriptPath || m_ScriptPropsTime != writeTime)) {
                        m_ScriptPropsPath = script.ScriptPath;
                        m_ScriptPropsTime = writeTime;
                        m_ScriptPropsDefaults = Prism::ScriptEngine::ReadScriptProperties(absolutePath);
                    }
                }

                if (!m_ScriptPropsDefaults.empty() && m_ScriptPropsPath == script.ScriptPath) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Variaveis (Properties)");
                    for (const auto& defaults : m_ScriptPropsDefaults) {
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
                        bool overridden = current != nullptr;

                        if (defaults.Type == Prism::ScriptPropertyType::Number) {
                            float number = (float)shown.Number;
                            if (ImGui::DragFloat(defaults.Name.c_str(), &number, 0.05f)) {

                                getOrCreate().Number = number;
                            }
                            TrackContinuousEdit(m_Ctx, m_ScriptBeforeEdit, script, "Script");
                        }
                        else if (defaults.Type == Prism::ScriptPropertyType::Bool) {
                            bool flag = shown.Bool;
                            auto before = script;
                            if (ImGui::Checkbox(defaults.Name.c_str(), &flag)) {
                                getOrCreate().Bool = flag;
                                CommitComponentEdit(m_Ctx, before, script, "Script");
                            }
                        }
                        else {
                            char buffer[256];
                            std::snprintf(buffer, sizeof(buffer), "%s", shown.String.c_str());
                            if (ImGui::InputText(defaults.Name.c_str(), buffer, sizeof(buffer)))
                                getOrCreate().String = buffer;
                            TrackContinuousEdit(m_Ctx, m_ScriptBeforeEdit, script, "Script");
                        }

                        if (overridden) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Reset")) {
                                auto before = script;
                                script.Properties.erase(
                                    std::remove_if(script.Properties.begin(), script.Properties.end(),
                                        [&](const Prism::ScriptPropertyValue& v) { return v.Name == defaults.Name && v.Type == defaults.Type; }),
                                    script.Properties.end());
                                CommitComponentEdit(m_Ctx, before, script, "Script");
                            }
                        }
                        ImGui::PopID();
                    }
                }
            }
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ScriptComponent>>(m_Ctx.SelectedEntity, "Script"));
    }

}
