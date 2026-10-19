#pragma once

// ============================================================================
// Project.h
// Um Project representa a unidade fundamental de trabalho na Prism Engine:
// uma pasta no disco contendo assets, cenas, scripts e um arquivo de
// configuracao (.prismproj). E o analogo direto do "project.godot" no Godot
// ou do .uproject na Unreal.
//
// Este e o alicerce sobre o qual TUDO mais se apoia: o editor so pode abrir
// paineis de assets/cena depois que um Project existe; brushes/BSP vao viver
// dentro de "Maps" que pertencem a um Project; scripts Lua serao procurados
// dentro da pasta Scripts/ do projeto ativo.
// ============================================================================

#include "../Core/Base.h"
#include "../Assets/AssetRegistry.h"
#include <string>
#include <vector>
#include <filesystem>

namespace Prism {

    struct ProjectConfig {
        std::string Name = "Untitled Project";

        // Todos os caminhos abaixo sao relativos a raiz do projeto.
        std::filesystem::path AssetDirectory = "Assets";
        std::filesystem::path ScriptDirectory = "Scripts";
        std::filesystem::path MapDirectory = "Maps";
        std::filesystem::path StartMap; // mapa aberto ao iniciar o jogo, se houver

        // Versao da engine (Version::Number) que criou o projeto. Vazio em
        // projetos criados antes desta chave existir. O editor so usa isso
        // para avisar quando a versao difere da atual.
        std::string EngineVersion;
    };

    // Opcoes da tela "Novo projeto" do editor (ver Project::New abaixo).
    struct ProjectCreateOptions {
        std::string Name;                        // nome do projeto = nome da pasta = nome do .prismproj
        std::filesystem::path ParentDirectory;   // pasta ONDE a pasta do projeto sera criada
        std::string EngineVersion;               // gravado em ProjectConfig::EngineVersion
    };

    // Leitura rapida de um .prismproj sem carrega-lo de verdade (usada na
    // lista de projetos recentes: nao varre assets nem troca o projeto ativo).
    struct ProjectSummary {
        std::string Name;
        std::string EngineVersion;
    };

    // Estrutura de pastas criada para todo novo projeto. Mantida em um so
    // lugar para que o ProjectManager e testes usem exatamente a mesma lista.
    inline const std::vector<std::string>& GetDefaultProjectFolders() {
        static const std::vector<std::string> folders = {
            "Assets",
            "Assets/Models",
            "Assets/Textures",
            "Assets/Audio",
            "Assets/Materials",
            "Assets/Prefabs",
            "Scripts",
            "Maps",
            "Cache"     // dados derivados/importados - nao deve ir pro controle de versao
        };
        return folders;
    }

    class Project {
    public:
        const ProjectConfig& GetConfig() const { return m_Config; }
        const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }

        std::filesystem::path GetAssetDirectory() const { return m_ProjectDirectory / m_Config.AssetDirectory; }
        std::filesystem::path GetScriptDirectory() const { return m_ProjectDirectory / m_Config.ScriptDirectory; }
        std::filesystem::path GetMapDirectory() const { return m_ProjectDirectory / m_Config.MapDirectory; }

        // Pasta onde .prismprefab (entidades reutilizaveis) fica - ver
        // PrefabSerializer.h. NAO tem uma entrada propria em ProjectConfig
        // (ao contrario de AssetDirectory/ScriptDirectory/MapDirectory)
        // porque e sempre uma subpasta FIXA dentro de Assets/ (ver
        // GetDefaultProjectFolders acima, "Assets/Prefabs") - nao ha
        // necessidade de configurar isso por projeto. Projetos antigos
        // simplesmente nao tem a pasta no disco ate a primeira vez que algo
        // for salvo la (ver comentario em PrefabSerializer::Serialize
        // sobre create_directories).
        std::filesystem::path GetPrefabDirectory() const { return GetAssetDirectory() / "Prefabs"; }

