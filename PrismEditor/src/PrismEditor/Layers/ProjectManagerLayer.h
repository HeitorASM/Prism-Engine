#pragma once

// ============================================================================
// ProjectManagerLayer.h
// Primeira tela que o usuario ve ao abrir o PrismEditor: projetos recentes
// (esquerda) e a criacao de um projeto novo (direita), com dialogos nativos
// para escolher pasta/arquivo e validacao do nome e do local em tempo real.
// Quando o usuario efetivamente abre/cria um projeto, esta Layer se remove
// da Application e da lugar a EditorLayer (troca de "tela", nao troca de
// processo - continua tudo dentro do mesmo executavel/janela). O caminho
// inverso (Arquivo > Fechar Projeto) empilha esta Layer de volta.
//
// Todo projeto e C++ primeiro: ao criar (ou abrir um projeto antigo sem
// Source/) o projeto de scripts C++ e gerado automaticamente. Scripts Lua
// continuam suportados, sem opcao propria aqui.
//
// Persistencia: recent_projects.txt ao lado do executavel.
//   @dir=<ultima pasta de criacao>   @build=<0|1 compilar ao abrir>
//   <unix time>\t<caminho do .prismproj>        (um por linha, mais recente primeiro)
// Linhas antigas (so o caminho) continuam sendo lidas.
// ============================================================================

#include <Prism.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PrismEditor {

    class ProjectManagerLayer : public Prism::Layer {
    public:
        ProjectManagerLayer();

        void OnAttach() override;
        void OnUpdate(float deltaTime) override;
        void OnImGuiRender() override;

    private:
        struct RecentProject {
            std::filesystem::path Path;     // .prismproj
            std::int64_t LastOpened = 0;    // unix time; 0 = desconhecido (entrada antiga)
            std::string Name;
            std::string EngineVersion;
            bool Exists = true;
        };

        void RenderRecentProjects(float width, float height);
        void RenderNewProject(float width, float height);
        void RenderFooter();

        // Recalcula m_ValidationError quando nome/local mudam (ou a cada ~1 s,
        // para pegar mudancas feitas no disco).
        void RefreshValidation();
        void SuggestProjectName();

        void CreateAndOpenProject();
        void OpenProject(const std::filesystem::path& prismprojFile);
        void EnterEditor();

        void LoadRecentProjects();
        void SaveRecentProjects();
        void AddToRecentProjects(const std::filesystem::path& prismprojFile);
        void ResolveRecent(RecentProject& recent);

    private:
        char m_NameBuffer[128] = "";
        char m_ParentBuffer[1024] = "";   // UTF-8 (ver FileDialog.h)

        std::string m_ValidationError;    // "" = pode criar
        std::string m_ValidatedName;
        std::string m_ValidatedParent;
        double m_ValidatedAt = 0.0;
        bool m_ValidationDirty = true;

        std::string m_CreateError;        // falha ao criar (mostrada acima do botao)
        std::string m_OpenError;          // falha ao abrir (mostrada na lista de recentes)

        bool m_BuildOnOpen = true;        // compila os scripts C++ ao abrir o editor
        std::filesystem::path m_LastProjectsDirectory;

        std::vector<RecentProject> m_RecentProjects;
    };

}
