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
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ScriptComponent>>(m_Ctx.SelectedEntity, "Script"));
    }

}
