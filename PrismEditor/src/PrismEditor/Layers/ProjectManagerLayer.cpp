#include "ProjectManagerLayer.h"
#include "../Core/EditorTheme.h"
#include "EditorLayer.h"
#include "../Core/FileDialog.h"

#include <PrismVersion.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>

namespace PrismEditor {

    // Onde guardamos a lista de projetos recentes. Fica ao lado do executavel
    // por simplicidade nesta fase - candidato natural a virar %APPDATA% depois.
    static const char* kRecentProjectsFile = "recent_projects.txt";
    static constexpr size_t kMaxRecentProjects = 20;

    static std::int64_t NowSeconds() {
        return (std::int64_t)std::time(nullptr);
    }

    // "agora ha pouco", "ha 5 min", "ha 3 h", "ha 2 dia(s)", "ha 1 mes(es)".
    static std::string RelativeTimeText(std::int64_t then) {
        if (then <= 0)
            return {};
        std::int64_t diff = NowSeconds() - then;
        if (diff < 0)
            diff = 0;
        char buffer[48];
        if (diff < 60)
            return "agora ha pouco";
        if (diff < 3600)
            std::snprintf(buffer, sizeof(buffer), "ha %lld min", (long long)(diff / 60));
        else if (diff < 86400)
            std::snprintf(buffer, sizeof(buffer), "ha %lld h", (long long)(diff / 3600));
        else if (diff < 86400 * 30)
            std::snprintf(buffer, sizeof(buffer), "ha %lld dia(s)", (long long)(diff / 86400));
        else
            std::snprintf(buffer, sizeof(buffer), "ha %lld mes(es)", (long long)(diff / (86400 * 30)));
        return buffer;
    }

    static std::string Trim(const std::string& text) {
        size_t begin = 0;
        size_t end = text.size();
        while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
            begin++;
        while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
            end--;
        return text.substr(begin, end - begin);
    }

    // snprintf em vez de strncpy: strncpy NAO garante terminador nulo quando
    // o texto e maior que o buffer, e o ImGui::InputText le o buffer com
    // strlen (corrupcao de memoria sutil, ja vista neste projeto).
    static void CopyToBuffer(char* buffer, size_t size, const std::string& text) {
        std::snprintf(buffer, size, "%s", text.c_str());
    }

    // Pasta padrao dos projetos: ~/PrismProjects (USERPROFILE no Windows).
    static std::filesystem::path DefaultProjectsDirectory() {
        const char* home = std::getenv("USERPROFILE");
        if (!home || !*home)
            home = std::getenv("HOME");
        if (home && *home)
            return std::filesystem::path(std::string(home)) / "PrismProjects";
        std::error_code ec;
        return std::filesystem::current_path(ec) / "PrismProjects";
    }

    ProjectManagerLayer::ProjectManagerLayer() : Layer("ProjectManagerLayer") {}

    void ProjectManagerLayer::OnUpdate(float deltaTime) {
        (void)deltaTime;
    }

    void ProjectManagerLayer::OnAttach() {
        EditorTheme::Init(); // fonte + estilo do editor (idempotente) - antes do primeiro NewFrame
        LoadRecentProjects();

        if (m_LastProjectsDirectory.empty())
            m_LastProjectsDirectory = DefaultProjectsDirectory();
        CopyToBuffer(m_ParentBuffer, sizeof(m_ParentBuffer), FileDialog::ToUtf8(m_LastProjectsDirectory));

        SuggestProjectName();
        m_ValidationDirty = true;
    }

    // "NovoProjeto", "NovoProjeto2", ... - o primeiro que ainda nao existe no local.
    void ProjectManagerLayer::SuggestProjectName() {
        std::filesystem::path parent = FileDialog::FromUtf8(m_ParentBuffer);
        std::string name = "NovoProjeto";
        std::error_code ec;
        for (int i = 2; i < 1000 && std::filesystem::exists(parent / name, ec); i++)
            name = "NovoProjeto" + std::to_string(i);
        CopyToBuffer(m_NameBuffer, sizeof(m_NameBuffer), name);
    }

