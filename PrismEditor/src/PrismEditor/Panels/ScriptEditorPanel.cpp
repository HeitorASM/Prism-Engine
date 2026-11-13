#include "ScriptEditorPanel.h"
#include <Prism.h>
#include <imgui.h>
#include <imgui_internal.h> // ImGuiSettingsHandler (zoom salvo no imgui.ini)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace PrismEditor {

    namespace {
        const std::filesystem::path kNoPath;

        // Zoom do texto. Vive no escopo do arquivo (e nao num membro) de proposito: o handler do
        // imgui.ini abaixo fica registrado no contexto do ImGui ate o fim do programa e nao pode
        // guardar um ponteiro para um painel que morre ao fechar o projeto.
        float s_CodeZoom = 1.0f;
        bool s_ZoomHandlerRegistered = false;

        // Persistencia: [PrismCodeEditor][Main] / Zoom=1.25 no imgui.ini.
        void* ZoomReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
            return std::strcmp(name, "Main") == 0 ? (void*)&s_CodeZoom : nullptr;
        }
        void ZoomReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
            float value = 0.0f;
            if (std::sscanf(line, "Zoom=%f", &value) == 1 && std::isfinite(value))
                s_CodeZoom = std::clamp(value, ScriptEditorPanel::kMinZoom, ScriptEditorPanel::kMaxZoom);
        }
        void ZoomWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
            out->appendf("[%s][Main]\nZoom=%.2f\n\n", handler->TypeName, s_CodeZoom);
        }
    }

    // O ImGui le o imgui.ini UMA vez, no primeiro NewFrame, e descarta o que nenhum handler
    // reconhece: por isso o registro precisa vir antes disso (ver EditorApp.cpp). Chamado de
    // novo por OnImGuiRender como rede de seguranca (nesse caso o zoom salvo so vale na proxima sessao).
    void ScriptEditorPanel::RegisterSettings() {
        if (s_ZoomHandlerRegistered || !ImGui::GetCurrentContext())
            return;
        s_ZoomHandlerRegistered = true;
        ImGuiSettingsHandler handler;
        handler.TypeName = "PrismCodeEditor";
        handler.TypeHash = ImHashStr(handler.TypeName);
        handler.ReadOpenFn = ZoomReadOpen;
        handler.ReadLineFn = ZoomReadLine;
        handler.WriteAllFn = ZoomWriteAll;
        ImGui::AddSettingsHandler(&handler);
    }

    float ScriptEditorPanel::GetZoom() { return s_CodeZoom; }

    void ScriptEditorPanel::SetZoom(float zoom) {
        if (!std::isfinite(zoom))
            return;
        // Arredonda a 5%: passos repetidos da roda nao acumulam erro de float (0.1 + 0.1 + ...).
        zoom = std::round(std::clamp(zoom, kMinZoom, kMaxZoom) * 20.0f) / 20.0f;
        if (zoom == s_CodeZoom)
            return;
        s_CodeZoom = zoom;
        if (ImGui::GetCurrentContext())
            ImGui::MarkIniSettingsDirty();
    }

    bool ScriptEditorPanel::LooksLikeTextFile(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return true; // arquivo novo (ex: script recem criado): abre vazio
        auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 4u * 1024u * 1024u)
            return false;
        std::ifstream file(path, std::ios::binary);
        char block[4096];
        file.read(block, sizeof(block));
        std::streamsize count = file.gcount();
        for (std::streamsize i = 0; i < count; i++)
            if (block[i] == '\0')
                return false;
        return true;
    }

    const std::filesystem::path& ScriptEditorPanel::GetCurrentPath() const {
        if (m_Active >= 0 && m_Active < (int)m_Documents.size())
            return m_Documents[m_Active]->Path;
        return kNoPath;
    }

    int ScriptEditorPanel::FindDocument(const std::filesystem::path& path) const {
        std::filesystem::path wanted = path.lexically_normal();
        for (int i = 0; i < (int)m_Documents.size(); i++)
            if (m_Documents[i]->Path.lexically_normal() == wanted)
                return i;
        return -1;
    }

    bool ScriptEditorPanel::AnyDirty() const {
        for (const auto& document : m_Documents)
            if (document->Dirty)
                return true;
        return false;
    }

    void ScriptEditorPanel::LoadInto(Document& document, const std::filesystem::path& path) {
        document.Path = path;
        document.Dirty = false;

        std::string content;
        std::ifstream file(path, std::ios::binary);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            content = buffer.str();
        }
        // Arquivo inexistente (script recem "criado"): content fica vazio; o primeiro Save() cria.
        document.Crlf = content.find("\r\n") != std::string::npos;

        document.Editor.SetText(content);

        // Highlight so para Lua e C/C++ (por extensao); o resto e texto puro.
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if (extension == ".lua")
            document.Editor.SetLanguage(TextEditor::LookupLanguage("Lua"));
        else if (extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".hpp" || extension == ".hxx" || extension == ".h" || extension == ".inl")
            document.Editor.SetLanguage(TextEditor::LookupLanguage("C++"));
        else if (extension == ".c")
            document.Editor.SetLanguage(TextEditor::LookupLanguage("C"));
        else
            document.Editor.SetLanguage(nullptr);

        // A lambda captura o Document* (estavel: os documentos vivem em unique_ptr).
        // Debounce 0: marca "sujo" imediatamente.
        Document* self = &document;
        document.Editor.SetChangeCallback([self]() { self->Dirty = true; }, 0);
    }

    bool ScriptEditorPanel::Open(const std::filesystem::path& path) {
        int existing = FindDocument(path);
        if (existing >= 0) {
            m_Active = existing;
            return true;
        }
        if (!LooksLikeTextFile(path)) {
            PRISM_CORE_WARN("Editor de Codigo: '", path.filename().string(), "' nao parece um arquivo de texto (binario ou maior que 4 MB); nao aberto.");
            return false;
        }
        auto document = std::make_unique<Document>();
        LoadInto(*document, path);
        m_Documents.push_back(std::move(document));
        m_Active = (int)m_Documents.size() - 1;
        return true;
    }

    bool ScriptEditorPanel::SaveDocument(Document& document) {
        std::ofstream file(document.Path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            PRISM_CORE_ERROR("Editor de Codigo: falha ao salvar '", document.Path.string(), "' (sem permissao ou pasta nao existe?).");
            return false;
        }

        std::string text = document.Editor.GetText();
        if (document.Crlf) {
            std::string converted;
            converted.reserve(text.size() + text.size() / 20);
            for (char ch : text) {
                if (ch == '\n')
                    converted += "\r\n";
                else
                    converted += ch;
            }
            text.swap(converted);
        }
        file << text;
        file.close();

        document.Dirty = false;
        PRISM_CORE_INFO("Editor de Codigo: '", document.Path.filename().string(), "' salvo.");
        return true;
    }

    bool ScriptEditorPanel::Save() {
        if (m_Active < 0 || m_Active >= (int)m_Documents.size())
            return false;
        return SaveDocument(*m_Documents[m_Active]);
    }

    bool ScriptEditorPanel::SaveAll() {
        bool ok = true;
        for (auto& document : m_Documents)
            if (document->Dirty)
                ok = SaveDocument(*document) && ok;
        return ok;
    }

    void ScriptEditorPanel::CloseDocument(int index) {
        if (index < 0 || index >= (int)m_Documents.size())
            return;
        m_Documents.erase(m_Documents.begin() + index);
        if (m_Documents.empty())
            m_Active = -1;
        else if (m_Active > index)
            m_Active--;                                              // a lista andou uma posicao
        else if (m_Active == index)
            m_Active = std::min(index, (int)m_Documents.size() - 1); // ativa o vizinho
        // m_Active < index: continua valido
    }

    void ScriptEditorPanel::OnImGuiRender() {
        // Titulo com o arquivo ativo e "*" se ha mudancas nao salvas. "###" fixa o ID:
        // o texto visivel muda, mas o ImGui trata como a MESMA janela (docking/posicao preservados).
        std::string title = "Editor de Codigo";
        if (m_Active >= 0 && m_Active < (int)m_Documents.size())
            title += " - " + m_Documents[m_Active]->Path.filename().string() + (AnyDirty() ? " *" : "");
        title += "###ScriptEditorPanel";

        RegisterSettings(); // rede de seguranca (normalmente ja registrado em EditorApp.cpp)

        ImGui::Begin(title.c_str());
        m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        // --- confirmacao ao fechar um arquivo com alteracoes ---------------
        if (m_OpenClosePopup) {
            ImGui::OpenPopup("Alteracoes nao salvas##CodeEditor");
            m_OpenClosePopup = false;
        }
        if (ImGui::BeginPopupModal("Alteracoes nao salvas##CodeEditor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (m_PendingClose >= 0 && m_PendingClose < (int)m_Documents.size()) {
                ImGui::Text("Salvar alteracoes em '%s' antes de fechar?", m_Documents[m_PendingClose]->Path.filename().string().c_str());
                if (ImGui::Button("Salvar e fechar")) {
                    if (SaveDocument(*m_Documents[m_PendingClose])) {
                        CloseDocument(m_PendingClose);
                        m_PendingClose = -1;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Descartar e fechar")) {
                    CloseDocument(m_PendingClose);
                    m_PendingClose = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("Cancelar") || m_PendingClose < 0) {
                m_PendingClose = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (m_Documents.empty()) {
            ImGui::TextDisabled("Nenhum arquivo aberto - de duplo clique num arquivo de texto no Conteudo do Projeto (.lua, .cpp, .md, .txt...) ou clique em \"Editar\" no componente Script.");
            ImGui::End();
            return;
        }

        // --- lateral: arquivos abertos -------------------------------------
        int closeRequest = -1;
        ImGui::BeginChild("##OpenFiles", ImVec2(200.0f, 0.0f), true);
        ImGui::TextDisabled("Arquivos abertos");
        ImGui::Separator();
        const float closeButtonWidth = ImGui::GetFrameHeight();
        for (int i = 0; i < (int)m_Documents.size(); i++) {
            Document& document = *m_Documents[i];
            ImGui::PushID(i);
            std::string label = document.Path.filename().string() + (document.Dirty ? " *" : "");
            float selectableWidth = ImGui::GetContentRegionAvail().x - closeButtonWidth - ImGui::GetStyle().ItemSpacing.x;
            if (ImGui::Selectable(label.c_str(), i == m_Active, 0, ImVec2(selectableWidth, 0.0f)))
                m_Active = i;
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", document.Path.string().c_str());
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                    closeRequest = i; // clique do meio fecha, como em abas de navegador
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                closeRequest = i;
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::BeginDisabled(!AnyDirty());
        if (ImGui::Button("Salvar tudo", ImVec2(-1.0f, 0.0f)))
            SaveAll();
        ImGui::EndDisabled();
        ImGui::EndChild();

        if (closeRequest >= 0) {
            if (m_Documents[closeRequest]->Dirty) {
                m_PendingClose = closeRequest;
                m_OpenClosePopup = true;
            }
            else {
                CloseDocument(closeRequest);
            }
        }
        if (m_Documents.empty() || m_Active < 0 || m_Active >= (int)m_Documents.size()) {
            ImGui::End();
            return;
        }

        // --- arquivo ativo --------------------------------------------------
        ImGui::SameLine();
        ImGui::BeginChild("##CodeArea", ImVec2(0.0f, 0.0f), false);
        Document& active = *m_Documents[m_Active];

        ImGui::BeginDisabled(!active.Dirty);
        if (ImGui::Button("Salvar (Ctrl+S)"))
            SaveDocument(active);
        ImGui::EndDisabled();
        ImGui::SameLine();

        // Zoom: [-] 100% [+] no canto direito da barra; o caminho do arquivo ocupa o que sobra.
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            char percent[16];
            std::snprintf(percent, sizeof(percent), "%d%%", (int)std::lround(GetZoom() * 100.0f));
            const float minusWidth = ImGui::CalcTextSize("-").x + style.FramePadding.x * 2.0f;
            const float plusWidth = ImGui::CalcTextSize("+").x + style.FramePadding.x * 2.0f;
            const float percentWidth = ImGui::CalcTextSize("400%").x + style.FramePadding.x * 2.0f;
            const float zoomWidth = minusWidth + percentWidth + plusWidth + style.ItemSpacing.x * 2.0f;
            const float zoomStart = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - zoomWidth);

            // Caminho truncado pela largura disponivel (o tooltip mostra o completo).
            ImGui::BeginChild("##CodePath", ImVec2(std::max(0.0f, zoomStart - ImGui::GetCursorPosX() - style.ItemSpacing.x), ImGui::GetFrameHeight()), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", active.Path.string().c_str());
            ImGui::EndChild();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", active.Path.string().c_str());

            ImGui::SameLine(zoomStart);
            if (ImGui::Button("-##CodeZoomOut", ImVec2(minusWidth, 0.0f)))
                ZoomBy(-0.1f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Diminuir o texto (Ctrl + roda do mouse / Ctrl + -)");
            ImGui::SameLine();
            // Clicar na porcentagem volta para 100%.
            if (ImGui::Button((std::string(percent) + "##CodeZoomReset").c_str(), ImVec2(percentWidth, 0.0f)))
                ResetZoom();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Zoom do texto. Clique para voltar a 100%% (Ctrl + 0)");
            ImGui::SameLine();
            if (ImGui::Button("+##CodeZoomIn", ImVec2(plusWidth, 0.0f)))
                ZoomBy(0.1f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Aumentar o texto (Ctrl + roda do mouse / Ctrl + +)");
        }

        // Ctrl+S salva o arquivo ativo quando o foco esta neste painel (inclusive dentro do TextEditor).
        ImGuiIO& io = ImGui::GetIO();
        const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && panelFocused)
            SaveDocument(active);

        // Atalhos de zoom: Ctrl + '+' (a tecla '=' no teclado ABNT2/US tambem, sem precisar de Shift),
        // Ctrl + '-' e Ctrl + '0', com repeticao ao segurar. Teclado numerico incluido.
        if (io.KeyCtrl && !io.KeyAlt && panelFocused) {
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
                ZoomBy(0.1f);
            else if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
                ZoomBy(-0.1f);
            else if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false))
                ResetZoom();
        }

        ImGui::Separator();

        // Ctrl + roda do mouse sobre o codigo. Com Ctrl pressionado o ImGui nao rola a janela, entao
        // nao ha rolagem junto com o zoom. O fator vale a partir do proximo desenho do texto.
        if (io.KeyCtrl && io.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            ZoomBy(io.MouseWheel > 0.0f ? 0.1f : -0.1f);

        // O ID inclui o caminho: cada arquivo tem seu proprio estado de foco/rolagem no ImGui.
        // O TextEditor le o tamanho da fonte ao desenhar (GetFontSize/CalcTextSize), entao empilhar
        // uma fonte maior em volta de Render() escala texto, numeros de linha, cursor e selecao juntos.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool zoomed = GetZoom() != 1.0f;
        if (zoomed)
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * GetZoom());
        active.Editor.Render(("##ScriptEditorBuffer_" + active.Path.string()).c_str(), ImVec2(avail.x, avail.y));
        if (zoomed)
            ImGui::PopFont();
        ImGui::EndChild();

        ImGui::End();
    }

}