        // Pasta onde .prismmat (materiais reutilizaveis) fica - ver
        // MaterialSerializer.h. Mesmo raciocinio de GetPrefabDirectory
        // acima: subpasta fixa dentro de Assets/, "Assets/Materials" ja
        // fazia parte de GetDefaultProjectFolders desde antes deste
        // sistema existir (a pasta ja era usada para as TEXTURAS que um
        // MaterialComponent referencia - AlbedoPath etc - agora tambem
        // guarda os .prismmat que agrupam esses caminhos num asset
        // reutilizavel).
        std::filesystem::path GetMaterialDirectory() const { return GetAssetDirectory() / "Materials"; }

        // Indice de identidade dos assets deste projeto (AssetID <-> caminho,
        // ver Assets/AssetRegistry.h). Pertence ao Project: e recriado junto
        // com ele, e sempre aponta para GetAssetDirectory() deste projeto.
        // Ja vem VARRIDO quando Project::New/Load retornam - todo asset ja
        // tem um .meta com ID. Chame GetAssetRegistry().Refresh() de novo
        // para reconciliar mudancas feitas no disco com o editor aberto
        // (arquivos novos, movidos ou removidos).
        AssetRegistry& GetAssetRegistry() { return m_AssetRegistry; }
        const AssetRegistry& GetAssetRegistry() const { return m_AssetRegistry; }

        static Ref<Project> GetActive() { return s_ActiveProject; }

        // Cria um novo projeto do zero: cria a pasta, a estrutura de subpastas
        // e escreve o arquivo .prismproj inicial. Retorna nullptr em caso de falha.
        static Ref<Project> New(const std::filesystem::path& directory, const std::string& name);

        // Versao usada pela tela "Novo projeto": valida o nome e o local
        // (ValidateNewProject), cria a pasta <ParentDirectory>/<Name> com a
        // estrutura padrao, grava o .prismproj com EngineVersion e um
        // .gitignore (Cache/ e Binaries/). Em falha devolve nullptr e
        // preenche 'error' com um texto pronto para mostrar ao usuario.
        // NAO gera o projeto de scripts C++ (Source/): isso depende do modulo
        // Scripting e fica a cargo do editor (NativeScripting::GenerateProjectFiles).
        static Ref<Project> New(const ProjectCreateOptions& options, std::string& error);

        // Regras de nome de projeto: nome de pasta valido no Windows e em
        // sistemas POSIX (sem \ / : * ? " < > | nem caracteres de controle,
        // sem espaco/ponto no fim, sem nomes reservados como CON/NUL/COM1,
        // no maximo 64 caracteres). Em falha preenche 'reason'.
        static bool IsValidProjectName(const std::string& name, std::string& reason);

        // "" = pode criar. Senao, o motivo (nome invalido, local vazio, pasta
        // de destino ja existe e nao esta vazia...). Nao cria nada no disco.
        static std::string ValidateNewProject(const std::filesystem::path& parentDirectory, const std::string& name);

        // Le so Name/EngineVersion de um .prismproj. false se nao abrir ou
        // nao tiver Name.
        static bool ReadSummary(const std::filesystem::path& projectFilePath, ProjectSummary& outSummary);

        // Carrega um projeto existente a partir do caminho do seu .prismproj.
        static Ref<Project> Load(const std::filesystem::path& projectFilePath);

        // Salva a configuracao atual do projeto ativo de volta pro .prismproj.
        static bool SaveActive();

        // Define o mapa/cena principal do projeto e persiste isso no
        // .prismproj imediatamente (chama SaveActive() internamente). O
        // caminho e relativo a GetMapDirectory() - ver ProjectConfig::StartMap.
        // Usado por SceneDocument::SaveActiveScene() no primeiro save de uma
        // cena nova, para que o editor saiba qual mapa reabrir da proxima vez.
        static bool SetStartMap(const std::filesystem::path& relativeMapPath);

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;
        std::filesystem::path m_ProjectFilePath;

        // ATENCAO: fica NO FIM da classe DE PROPOSITO - nao mova para o
        // meio. Project.h e incluido por muitos .cpp; um membro no meio
        // deslocaria m_ProjectDirectory e m_ProjectFilePath em qualquer .obj
        // ainda compilado com o layout antigo (build incremental).
        // (O antigo RenderSettings foi removido: o visual agora vive no
        // EnvironmentComponent da cena e no CameraComponent.)
        AssetRegistry m_AssetRegistry;

        inline static Ref<Project> s_ActiveProject;

        friend class ProjectSerializer;
    };

}