    void ProjectManagerLayer::RefreshValidation() {
        std::string name = Trim(m_NameBuffer);
        std::string parent = m_ParentBuffer;
        double now = ImGui::GetTime();
        if (!m_ValidationDirty && name == m_ValidatedName && parent == m_ValidatedParent && now - m_ValidatedAt < 1.0)
            return;

        m_ValidationDirty = false;
        m_ValidatedName = name;
        m_ValidatedParent = parent;
        m_ValidatedAt = now;
        m_ValidationError = Prism::Project::ValidateNewProject(FileDialog::FromUtf8(parent), name);
    }

    // ------------------------------------------------------------------
    // Interface
    // ------------------------------------------------------------------

    void ProjectManagerLayer::OnImGuiRender() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
        ImGui::Begin("ProjectManager", nullptr, flags);

        ImGui::Dummy(ImVec2(0, 12));
        ImGui::SetWindowFontScale(1.6f);
        ImGui::Text("  Prism Engine");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("  Gerenciador de Projetos");
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));

        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float leftWidth = std::floor(available.x * 0.42f);
        const float rightWidth = available.x - leftWidth - spacing;
        const float panelHeight = (std::max)(available.y - footerHeight, 100.0f);

        RenderRecentProjects(leftWidth, panelHeight);
        ImGui::SameLine();
        RenderNewProject(rightWidth, panelHeight);

        RenderFooter();

        ImGui::End();
    }

    void ProjectManagerLayer::RenderRecentProjects(float width, float height) {
        ImGui::BeginChild("##RecentProjects", ImVec2(width, height), true);

        ImGui::TextUnformatted("Projetos recentes");
        const char* openLabel = "Abrir projeto...";
        const float openWidth = ImGui::CalcTextSize(openLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;

        // Resultado de qualquer acao que abre um projeto: aplicado DEPOIS do
        // loop abaixo. OpenProject() -> AddToRecentProjects() faz erase+insert
        // em m_RecentProjects, o que invalidaria o 'for' no meio da iteracao.
        std::filesystem::path pendingOpen;
        int pendingRemove = -1;

        ImGui::SameLine(ImGui::GetWindowWidth() - openWidth - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(openLabel)) {
            std::filesystem::path startIn = m_RecentProjects.empty() ? m_LastProjectsDirectory : m_RecentProjects.front().Path.parent_path();
            pendingOpen = FileDialog::OpenFile("Projeto Prism", "prismproj", startIn);
        }
        ImGui::Separator();

        const float footerForError = m_OpenError.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        ImGui::BeginChild("##RecentProjectsList", ImVec2(0, -footerForError), false);

        if (m_RecentProjects.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Nenhum projeto recente ainda.");
            ImGui::TextDisabled("Crie um projeto ao lado ou use \"Abrir projeto...\".");
        }

        const float itemHeight = 54.0f;
        for (size_t i = 0; i < m_RecentProjects.size(); i++) {
            const RecentProject& recent = m_RecentProjects[i];
            ImGui::PushID((int)i);

            const ImVec2 position = ImGui::GetCursorScreenPos();
            const float itemWidth = ImGui::GetContentRegionAvail().x;
            bool clicked = ImGui::Selectable("##item", false, 0, ImVec2(itemWidth, itemHeight));

            // Texto da direita: versao da engine e quando foi aberto.
            std::string meta;
            if (!recent.EngineVersion.empty())
                meta = "v" + recent.EngineVersion;
            std::string when = RelativeTimeText(recent.LastOpened);
            if (!when.empty())
                meta += (meta.empty() ? "" : " - ") + when;
            const bool versionDiffers = !recent.EngineVersion.empty() && recent.EngineVersion != Prism::Version::Number;

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float metaWidth = ImGui::CalcTextSize(meta.c_str()).x;
            const ImU32 textColor = ImGui::GetColorU32(recent.Exists ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

            drawList->PushClipRect(position, ImVec2(position.x + itemWidth - metaWidth - 20.0f, position.y + itemHeight), true);
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.15f, ImVec2(position.x + 10.0f, position.y + 6.0f), textColor, recent.Name.c_str());
            drawList->PopClipRect();

            std::string pathText = recent.Exists ? FileDialog::ToUtf8(recent.Path) : "Arquivo nao encontrado: " + FileDialog::ToUtf8(recent.Path);
            drawList->PushClipRect(position, ImVec2(position.x + itemWidth - 10.0f, position.y + itemHeight), true);
            drawList->AddText(ImVec2(position.x + 10.0f, position.y + itemHeight - 22.0f),
                recent.Exists ? dimColor : ImGui::GetColorU32(ImVec4(0.95f, 0.45f, 0.40f, 1.0f)), pathText.c_str());
            drawList->PopClipRect();

            if (!meta.empty()) {
                drawList->AddText(ImVec2(position.x + itemWidth - metaWidth - 10.0f, position.y + 8.0f),
                    versionDiffers ? ImGui::GetColorU32(ImVec4(0.95f, 0.75f, 0.20f, 1.0f)) : dimColor, meta.c_str());
            }
            if (versionDiffers && ImGui::IsItemHovered())
                ImGui::SetTooltip("Este projeto foi criado com a engine %s; a atual e %s.", recent.EngineVersion.c_str(), Prism::Version::Number);

            if (ImGui::BeginPopupContextItem("##recentContext")) {
                if (ImGui::MenuItem("Abrir", nullptr, false, recent.Exists))
                    pendingOpen = recent.Path;
                if (ImGui::MenuItem("Remover da lista"))
                    pendingRemove = (int)i;
                ImGui::EndPopup();
            }

            if (clicked && recent.Exists)
                pendingOpen = recent.Path;

            ImGui::PopID();
        }
        ImGui::EndChild();

        if (!m_OpenError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
            ImGui::TextWrapped("%s", m_OpenError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();

        if (pendingRemove >= 0 && pendingRemove < (int)m_RecentProjects.size()) {
            m_RecentProjects.erase(m_RecentProjects.begin() + pendingRemove);
            SaveRecentProjects();
        }
        if (!pendingOpen.empty())
            OpenProject(pendingOpen);
    }

    void ProjectManagerLayer::RenderNewProject(float width, float height) {
        ImGui::BeginChild("##NewProject", ImVec2(width, height), true);

        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextUnformatted("Novo projeto");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("Uma pasta com assets, mapas, scripts C++ e um arquivo .prismproj.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Nome do projeto");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectName", m_NameBuffer, sizeof(m_NameBuffer));
        ImGui::Spacing();

        ImGui::TextUnformatted("Local");
        const float browseWidth = ImGui::CalcTextSize("Procurar...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(-(browseWidth + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputText("##ProjectParent", m_ParentBuffer, sizeof(m_ParentBuffer));
        ImGui::SameLine();
        if (ImGui::Button("Procurar...")) {
            std::filesystem::path picked = FileDialog::PickFolder(FileDialog::FromUtf8(m_ParentBuffer));
            if (!picked.empty())
                CopyToBuffer(m_ParentBuffer, sizeof(m_ParentBuffer), FileDialog::ToUtf8(picked));
        }

        RefreshValidation();

        ImGui::Spacing();
        const std::string name = Trim(m_NameBuffer);
        if (!name.empty() && m_ParentBuffer[0] != '\0') {
            std::filesystem::path target = FileDialog::FromUtf8(m_ParentBuffer) / FileDialog::FromUtf8(name);
            ImGui::TextDisabled("O projeto sera criado em:");
            ImGui::TextWrapped("%s", FileDialog::ToUtf8(target).c_str());
        }
        if (!m_ValidationError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
            ImGui::TextWrapped("%s", m_ValidationError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Scripts");
        ImGui::TextWrapped("Os scripts do jogo sao em C++: o projeto ja nasce com a pasta Source/ "
            "(CMake e um exemplo pronto). Scripts Lua continuam disponiveis em Scripts/.");
        if (ImGui::Checkbox("Compilar os scripts C++ ao abrir o projeto", &m_BuildOnOpen))
            SaveRecentProjects(); // a preferencia vale tambem para projetos existentes
        ImGui::SetItemTooltip("Roda o CMake em segundo plano assim que o editor abre.\nRequer CMake e um compilador C++20 no PATH.");

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Estrutura criada")) {
            ImGui::TextDisabled("Assets/   modelos, texturas, audio, materiais, prefabs");
            ImGui::TextDisabled("Maps/     mapas (.prismmap)");
            ImGui::TextDisabled("Scripts/  scripts Lua");
            ImGui::TextDisabled("Source/   scripts C++ (CMakeLists.txt, exemplo Spinner)");
            ImGui::TextDisabled("Binaries/ modulo C++ compilado (fora do Git)");
            ImGui::TextDisabled("Cache/    dados derivados (fora do Git)");
        }

        // Botao ancorado no fim do painel (sem sobrepor o conteudo se a janela for pequena).
        const float buttonHeight = 40.0f;
        const float messageHeight = m_CreateError.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        const float bottomY = ImGui::GetWindowHeight() - buttonHeight - messageHeight - ImGui::GetStyle().WindowPadding.y;
        ImGui::SetCursorPosY((std::max)(ImGui::GetCursorPosY(), bottomY));

        if (!m_CreateError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
            ImGui::TextWrapped("%s", m_CreateError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::BeginDisabled(!m_ValidationError.empty());
        const bool create = ImGui::Button("Criar projeto", ImVec2(-1, buttonHeight));
        ImGui::EndDisabled();

        ImGui::EndChild();

        if (create)
            CreateAndOpenProject();
    }

    void ProjectManagerLayer::RenderFooter() {
        ImGui::Separator();
        ImGui::TextDisabled("%s", Prism::Version::Full().c_str());
    }

    // ------------------------------------------------------------------
    // Criar / abrir
    // ------------------------------------------------------------------

    void ProjectManagerLayer::CreateAndOpenProject() {
        m_CreateError.clear();

        std::filesystem::path parent = FileDialog::FromUtf8(m_ParentBuffer);
        Prism::ProjectCreateOptions options;
        options.Name = Trim(m_NameBuffer);
        options.ParentDirectory = parent;
        options.EngineVersion = Prism::Version::Number;

        std::string error;
        Prism::Ref<Prism::Project> project = Prism::Project::New(options, error);
        if (!project) {
            m_CreateError = error;
            PRISM_ERROR("Falha ao criar o projeto: ", error);
            m_ValidationDirty = true;
            return;
        }

        // C++ e a linguagem principal: o projeto de scripts ja nasce pronto.
        // Se falhar (ex: PrismSDK.cmake ausente), o projeto continua valido e
        // o botao "Compilar" da barra superior refaz o passo (gera se faltar).
        std::string message;
        if (Prism::NativeScripting::GenerateProjectFiles(*project, message))
            PRISM_INFO(message);
        else
            PRISM_ERROR("Projeto criado, mas o projeto de scripts C++ nao foi gerado: ", message);

        m_LastProjectsDirectory = parent;
        AddToRecentProjects(project->GetProjectDirectory() / (options.Name + ".prismproj"));
        SaveRecentProjects();

        EnterEditor();
    }

    void ProjectManagerLayer::OpenProject(const std::filesystem::path& prismprojFile) {
        m_OpenError.clear();

        std::error_code ec;
        if (!std::filesystem::exists(prismprojFile, ec)) {
            m_OpenError = "O arquivo nao existe mais: " + FileDialog::ToUtf8(prismprojFile);
            PRISM_ERROR(m_OpenError);
            return;
        }

        Prism::Ref<Prism::Project> project = Prism::Project::Load(prismprojFile);
        if (!project) {
            m_OpenError = "Nao foi possivel abrir o projeto: " + FileDialog::ToUtf8(prismprojFile);
            PRISM_ERROR(m_OpenError);
            return;
        }

        // Projeto de antes do foco em C++ (so Lua): adiciona Source/ para que
        // "Compilar" funcione. So cria arquivos novos na pasta Source/ - nunca
        // mexe no que ja existe (ver GenerateProjectFiles).
        if (!std::filesystem::exists(Prism::NativeScripting::GetSourceDirectory(*project) / "CMakeLists.txt", ec)) {
            PRISM_INFO("Projeto sem a pasta Source/: gerando o projeto de scripts C++.");
            std::string message;
            if (Prism::NativeScripting::GenerateProjectFiles(*project, message))
                PRISM_INFO(message);
            else
                PRISM_WARN("Nao foi possivel gerar o projeto de scripts C++: ", message);
        }

        m_LastProjectsDirectory = prismprojFile.parent_path().parent_path();
        AddToRecentProjects(prismprojFile);
        SaveRecentProjects();

        EnterEditor();
    }

    void ProjectManagerLayer::EnterEditor() {
        // Troca de tela: PushLayer(EditorLayer) e PopLayer(this) sao
        // enfileirados JUNTOS, no mesmo frame, e processados no mesmo
        // ProcessPendingLayerOps() do proximo frame - sem nenhum frame
        // intermediario em que as duas Layers desenham UI ao mesmo tempo (isso
        // ja causou um crash de ImGui::InputText). Nao use 'this' depois daqui.
        Prism::Application& app = Prism::Application::Get();
        app.PushLayer(new EditorLayer(m_BuildOnOpen));
        app.PopLayer(this);
    }

    // ------------------------------------------------------------------
    // Projetos recentes
    // ------------------------------------------------------------------

    void ProjectManagerLayer::ResolveRecent(RecentProject& recent) {
        std::error_code ec;
        recent.Exists = std::filesystem::exists(recent.Path, ec) && !ec;

        Prism::ProjectSummary summary;
        if (recent.Exists && Prism::Project::ReadSummary(recent.Path, summary)) {
            recent.Name = summary.Name;
            recent.EngineVersion = summary.EngineVersion;
        }
        else {
            recent.Name = FileDialog::ToUtf8(recent.Path.stem());
            recent.EngineVersion.clear();
        }
    }

    void ProjectManagerLayer::LoadRecentProjects() {
        m_RecentProjects.clear();
        std::ifstream in(kRecentProjectsFile);
        if (!in.is_open())
            return;

        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            if (line[0] == '@') {
                size_t eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string key = line.substr(1, eq - 1);
                std::string value = line.substr(eq + 1);
                if (key == "dir" && !value.empty())
                    m_LastProjectsDirectory = FileDialog::FromUtf8(value);
                else if (key == "build")
                    m_BuildOnOpen = value != "0";
                continue;
            }

            RecentProject recent;
            size_t tab = line.find('\t');
            if (tab != std::string::npos) {
                recent.LastOpened = std::strtoll(line.c_str(), nullptr, 10);
                recent.Path = FileDialog::FromUtf8(line.substr(tab + 1));
            }
            else {
                recent.Path = FileDialog::FromUtf8(line); // formato antigo: so o caminho
            }
            if (recent.Path.empty())
                continue;

            bool duplicate = std::any_of(m_RecentProjects.begin(), m_RecentProjects.end(),
                [&](const RecentProject& other) { return other.Path == recent.Path; });
            if (duplicate || m_RecentProjects.size() >= kMaxRecentProjects)
                continue;

            ResolveRecent(recent);
            m_RecentProjects.push_back(std::move(recent));
        }
    }

    void ProjectManagerLayer::SaveRecentProjects() {
        std::ofstream out(kRecentProjectsFile);
        if (!out.is_open())
            return;
        out << "@dir=" << FileDialog::ToUtf8(m_LastProjectsDirectory) << "\n";
        out << "@build=" << (m_BuildOnOpen ? 1 : 0) << "\n";
        for (const RecentProject& recent : m_RecentProjects)
            out << recent.LastOpened << '\t' << FileDialog::ToUtf8(recent.Path) << "\n";
    }

    void ProjectManagerLayer::AddToRecentProjects(const std::filesystem::path& prismprojFile) {
        auto it = std::find_if(m_RecentProjects.begin(), m_RecentProjects.end(),
            [&](const RecentProject& recent) { return recent.Path == prismprojFile; });
        if (it != m_RecentProjects.end())
            m_RecentProjects.erase(it);

        RecentProject recent;
        recent.Path = prismprojFile;
        recent.LastOpened = NowSeconds();
        ResolveRecent(recent);
        m_RecentProjects.insert(m_RecentProjects.begin(), std::move(recent));

        if (m_RecentProjects.size() > kMaxRecentProjects)
            m_RecentProjects.resize(kMaxRecentProjects);
    }

}
