#include "ContentBrowserPanel.h"
#include "../Core/EditorIcons.h"
#include "../Core/EditorTheme.h"
#include <Prism/Assets/AssetMeta.h>
#include <Prism/Core/Log.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace PrismEditor {

    void ContentBrowserPanel::ResetToProjectRoot() {
        auto project = Prism::Project::GetActive();
        if (!project) {
            m_CurrentDirectory.clear();
            m_CachedEntries.clear();
            return;
        }
        NavigateTo(project->GetProjectDirectory());
    }

    void ContentBrowserPanel::NavigateTo(const std::filesystem::path& directory) {
        m_CurrentDirectory = directory;
        m_SelectedPath.clear();
        RefreshEntries();
    }

    void ContentBrowserPanel::RefreshEntries() {
        m_CachedEntries.clear();

        std::error_code ec;
        if (m_CurrentDirectory.empty() || !std::filesystem::exists(m_CurrentDirectory, ec))
            return;

        for (const auto& dirEntry : std::filesystem::directory_iterator(m_CurrentDirectory, ec)) {
            if (ec) break;

            // Os .meta guardam a identidade dos assets (ver Assets/AssetMeta.h)
            // e nao sao coisa que o usuario deva ver nem manipular a mao -
            // ficam ocultos, como na Unity/Godot. Mover/renomear um asset
            // por fora do editor deve levar o .meta junto (ver AssetMeta.h).
            if (!dirEntry.is_directory() && Prism::AssetMetaFile::IsMetaFile(dirEntry.path()))
                continue;

            Entry entry;
            entry.Path = dirEntry.path();
            entry.Name = dirEntry.path().filename().string();
            entry.IsDirectory = dirEntry.is_directory();
            m_CachedEntries.push_back(std::move(entry));
        }

        // Pastas primeiro, depois arquivos - dentro de cada grupo, ordem
        // alfabetica (case-insensitive nao vale a pena aqui, nomes de
        // pasta/arquivo do projeto ja seguem uma convencao previsivel).
        std::sort(m_CachedEntries.begin(), m_CachedEntries.end(), [](const Entry& a, const Entry& b) {
            if (a.IsDirectory != b.IsDirectory)
                return a.IsDirectory > b.IsDirectory;
            return a.Name < b.Name;
            });
    }

    namespace fs = std::filesystem;

    namespace {
        // 'child' esta ESTRITAMENTE dentro de 'parent' (igualdade = false).
        bool IsInside(const fs::path& child, const fs::path& parent) {
            std::error_code ec;
            fs::path c = fs::weakly_canonical(child, ec);
            if (ec) return false;
            fs::path p = fs::weakly_canonical(parent, ec);
            if (ec) return false;
            fs::path rel = c.lexically_relative(p);
            if (rel.empty() || rel == ".") return false;
            return *rel.begin() != "..";
        }

        bool SamePath(const fs::path& a, const fs::path& b) {
            std::error_code ec;
            bool eq = fs::equivalent(a, b, ec);
            return !ec && eq;
        }

        void CopyName(char* buffer, size_t size, const std::string& text) {
            std::strncpy(buffer, text.c_str(), size - 1);
            buffer[size - 1] = '\0';
        }
    }

    bool ContentBrowserPanel::ValidName(const std::string& name, std::string& error) {
        if (name.empty()) { error = "O nome nao pode ficar vazio."; return false; }
        if (name == "." || name == "..") { error = "Nome invalido."; return false; }
        if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            error = "O nome nao pode conter \\ / : * ? \" < > |";
            return false;
        }
        if (name.back() == '.' || name.back() == ' ') { error = "O nome nao pode terminar com ponto ou espaco."; return false; }
        if (name.front() == ' ') { error = "O nome nao pode comecar com espaco."; return false; }
        if (Prism::AssetMetaFile::IsMetaFile(fs::path(name))) { error = "A extensao .meta e reservada pela engine."; return false; }
        return true;
    }

    // Tudo que fica direto na raiz do projeto (Assets/Maps/Scripts/..., .prismproj)
    // e estrutura da engine: nao se move, renomeia nem exclui por aqui.
    bool ContentBrowserPanel::IsProtected(const fs::path& path) const {
        auto project = Prism::Project::GetActive();
        if (!project) return true;
        const fs::path root = project->GetProjectDirectory();
        return SamePath(path, root) || SamePath(path.parent_path(), root);
    }

    bool ContentBrowserPanel::MoveEntry(const fs::path& from, const fs::path& to, std::string& error) {
        auto project = Prism::Project::GetActive();
        if (!project) { error = "Nenhum projeto ativo."; return false; }

        std::error_code ec;
        if (!fs::exists(from, ec)) { error = "O item nao existe mais."; return false; }
        if (IsProtected(from)) { error = "Este item faz parte da estrutura do projeto e nao pode ser movido ou renomeado."; return false; }

        const fs::path root = project->GetProjectDirectory();
        if (!IsInside(to, root)) { error = "O destino precisa ficar dentro do projeto."; return false; }

        const bool caseOnly = fs::exists(to, ec) && SamePath(from, to);
        if (!caseOnly && fs::exists(to, ec)) { error = "Ja existe um item com esse nome no destino."; return false; }
        if (!fs::exists(to.parent_path(), ec)) { error = "A pasta de destino nao existe."; return false; }

        const bool isDirectory = fs::is_directory(from, ec);
        if (isDirectory && (SamePath(from, to.parent_path()) || IsInside(to, from))) {
            error = "Uma pasta nao pode ser movida para dentro dela mesma.";
            return false;
        }

        const fs::path metaFrom = Prism::AssetMetaFile::MetaPathFor(from);
        const fs::path metaTo = Prism::AssetMetaFile::MetaPathFor(to);
        const bool hasMeta = !isDirectory && fs::exists(metaFrom, ec);

        if (caseOnly) {
            // Windows: so a caixa muda - dois passos via nome temporario.
            fs::path temp = from;
            temp += ".prism_rename_tmp";
            fs::rename(from, temp, ec);
            if (!ec) fs::rename(temp, to, ec);
        }
        else {
            if (hasMeta && fs::exists(metaTo, ec)) fs::remove(metaTo, ec); // .meta orfao de um arquivo que nao existe mais
            fs::rename(from, to, ec);
        }
        if (ec) { error = "Falha ao mover: " + ec.message(); return false; }

        if (hasMeta) {
            std::error_code metaEc;
            if (caseOnly) {
                fs::path tempMeta = metaFrom;
                tempMeta += ".prism_rename_tmp";
                fs::rename(metaFrom, tempMeta, metaEc);
                if (!metaEc) fs::rename(tempMeta, metaTo, metaEc);
            }
            else {
                fs::rename(metaFrom, metaTo, metaEc);
            }
            if (metaEc)
                PRISM_CORE_WARN("ContentBrowser: o arquivo foi movido, mas o .meta nao ('", metaFrom.generic_string(), "'): ", metaEc.message(), ". O AssetID pode ser regerado.");
        }

        project->GetAssetRegistry().Refresh();
        if (m_OnPathMoved)
            m_OnPathMoved(from, to);
        RefreshEntries();
        return true;
    }

    bool ContentBrowserPanel::DeleteEntry(const fs::path& path, std::string& error) {
        auto project = Prism::Project::GetActive();
        if (!project) { error = "Nenhum projeto ativo."; return false; }
        if (IsProtected(path)) { error = "Este item faz parte da estrutura do projeto e nao pode ser excluido."; return false; }

        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            fs::remove_all(path, ec);
        }
        else {
            fs::remove(path, ec);
            if (!ec) {
                std::error_code metaEc;
                fs::remove(Prism::AssetMetaFile::MetaPathFor(path), metaEc);
            }
        }
        if (ec) { error = "Falha ao excluir: " + ec.message(); return false; }

        project->GetAssetRegistry().Refresh();
        if (m_OnPathDeleted)
            m_OnPathDeleted(path);
        if (SamePath(path, m_SelectedPath) || m_SelectedPath.empty() || !fs::exists(m_SelectedPath, ec))
            m_SelectedPath.clear();
        RefreshEntries();
        return true;
    }

    bool ContentBrowserPanel::CreateFolder(const std::string& name, std::string& error) {
        if (!ValidName(name, error)) return false;
        fs::path target = m_CurrentDirectory / name;
        std::error_code ec;
        if (fs::exists(target, ec)) { error = "Ja existe um item com esse nome."; return false; }
        if (!fs::create_directory(target, ec) || ec) { error = "Falha ao criar a pasta: " + ec.message(); return false; }
        RefreshEntries();
        m_SelectedPath = target;
        return true;
    }

    void ContentBrowserPanel::BeginRename(const fs::path& path) {
        if (IsProtected(path)) {
            PRISM_CORE_WARN("ContentBrowser: itens da raiz do projeto nao podem ser renomeados.");
            return;
        }
        m_PopupTarget = path;
        CopyName(m_NameBuffer, sizeof(m_NameBuffer), path.filename().string());
        m_PopupError.clear();
        m_OpenRenamePopup = true;
    }

    void ContentBrowserPanel::BeginDelete(const fs::path& path) {
        if (IsProtected(path)) {
            PRISM_CORE_WARN("ContentBrowser: itens da raiz do projeto nao podem ser excluidos.");
            return;
        }
        m_PopupTarget = path;
        m_PopupError.clear();
        m_OpenDeletePopup = true;
    }

    void ContentBrowserPanel::RenderPopups() {
        const EditorTheme& theme = EditorTheme::Get();

        if (m_OpenRenamePopup) { ImGui::OpenPopup("Renomear##CB"); m_OpenRenamePopup = false; }
        if (m_OpenDeletePopup) { ImGui::OpenPopup("Excluir##CB"); m_OpenDeletePopup = false; }
        if (m_OpenNewFolderPopup) { ImGui::OpenPopup("Nova Pasta##CB"); m_OpenNewFolderPopup = false; }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

        // --- Renomear ---------------------------------------------------------
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Renomear##CB", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextDisabled("%s", m_PopupTarget.filename().string().c_str());
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(320.0f);
            bool enter = ImGui::InputText("##name", m_NameBuffer, sizeof(m_NameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (!m_PopupError.empty())
                ImGui::TextColored(theme.Stop, "%s", m_PopupError.c_str());

            const bool confirm = ImGui::Button("Renomear", ImVec2(120, 0)) || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancelar", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

            if (confirm) {
                std::string name = m_NameBuffer;
                std::string error;
                fs::path target = m_PopupTarget.parent_path() / name;
                if (name == m_PopupTarget.filename().string()) {
                    ImGui::CloseCurrentPopup();
                }
                else if (ValidName(name, error) && MoveEntry(m_PopupTarget, target, error)) {
                    m_SelectedPath = target;
                    ImGui::CloseCurrentPopup();
                }
                else {
                    m_PopupError = error;
                }
            }
            else if (cancel) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // --- Excluir ----------------------------------------------------------
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Excluir##CB", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            std::error_code ec;
            const bool isDir = fs::is_directory(m_PopupTarget, ec);
            ImGui::Text("Excluir \"%s\"?", m_PopupTarget.filename().string().c_str());
            if (isDir)
                ImGui::TextColored(theme.Warning, "A pasta e TODO o conteudo dela serao excluidos.");
            ImGui::TextDisabled("Isto nao pode ser desfeito. Referencias a este item vao ficar quebradas.");
            if (!m_PopupError.empty())
                ImGui::TextColored(theme.Stop, "%s", m_PopupError.c_str());

            ImGui::PushStyleColor(ImGuiCol_Button, theme.Stop);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.StopHot);
            const bool confirm = ImGui::Button("Excluir", ImVec2(120, 0));
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancelar", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

            if (confirm) {
                std::string error;
                if (DeleteEntry(m_PopupTarget, error)) ImGui::CloseCurrentPopup();
                else m_PopupError = error;
            }
            else if (cancel) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // --- Nova pasta -------------------------------------------------------
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Nova Pasta##CB", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(320.0f);
            bool enter = ImGui::InputText("##folder", m_NameBuffer, sizeof(m_NameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (!m_PopupError.empty())
                ImGui::TextColored(theme.Stop, "%s", m_PopupError.c_str());

            const bool confirm = ImGui::Button("Criar", ImVec2(120, 0)) || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancelar", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

            if (confirm) {
                std::string error;
                if (CreateFolder(m_NameBuffer, error)) ImGui::CloseCurrentPopup();
                else m_PopupError = error;
            }
            else if (cancel) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserPanel::OnImGuiRender() {
        ImGui::Begin("Conteudo do Projeto");

        auto project = Prism::Project::GetActive();
        if (!project) {
            ImGui::TextDisabled("Nenhum projeto ativo.");
            ImGui::End();
            return;
        }

        // Primeira vez que este painel roda com um projeto ativo (ou depois
        // de ResetToProjectRoot nao ter sido chamado ainda por algum motivo)
        // - navega para a raiz automaticamente em vez de mostrar vazio.
        if (m_CurrentDirectory.empty())
            NavigateTo(project->GetProjectDirectory());

        RenderToolbar();
        ImGui::Separator();
        RenderGrid();
        RenderPopups();

        ImGui::End();
    }

    // Aceita qualquer payload de arquivo do Content Browser e devolve o caminho.
    static bool AcceptPathPayload(std::string& outPath) {
        static const char* kTypes[] = {
            "CONTENT_BROWSER_MOVE_PATH", "CONTENT_BROWSER_IMAGE_PATH", "CONTENT_BROWSER_PREFAB_PATH",
            "CONTENT_BROWSER_MATERIAL_PATH", "CONTENT_BROWSER_MODEL_PATH", "CONTENT_BROWSER_AUDIO_PATH" };
        for (const char* type : kTypes) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type)) {
                outPath = (const char*)payload->Data;
                return true;
            }
        }
        return false;
    }

    void ContentBrowserPanel::RenderToolbar() {
        auto project = Prism::Project::GetActive();
        const EditorTheme& theme = EditorTheme::Get();

        // Comparacao via caminhos canonicalizados (resolve diferenças de
        // barra/trailing slash entre o que veio de directory_iterator e o
        // que Project::GetProjectDirectory() retorna).
        const bool atRoot = SamePath(m_CurrentDirectory, project->GetProjectDirectory());

        fs::path navigateTo;
        fs::path moveFrom, moveTo;

        ImGui::BeginDisabled(atRoot);
        if (Prism::Project::GetActive() && IconButton("##back", ToolIcon::Back, "Voltar (solte um arquivo aqui para mover para a pasta acima)"))
            navigateTo = m_CurrentDirectory.parent_path();
        ImGui::EndDisabled();
        if (!atRoot && ImGui::BeginDragDropTarget()) {
            std::string dropped;
            if (AcceptPathPayload(dropped)) {
                moveFrom = dropped;
                moveTo = m_CurrentDirectory.parent_path() / fs::path(dropped).filename();
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (IconButton("##refresh", ToolIcon::Refresh, "Atualizar")) {
            // Reconcilia a identidade dos assets com o disco ANTES de reler a
            // pasta: arquivos novos ganham .meta, movidos mantem o ID (ver
            // Assets/AssetRegistry.h).
            project->GetAssetRegistry().Refresh();
            RefreshEntries();
        }

        ImGui::SameLine();
        if (IconButton("##newfolder", ToolIcon::NewFolder, "Nova pasta")) {
            std::string name = "Nova Pasta";
            std::error_code ec;
            for (int i = 2; fs::exists(m_CurrentDirectory / name, ec); ++i)
                name = "Nova Pasta " + std::to_string(i);
            CopyName(m_NameBuffer, sizeof(m_NameBuffer), name);
            m_PopupError.clear();
            m_OpenNewFolderPopup = true;
        }

        // Slider de tamanho dos icones, alinhado a direita.
        const float sliderWidth = 90.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - sliderWidth));
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##cellsize", &m_CellSize, 56.0f, 128.0f, "");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tamanho dos icones");

        // Breadcrumb clicavel: Projeto / Assets / Models. Cada trecho navega.
        fs::path relative = fs::relative(m_CurrentDirectory, project->GetProjectDirectory());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
        if (ImGui::SmallButton(project->GetConfig().Name.c_str()))
            navigateTo = project->GetProjectDirectory();
        if (relative != ".") {
            fs::path accumulated = project->GetProjectDirectory();
            int index = 0;
            for (const auto& part : relative) {
                accumulated /= part;
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::TextColored(theme.TextDim, "/");
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::PushID(index++);
                if (ImGui::SmallButton(part.string().c_str()))
                    navigateTo = accumulated;
                ImGui::PopID();
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        if (!moveFrom.empty()) {
            std::string error;
            if (!MoveEntry(moveFrom, moveTo, error))
                PRISM_CORE_WARN("ContentBrowser: ", error);
        }
        else if (!navigateTo.empty()) {
            NavigateTo(navigateTo);
        }
    }

    void ContentBrowserPanel::RenderGrid() {
        const EditorTheme& theme = EditorTheme::Get();
        ImGuiIO& io = ImGui::GetIO();

        // Atalhos: F2 renomeia e Del exclui o item selecionado (so com o painel
        // em foco e sem campo de texto ativo).
        if (!m_SelectedPath.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) BeginRename(m_SelectedPath);
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) BeginDelete(m_SelectedPath);
        }

        // Grid de celulas com quebra de linha automatica (colunas calculadas
        // pela largura disponivel).
        const float cellSize = m_CellSize;
        const float cellPadding = 8.0f;
        const float labelHeight = ImGui::GetTextLineHeight() * 2.0f + 2.0f;
        const float panelWidth = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, (int)(panelWidth / (cellSize + cellPadding)));

        // Acoes adiadas para FORA do loop (mexem em m_CachedEntries; fazer isso
        // dentro do for causava use-after-free / heap corruption).
        fs::path navigateToPath;
        fs::path openMapPath;
        fs::path openTextPath;
        fs::path renamePath;
        fs::path deletePath;
        fs::path moveFrom, moveTo;

        if (m_CachedEntries.empty()) {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::TextDisabled("Esta pasta esta vazia.");
        }
        else {
            ImGui::Columns(columns, nullptr, false);

            for (const auto& entry : m_CachedEntries) {
                ImGui::PushID(entry.Path.string().c_str());

                const bool isSelected = SamePath(entry.Path, m_SelectedPath) && !m_SelectedPath.empty();
                const std::string extRaw = entry.Path.extension().string();
                std::string ext = extRaw;
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });

                const bool isMapFile = !entry.IsDirectory && ext == ".prismmap";
                const bool isProjectFile = !entry.IsDirectory && ext == ".prismproj";
                const bool isPrefabFile = !entry.IsDirectory && ext == ".prismprefab";
                const bool isMaterialFile = !entry.IsDirectory && ext == ".prismmat";
                // Mesma classificacao que o AssetRegistry usa (AssetType, ver AssetMeta.h).
                const bool isModelFile = !entry.IsDirectory && Prism::AssetTypeFromExtension(entry.Path) == Prism::AssetType::Model;
                const bool isAudioFile = !entry.IsDirectory && Prism::AssetTypeFromExtension(entry.Path) == Prism::AssetType::Audio;
                const bool isScriptFile = !entry.IsDirectory && (ext == ".lua" || ext == ".cpp" || ext == ".h" || ext == ".hpp");
                // Extensoes que stb_image decodifica (ver Texture.cpp).
                const bool isImage = !entry.IsDirectory && (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr" || ext == ".psd" || ext == ".gif");
                const bool isTyped = isImage || isPrefabFile || isMaterialFile || isModelFile || isAudioFile;
                const bool isProtectedEntry = IsProtected(entry.Path);

                // Miniatura de imagem, pelo mesmo cache do Renderer usado pelo
                // painel de Material (isSRGB=true: so preview, a diferenca e cosmetica).
                Prism::Texture2D* thumbnail = isImage ? Prism::Renderer::GetOrLoadTexture(entry.Path.string(), /*isSRGB*/ true) : nullptr;
                const bool hasThumbnail = thumbnail && thumbnail->IsValid();

                FileIcon icon = entry.IsDirectory ? FileIcon::Folder
                    : isMapFile ? FileIcon::Map
                    : isProjectFile ? FileIcon::Project
                    : isPrefabFile ? FileIcon::Prefab
                    : isMaterialFile ? FileIcon::Material
                    : isModelFile ? FileIcon::Model
                    : isAudioFile ? FileIcon::Audio
                    : isImage ? FileIcon::Image
                    : isScriptFile ? FileIcon::Script
                    : FileIcon::File;
                const ImVec4 iconColor = entry.IsDirectory ? ImVec4(0.45f, 0.65f, 0.90f, 1.0f)
                    : isMapFile ? ImVec4(0.90f, 0.58f, 0.22f, 1.0f)
                    : isProjectFile ? ImVec4(0.65f, 0.45f, 0.85f, 1.0f)
                    : isPrefabFile ? ImVec4(0.35f, 0.80f, 0.55f, 1.0f)
                    : isMaterialFile ? ImVec4(0.85f, 0.35f, 0.65f, 1.0f)
                    : isModelFile ? ImVec4(0.35f, 0.70f, 0.85f, 1.0f)
                    : isAudioFile ? ImVec4(0.90f, 0.75f, 0.25f, 1.0f)
                    : isImage ? ImVec4(0.60f, 0.78f, 0.45f, 1.0f)
                    : isScriptFile ? ImVec4(0.55f, 0.65f, 0.95f, 1.0f)
                    : ImVec4(0.62f, 0.64f, 0.70f, 1.0f);

                // --- Celula: um unico item invisivel (alvo de clique/drag/drop) ----
                const ImVec2 cellMin = ImGui::GetCursorScreenPos();
                const ImVec2 cellMax(cellMin.x + cellSize, cellMin.y + cellSize + labelHeight);
                const bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cellSize, cellSize + labelHeight));
                const bool hovered = ImGui::IsItemHovered();

                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (isSelected)
                    dl->AddRectFilled(cellMin, cellMax, ToU32(theme.Accent, 0.28f), theme.Rounding);
                else if (hovered)
                    dl->AddRectFilled(cellMin, cellMax, ToU32(theme.SurfaceHot, 0.7f), theme.Rounding);
                if (isSelected)
                    dl->AddRect(cellMin, cellMax, ToU32(theme.Accent, 0.9f), theme.Rounding, 0, 1.0f);

                const float inset = cellSize * 0.12f;
                const ImVec2 iconMin(cellMin.x + inset, cellMin.y + inset * 0.8f);
                const ImVec2 iconMax(cellMin.x + cellSize - inset, cellMin.y + cellSize - inset * 0.6f);
                if (hasThumbnail) {
                    // V invertido: Texture2D guarda a imagem de cabeca para baixo (stbi flip, ver Texture.cpp)
                    dl->AddImageRounded((ImTextureID)(uintptr_t)thumbnail->GetRendererID(), iconMin, iconMax, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 3.0f);
                }
                else {
                    DrawFileIcon(dl, icon, iconMin, iconMax, ToU32(iconColor));
                }

                // Nome: centralizado se cabe numa linha, senao quebra em ate 2 linhas (recortado).
                {
                    const std::string& name = entry.Name;
                    const float wrapWidth = cellSize - 6.0f;
                    ImFont* font = ImGui::GetFont();
                    const float fontSize = ImGui::GetFontSize();
                    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name.c_str());
                    ImVec2 pos(cellMin.x + 3.0f, cellMin.y + cellSize);
                    if (textSize.x < wrapWidth)
                        pos.x = cellMin.x + (cellSize - textSize.x) * 0.5f;
                    dl->PushClipRect(ImVec2(cellMin.x + 1.0f, cellMin.y + cellSize), ImVec2(cellMax.x - 1.0f, cellMax.y), true);
                    dl->AddText(font, fontSize, pos, ToU32(theme.Text), name.c_str(), nullptr, wrapWidth);
                    dl->PopClipRect();
                }

                if (clicked || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    m_SelectedPath = entry.Path;

                // --- Menu de contexto do item ---------------------------------
                if (ImGui::BeginPopupContextItem("##itemmenu")) {
                    if (entry.IsDirectory && ImGui::MenuItem("Abrir"))
                        navigateToPath = entry.Path;
                    if (isMapFile && m_OnMapDoubleClicked && ImGui::MenuItem("Abrir mapa"))
                        openMapPath = entry.Path;
                    if (!entry.IsDirectory && !isMapFile && !isTyped && !isProjectFile && m_OnTextFileDoubleClicked && ImGui::MenuItem("Abrir no editor de codigo"))
                        openTextPath = entry.Path;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Renomear", "F2", false, !isProtectedEntry))
                        renamePath = entry.Path;
                    if (ImGui::MenuItem("Excluir", "Del", false, !isProtectedEntry))
                        deletePath = entry.Path;
                    ImGui::EndPopup();
                }

                // --- Fontes de drag & drop -------------------------------------
                // Cada tipo carrega o caminho ABSOLUTO (quem recebe resolve o AssetID
                // ou relativiza): IMAGE -> slots de textura do Material; PREFAB ->
                // Hierarquia/Viewport; MATERIAL -> painel Material; MODEL -> Mesh
                // Renderer; AUDIO -> Audio Source. SourceAllowNullID por seguranca.
                // Pastas, mapas, scripts e arquivos genericos usam o payload de
                // MOVER; os tipados tambem podem ser soltos numa pasta (ver abaixo).
                const char* payloadType = isImage ? "CONTENT_BROWSER_IMAGE_PATH"
                    : isPrefabFile ? "CONTENT_BROWSER_PREFAB_PATH"
                    : isMaterialFile ? "CONTENT_BROWSER_MATERIAL_PATH"
                    : isModelFile ? "CONTENT_BROWSER_MODEL_PATH"
                    : isAudioFile ? "CONTENT_BROWSER_AUDIO_PATH"
                    : (!isProtectedEntry && !isProjectFile) ? "CONTENT_BROWSER_MOVE_PATH" : nullptr;
                if (payloadType && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    std::string pathString = entry.Path.string();
                    ImGui::SetDragDropPayload(payloadType, pathString.c_str(), pathString.size() + 1);
                    if (hasThumbnail)
                        ImGui::Image((ImTextureID)(uintptr_t)thumbnail->GetRendererID(), ImVec2(48, 48), ImVec2(0, 1), ImVec2(1, 0));
                    ImGui::Text("%s", entry.Name.c_str());
                    ImGui::EndDragDropSource();
                }

                // Pasta como destino: solta o item dentro dela (move).
                if (entry.IsDirectory && ImGui::BeginDragDropTarget()) {
                    std::string dropped;
                    if (AcceptPathPayload(dropped)) {
                        moveFrom = dropped;
                        moveTo = entry.Path / fs::path(dropped).filename();
                    }
                    ImGui::EndDragDropTarget();
                }

                // Duplo-clique: so REGISTRA a intencao - a execucao e depois do
                // loop (chamar NavigateTo()/callbacks aqui dentro era a causa do
                // HEAP CORRUPTION, use-after-free no m_CachedEntries).
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (entry.IsDirectory) {
                        navigateToPath = entry.Path;
                    }
                    else if (isMapFile && m_OnMapDoubleClicked) {
                        openMapPath = entry.Path;
                    }
                    else if (!isMapFile && !isProjectFile && !isTyped && m_OnTextFileDoubleClicked) {
                        openTextPath = entry.Path;
                    }
                }
                if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    ImGui::SetTooltip("%s", entry.Path.string().c_str());

                ImGui::PopID();
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
        }

        // Menu de contexto do fundo (area vazia do painel).
        if (ImGui::BeginPopupContextWindow("##blankmenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Nova pasta")) {
                std::string name = "Nova Pasta";
                std::error_code ec;
                for (int i = 2; fs::exists(m_CurrentDirectory / name, ec); ++i)
                    name = "Nova Pasta " + std::to_string(i);
                CopyName(m_NameBuffer, sizeof(m_NameBuffer), name);
                m_PopupError.clear();
                m_OpenNewFolderPopup = true;
            }
            if (ImGui::MenuItem("Atualizar")) {
                if (auto project = Prism::Project::GetActive())
                    project->GetAssetRegistry().Refresh();
                RefreshEntries();
            }
            ImGui::EndPopup();
        }

        // Executa a acao adiada (se houver) FORA do loop - o iterador ja foi
        // destruido, entao mexer em m_CachedEntries e seguro. Prioridade: mover,
        // navegar, abrir mapa, abrir texto, renomear, excluir.
        if (!moveFrom.empty()) {
            std::string error;
            if (!MoveEntry(moveFrom, moveTo, error))
                PRISM_CORE_WARN("ContentBrowser: ", error);
        }
        else if (!navigateToPath.empty()) {
            NavigateTo(navigateToPath);
        }
        else if (!openMapPath.empty() && m_OnMapDoubleClicked) {
            m_OnMapDoubleClicked(openMapPath);
        }
        else if (!openTextPath.empty() && m_OnTextFileDoubleClicked) {
            m_OnTextFileDoubleClicked(openTextPath);
        }
        else if (!renamePath.empty()) {
            BeginRename(renamePath);
        }
        else if (!deletePath.empty()) {
            BeginDelete(deletePath);
        }
    }

}
