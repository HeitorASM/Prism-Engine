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

        static Ref<Project> GetActive() { return s_ActiveProject; }

        // Cria um novo projeto do zero: cria a pasta, a estrutura de subpastas
        // e escreve o arquivo .prismproj inicial. Retorna nullptr em caso de falha.
        static Ref<Project> New(const std::filesystem::path& directory, const std::string& name);

        // Carrega um projeto existente a partir do caminho do seu .prismproj.
        static Ref<Project> Load(const std::filesystem::path& projectFilePath);

        // Salva a configuracao atual do projeto ativo de volta pro .prismproj.
        static bool SaveActive();

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;
        std::filesystem::path m_ProjectFilePath;

        inline static Ref<Project> s_ActiveProject;

        friend class ProjectSerializer;
    };

}
