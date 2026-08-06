#include "ProjectManagerLayer.h"
#include "EditorLayer.h"

#include <imgui.h>
#include <fstream>
#include <algorithm>

namespace PrismEditor {

    // Onde guardamos a lista de projetos recentes. Fica ao lado do executavel
    // por simplicidade nesta fase - candidato natural a virar %APPDATA% depois.
    static const char* kRecentProjectsFile = "recent_projects.txt";

    ProjectManagerLayer::ProjectManagerLayer() : Layer("ProjectManagerLayer") {}

    void ProjectManagerLayer::OnUpdate(float deltaTime) {
        if (m_Finished) {
            // PopLayer() apenas enfileira a remocao; a Layer so e de fato
            // removida (e deletada) depois que Application::Run() termina de
            // iterar a LayerStack neste frame, entao ainda e seguro continuar
            // usando "this" no restante desta chamada se precisar. Mesmo
            // assim, evitamos guardar o ponteiro para uso em frames futuros.
            Prism::Application::Get().PopLayer(this);
            return;
        }
    }

    void ProjectManagerLayer::OnAttach() {
        LoadRecentProjectsList();

        // Sugestao inicial de caminho: pasta "PrismProjects" ao lado do executavel.
        std::filesystem::path suggested = std::filesystem::current_path() / "PrismProjects";
        strncpy(m_NewProjectPathBuffer, suggested.string().c_str(), sizeof(m_NewProjectPathBuffer) - 1);
    }

    void ProjectManagerLayer::OnImGuiRender() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
        ImGui::Begin("ProjectManager", nullptr, flags);

