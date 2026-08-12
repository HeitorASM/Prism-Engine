#include "ScriptEditorPanel.h"
#include <Prism.h>
#include <imgui.h>
#include <fstream>
#include <sstream>

namespace PrismEditor {

    void ScriptEditorPanel::Open(const std::filesystem::path& scriptAbsolutePath) {
        m_CurrentPath = scriptAbsolutePath;
        m_Dirty = false;

        std::string content;
        std::ifstream file(scriptAbsolutePath);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            content = buffer.str();
        }
        // Se o arquivo nao existe ainda (ex: recem "criado" via
        // EditorLayer::CreateNewScript, que so grava um template - ver
        // comentario la), content fica vazio - nao e um erro, o primeiro
        // Save() cria o arquivo.

        m_Editor.SetText(content);
        m_Editor.SetLanguage(TextEditor::Language::Lua());

        // SetChangeCallback e reatribuido a cada Open() (nao so uma vez no
        // construtor) de proposito: a lambda captura 'this' por ponteiro,
        // entao reatribuir aqui e barato e garante que o callback sempre
        // aponta para o m_Editor certo, mesmo que a lib limpe callbacks
        // internamente ao trocar de texto via SetText() (nao documentado
        // que isso NAO aconteca, e reatribuir e mais seguro do que assumir).
        // Debounce de 0ms: queremos marcar "sujo" imediatamente, nao um
        // tempo depois - o debounce da lib existe para casos como
        // recompilar/revalidar em tempo real, que nao e o nosso caso aqui.
        m_Editor.SetChangeCallback([this]() {
            m_Dirty = true;
        }, 0);
    }

    bool ScriptEditorPanel::Save() {
        if (!IsOpen())
            return false;

        std::ofstream file(m_CurrentPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            PRISM_CORE_ERROR("ScriptEditorPanel: falha ao salvar '", m_CurrentPath.string(), "' (sem permissao ou pasta nao existe?).");
            return false;
        }

        file << m_Editor.GetText();
        file.close();

        m_Dirty = false;
        PRISM_CORE_INFO("ScriptEditorPanel: script salvo em '", m_CurrentPath.string(), "'.");
        return true;
    }

    void ScriptEditorPanel::OnImGuiRender() {
        // Titulo com "*" quando ha mudancas nao salvas - mesmo sinal visual
        // que editores de codigo tradicionais usam (VS Code, Visual Studio,
        // etc), reconhecivel sem precisar de legenda.
        std::string title = "Editor de Script";
        if (IsOpen())
            title += " - " + m_CurrentPath.filename().string() + (m_Dirty ? " *" : "");
        title += "###ScriptEditorPanel"; // ID fixo (apos ###) - o titulo visivel muda, mas ImGui trata como a MESMA janela (docking/posicao preservados)

        ImGui::Begin(title.c_str());

        if (!IsOpen()) {
            ImGui::TextDisabled("Nenhum script aberto - clique em \"Editar\" no componente Script de uma entidade (Properties panel).");
            ImGui::End();
            return;
        }

        // --- barra de ferramentas -------------------------------------
        // Desabilitado quando nao ha mudancas (m_Dirty == false) - evita
        // gravar o arquivo sem necessidade a cada clique.
        ImGui::BeginDisabled(!m_Dirty);
        if (ImGui::Button("Salvar (Ctrl+S)"))
            Save();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_CurrentPath.string().c_str());

        // Ctrl+S salva mesmo com o foco dentro do TextEditor abaixo - o
        // proprio TextEditor nao reserva Ctrl+S para nada (ver mapeamento
        // de atalhos no README da lib), entao verificar isso aqui - fora
        // do Render() - e seguro.
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            Save();

        ImGui::Separator();

        // --- area de texto ------------------------------------------------
        // TextEditor::Render desenha o editor de codigo completo (com
        // highlight de Lua, numeros de linha, undo/redo nativo etc) numa
        // child window interna - ver goossens/ImGuiColorTextEdit no
        // vendor/CMakeLists.txt para detalhes da lib.
        //
        // O ID passado ("##ScriptEditorBuffer_" + caminho) inclui o
        // caminho do arquivo: como so existe UMA instancia de TextEditor
        // neste painel (m_Editor) reaproveitada entre arquivos diferentes,
        // variar o ID evita qualquer estado de foco/scroll residual do
        // ImGui vazando de um arquivo pro outro ao trocar (mesma logica
        // que corrigiu o crash da versao anterior deste painel, baseada em
        // InputTextMultiline).
        ImVec2 avail = ImGui::GetContentRegionAvail();
        m_Editor.Render(("##ScriptEditorBuffer_" + m_CurrentPath.string()).c_str(), ImVec2(avail.x, avail.y));

        ImGui::End();
    }

}
