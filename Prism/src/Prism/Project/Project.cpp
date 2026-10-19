#include "Project.h"
#include "ProjectSerializer.h"
#include "../Core/Log.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace Prism {

    // Aponta o AssetRegistry para a pasta de assets do projeto e faz a
    // varredura inicial (cria os .meta que faltam). Compartilhado por New e
    // Load para os dois caminhos terminarem no MESMO estado.
    static void InitializeAssetRegistry(Project& project) {
        project.GetAssetRegistry().SetRoot(project.GetAssetDirectory());
        AssetRefreshReport report = project.GetAssetRegistry().Refresh();

        PRISM_CORE_INFO("Assets: ", report.Registered, " registrado(s)",
            report.NewlyCreated > 0 ? " (" + std::to_string(report.NewlyCreated) + " novo(s))" : std::string(),
            ".");
        for (const std::string& orphan : report.OrphanMetas)
            PRISM_CORE_WARN("Assets: .meta orfao (o asset nao existe mais): ", orphan);
    }

    Ref<Project> Project::New(const std::filesystem::path& directory, const std::string& name) {
        std::error_code ec;

        // exists() SEM error_code lanca filesystem_error se o SO recusar o
        // acesso ao caminho (permissao negada, caminho de rede indisponivel,
        // etc.) em vez de simplesmente retornar false. Como nada no main()
        // capturava excecoes ate agora, isso bastava para derrubar o
        // programa inteiro com abort() ao criar um projeto num caminho
        // problematico. Usando a overload com error_code, qualquer falha
        // vira um "false" tratavel, igual ja fazemos com create_directories
        // logo abaixo.
        bool dirExists = std::filesystem::exists(directory, ec);
        if (ec) {
            PRISM_CORE_ERROR("Falha ao verificar o diretorio do projeto: ", ec.message());
            return nullptr;
        }

        if (!dirExists) {
            if (!std::filesystem::create_directories(directory, ec)) {
                PRISM_CORE_ERROR("Falha ao criar diretorio do projeto: ", ec.message());
                return nullptr;
            }
        }

        for (const auto& folder : GetDefaultProjectFolders()) {
            std::filesystem::create_directories(directory / folder, ec);
        }

        auto project = CreateRef<Project>();
        project->m_Config.Name = name;
        project->m_ProjectDirectory = directory;
        project->m_ProjectFilePath = directory / (name + ".prismproj");

        ProjectSerializer serializer(project);
        if (!serializer.Serialize(project->m_ProjectFilePath)) {
            return nullptr;
        }

        InitializeAssetRegistry(*project);

        s_ActiveProject = project;
        PRISM_CORE_INFO("Projeto \"", name, "\" criado em ", directory.string());
        return project;
    }

    // ------------------------------------------------------------------
    // Validacao e criacao a partir da tela "Novo projeto"
    // ------------------------------------------------------------------

    bool Project::IsValidProjectName(const std::string& name, std::string& reason) {
        if (name.empty()) {
            reason = "Digite um nome para o projeto.";
            return false;
        }
        if (name.size() > 64) {
            reason = "O nome pode ter no maximo 64 caracteres.";
            return false;
        }
        if (name.front() == ' ' || name.back() == ' ') {
            reason = "O nome nao pode comecar nem terminar com espaco.";
            return false;
        }
        if (name.front() == '.' || name.back() == '.') {
            reason = "O nome nao pode comecar nem terminar com ponto.";
            return false;
        }
        for (unsigned char c : name) {
            if (c < 0x20 || std::string("<>:\"/\\|?*").find((char)c) != std::string::npos) {
                reason = "O nome nao pode conter os caracteres \\ / : * ? \" < > |";
                return false;
            }
        }

        // Nomes reservados do Windows (valem com ou sem extensao: "con.txt").
        std::string stem = name.substr(0, name.find('.'));
        std::string upper = stem;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });
        bool reserved = upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL";
        if (!reserved && upper.size() == 4 && (upper.compare(0, 3, "COM") == 0 || upper.compare(0, 3, "LPT") == 0))
            reserved = upper[3] >= '1' && upper[3] <= '9';
        if (reserved) {
            reason = "\"" + stem + "\" e um nome reservado do sistema. Escolha outro.";
            return false;
        }
        return true;
    }

    std::string Project::ValidateNewProject(const std::filesystem::path& parentDirectory, const std::string& name) {
        std::string reason;
        if (!IsValidProjectName(name, reason))
            return reason;
        if (parentDirectory.empty())
            return "Escolha a pasta onde o projeto sera criado.";

        std::error_code ec;
        if (std::filesystem::exists(parentDirectory, ec) && !std::filesystem::is_directory(parentDirectory, ec))
            return "O local escolhido nao e uma pasta.";
        // Se a pasta-mae nao existe, New() a cria (create_directories).

        std::filesystem::path target = parentDirectory / name;
        if (std::filesystem::exists(target, ec)) {
            if (!std::filesystem::is_directory(target, ec))
                return "Ja existe um arquivo chamado \"" + name + "\" nesse local.";
            if (!std::filesystem::is_empty(target, ec))
                return "A pasta \"" + name + "\" ja existe nesse local e nao esta vazia.";
        }
        return {};
    }

    Ref<Project> Project::New(const ProjectCreateOptions& options, std::string& error) {
        error = ValidateNewProject(options.ParentDirectory, options.Name);
        if (!error.empty())
            return nullptr;

        Ref<Project> project = New(options.ParentDirectory / options.Name, options.Name);
        if (!project) {
            error = "Nao foi possivel criar o projeto (veja o Console para o motivo).";
            return nullptr;
        }

        project->m_Config.EngineVersion = options.EngineVersion;
        SaveActive(); // New() ja gravou o arquivo, mas ainda sem a versao da engine

        // Cache/ (importados e copias do modulo) e Binaries/ (saida da
        // compilacao dos scripts) sao derivados: nunca devem ir para o Git.
        // Os .meta DEVEM ir (ver docs/formatos-de-arquivo.md).
        std::filesystem::path gitignore = project->m_ProjectDirectory / ".gitignore";
        std::error_code ec;
        if (!std::filesystem::exists(gitignore, ec)) {
            std::ofstream out(gitignore, std::ios::binary);
            out << "# Gerado pela Prism Engine\nCache/\nBinaries/\n";
        }
        return project;
    }

    bool Project::ReadSummary(const std::filesystem::path& projectFilePath, ProjectSummary& outSummary) {
        std::ifstream in(projectFilePath);
        if (!in.is_open())
            return false;

        ProjectSummary summary;
        bool hasName = false;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            if (key == "Name") { summary.Name = value; hasName = true; }
            else if (key == "EngineVersion") summary.EngineVersion = value;
        }
        if (!hasName)
            return false;
        outSummary = summary;
        return true;
    }

    Ref<Project> Project::Load(const std::filesystem::path& projectFilePath) {
        auto project = CreateRef<Project>();

        ProjectSerializer serializer(project);
        if (!serializer.Deserialize(projectFilePath)) {
            return nullptr;
        }

        project->m_ProjectFilePath = projectFilePath;
        project->m_ProjectDirectory = projectFilePath.parent_path();

        InitializeAssetRegistry(*project);

        s_ActiveProject = project;
        PRISM_CORE_INFO("Projeto \"", project->m_Config.Name, "\" carregado de ", projectFilePath.string());
        return project;
    }

    bool Project::SaveActive() {
        if (!s_ActiveProject)
            return false;

        ProjectSerializer serializer(s_ActiveProject);
        return serializer.Serialize(s_ActiveProject->m_ProjectFilePath);
    }

    bool Project::SetStartMap(const std::filesystem::path& relativeMapPath) {
        if (!s_ActiveProject)
            return false;

        s_ActiveProject->m_Config.StartMap = relativeMapPath;
        return SaveActive();
    }

}
