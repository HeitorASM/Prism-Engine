#include "Project.h"
#include "ProjectSerializer.h"
#include "../Core/Log.h"
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
