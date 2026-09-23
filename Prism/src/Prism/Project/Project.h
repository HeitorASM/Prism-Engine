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
    };

    // Ajustes de renderizacao guardados POR PROJETO (no .prismproj): cada
    // projeto tem o seu "look". Editados no menu "Renderizacao" do editor
    // (EditorLayer::RenderRenderSettingsMenu) e aplicados ao Renderer por
    // Renderer::ApplyRenderSettings.
    //
    // Os valores padrao abaixo sao a FONTE UNICA da verdade: os estaticos
    // do Renderer (s_Exposure, s_Ambient, s_Env*) sao inicializados a partir
    // deles, entao os dois nunca divergem. As cores sao sRGB (o que o color
    // picker mostra); ver Renderer::SetEnvironmentColors.
    struct RenderSettings {
        // Multiplicador de brilho aplicado em espaco linear ANTES do tone
        // mapping (Renderer::SetExposure). 1.0 = neutro.
        float Exposure = 1.0f;

        // Brilho MEDIO do ambiente (Renderer::SetAmbient). 0 = so as luzes
        // iluminam.
        float Ambient = 0.10f;

        // Gradiente de ambiente: ceu (+Y), horizonte e chao (-Y).
        float EnvZenith[3]  = { 0.36f, 0.55f, 0.95f };
        float EnvHorizon[3] = { 0.80f, 0.85f, 0.90f };
        float EnvGround[3]  = { 0.22f, 0.20f, 0.18f };

        // Limites aceitos ao LER um .prismproj (um arquivo editado a mao ou
        // corrompido nao pode deixar a cena preta ou estourada). Exposicao
        // <= 0 zeraria a imagem inteira.
        static constexpr float kExposureMin = 0.05f;
        static constexpr float kExposureMax = 16.0f;
        static constexpr float kAmbientMax  = 2.0f;
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

        // Ajustes de renderizacao deste projeto (exposicao, ambiente). A
        // versao nao-const e o que o editor edita; depois de mudar, chame
        // Renderer::ApplyRenderSettings para aplicar e Project::SaveActive
        // para persistir.
        RenderSettings& GetRenderSettings() { return m_RenderSettings; }
        const RenderSettings& GetRenderSettings() const { return m_RenderSettings; }

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

        // Carrega um projeto existente a partir do caminho do seu .prismproj.
        static Ref<Project> Load(const std::filesystem::path& projectFilePath);

        // Salva a configuracao atual do projeto ativo de volta pro .prismproj.
        static bool SaveActive();

        // Define o mapa/cena principal do projeto e persiste isso no
        // .prismproj imediatamente (chama SaveActive() internamente). O
        // caminho e relativo a GetMapDirectory() - ver ProjectConfig::StartMap.
        // Usado por EditorLayer::SaveActiveScene() no primeiro save de uma
        // cena nova, para que o editor saiba qual mapa reabrir da proxima vez.
        static bool SetStartMap(const std::filesystem::path& relativeMapPath);

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;
        std::filesystem::path m_ProjectFilePath;

        // ATENCAO: fica NO FIM da classe DE PROPOSITO - nao mova para o
        // meio. Project.h e incluido por muitos .cpp (via Prism.h e
        // Renderer.h); um membro novo no meio deslocaria m_ProjectDirectory
        // e m_ProjectFilePath, e qualquer .obj ainda compilado com o layout
        // antigo (build incremental) leria/escreveria no lugar errado -
        // corrupcao de heap, o mesmo bug que ja ocorreu no EditorLayer.h.
        // No fim, o layout de tudo que ja existia continua identico.
        RenderSettings m_RenderSettings;

        // Tambem NO FIM, pelo mesmo motivo do m_RenderSettings acima - e
        // DEPOIS dele, para que o layout de tudo que ja existia continue
        // identico.
        AssetRegistry m_AssetRegistry;

        inline static Ref<Project> s_ActiveProject;

        friend class ProjectSerializer;
    };

}