        ImGui::Dummy(ImVec2(0, 20));
        ImGui::SetWindowFontScale(1.6f);
        ImGui::Text("  Prism Engine");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("  Gerenciador de Projetos");
        ImGui::Dummy(ImVec2(0, 20));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        float halfWidth = ImGui::GetContentRegionAvail().x * 0.5f;

        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, halfWidth);

        RenderRecentProjectsList();

        ImGui::NextColumn();

        RenderNewProjectPanel();
        ImGui::Dummy(ImVec2(0, 15));
        RenderOpenProjectPanel();

        ImGui::Columns(1);

        ImGui::End();
    }

    void ProjectManagerLayer::RenderRecentProjectsList() {
        ImGui::Text("Projetos recentes");
        ImGui::Dummy(ImVec2(0, 8));

        if (m_RecentProjects.empty()) {
            ImGui::TextDisabled("Nenhum projeto recente ainda.");
        }

        // IMPORTANTE: nao chamamos OpenProject() diretamente daqui dentro
        // do loop. OpenProject() -> AddToRecentProjects() faz erase()+
        // insert() em m_RecentProjects, o que invalidaria os iteradores
        // deste 'for' NO MEIO da propria iteracao (use-after-free) -
        // exatamente o tipo de corrupcao de heap que o CRT em modo Debug
        // detecta e mata o processo com abort(). Em vez disso, so
        // registramos qual caminho foi clicado e abrimos o projeto DEPOIS
        // que o loop termina.
        std::filesystem::path clickedProject;
        bool clicked = false;

        for (const auto& path : m_RecentProjects) {
            std::string label = path.stem().string();
            std::string fullPathLabel = path.string();

            ImGui::PushID(fullPathLabel.c_str());
            if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, 40))) {
                clickedProject = path;
                clicked = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", fullPathLabel.c_str());
            }
            ImGui::PopID();
        }

        if (clicked)
            OpenProject(clickedProject);
    }

    void ProjectManagerLayer::RenderNewProjectPanel() {
        ImGui::Text("Criar novo projeto");
        ImGui::Dummy(ImVec2(0, 8));

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectName", m_NewProjectNameBuffer, sizeof(m_NewProjectNameBuffer));
        ImGui::TextDisabled("Nome do projeto");

        ImGui::Dummy(ImVec2(0, 6));

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectPath", m_NewProjectPathBuffer, sizeof(m_NewProjectPathBuffer));
        ImGui::TextDisabled("Pasta onde sera criado");

        ImGui::Dummy(ImVec2(0, 10));

        if (ImGui::Button("Criar Projeto", ImVec2(-1, 36))) {
            CreateAndOpenProject();
        }
    }

    void ProjectManagerLayer::RenderOpenProjectPanel() {
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Text("Abrir projeto existente");
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextDisabled("Informe o caminho do arquivo .prismproj:");

        static char pathBuffer[512] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##OpenProjectPath", pathBuffer, sizeof(pathBuffer));

        if (ImGui::Button("Abrir", ImVec2(-1, 36))) {
            if (strlen(pathBuffer) > 0) {
                OpenProject(std::filesystem::path(pathBuffer));
            }
        }

        // NOTA: um dialogo nativo de "Abrir Arquivo" (ex: via biblioteca
        // nativefiledialog ou tinyfiledialogs) e o proximo passo natural
        // aqui para substituir a caixa de texto por um seletor de verdade.
    }

    void ProjectManagerLayer::CreateAndOpenProject() {
        std::filesystem::path baseDir(m_NewProjectPathBuffer);
        std::string name(m_NewProjectNameBuffer);

        if (name.empty()) {
            PRISM_ERROR("Nome do projeto nao pode ser vazio.");
            return;
        }

        std::filesystem::path projectDir = baseDir / name;
        auto project = Prism::Project::New(projectDir, name);

        if (!project) {
            PRISM_ERROR("Falha ao criar o projeto.");
            return;
        }

        std::filesystem::path prismprojFile = projectDir / (name + ".prismproj");
        AddToRecentProjects(prismprojFile);
        SaveRecentProjectsList();

        // Troca de tela: entrega o controle ao EditorLayer e agenda a propria
        // remocao. Ambas as chamadas so enfileiram a operacao (ver
        // Application::PushLayer/PopLayer) - a troca de fato acontece com
        // seguranca no fim do frame atual, entao a ordem push-depois-pop
        // aqui nao importa e nao ha risco de invalidar a LayerStack no meio
        // do OnImGuiRender que estamos executando agora.
        Prism::Application& app = Prism::Application::Get();
        app.PushLayer(new EditorLayer());
        m_Finished = true;
    }

    void ProjectManagerLayer::OpenProject(const std::filesystem::path& prismprojFile) {
        auto project = Prism::Project::Load(prismprojFile);
        if (!project) {
            PRISM_ERROR("Falha ao abrir o projeto: ", prismprojFile.string());
            return;
        }

        AddToRecentProjects(prismprojFile);
        SaveRecentProjectsList();

        Prism::Application& app = Prism::Application::Get();
        app.PushLayer(new EditorLayer());
        m_Finished = true;
    }

    void ProjectManagerLayer::LoadRecentProjectsList() {
        m_RecentProjects.clear();
        std::ifstream in(kRecentProjectsFile);
        if (!in.is_open())
            return;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty())
                continue;

            // exists() sem error_code lanca excecao se o caminho for
            // invalido para o SO (caracteres proibidos, etc.) em vez de so
            // retornar false. Uma linha corrompida/antiga no
            // recent_projects.txt nao deve derrubar o editor inteiro.
            std::error_code ec;
            bool pathExists = std::filesystem::exists(line, ec);
            if (ec || !pathExists)
                continue;

            m_RecentProjects.push_back(line);
        }
    }

    void ProjectManagerLayer::SaveRecentProjectsList() {
        std::ofstream out(kRecentProjectsFile);
        for (const auto& path : m_RecentProjects)
            out << path.string() << "\n";
    }

    void ProjectManagerLayer::AddToRecentProjects(const std::filesystem::path& prismprojFile) {
        auto it = std::find(m_RecentProjects.begin(), m_RecentProjects.end(), prismprojFile);
        if (it != m_RecentProjects.end())
            m_RecentProjects.erase(it);
        m_RecentProjects.insert(m_RecentProjects.begin(), prismprojFile);
    }

}
