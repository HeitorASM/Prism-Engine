#pragma once

// ============================================================================
// ProjectManagerLayer.h
// Primeira tela que o usuario ve ao abrir o PrismEditor: criar um projeto
// novo, abrir um existente, ou escolher entre os projetos recentes.
// Quando o usuario efetivamente abre/cria um projeto, esta Layer se remove
// da Application e da lugar a EditorLayer (troca de "tela", nao troca de
// processo - continua tudo dentro do mesmo executavel/janela).
// ============================================================================

#include <Prism.h>
#include <string>
#include <vector>
#include <filesystem>

namespace PrismEditor {

    class ProjectManagerLayer : public Prism::Layer {
    public:
        ProjectManagerLayer();

        void OnAttach() override;
        void OnUpdate(float deltaTime) override;
        void OnImGuiRender() override;

    private:
        void RenderNewProjectPanel();
        void RenderOpenProjectPanel();
        void RenderRecentProjectsList();

        void CreateAndOpenProject();
        void OpenProject(const std::filesystem::path& prismprojFile);
        void LoadRecentProjectsList();
        void SaveRecentProjectsList();
        void AddToRecentProjects(const std::filesystem::path& prismprojFile);

    private:
        char m_NewProjectNameBuffer[128] = "MinhaEngineProject";
        char m_NewProjectPathBuffer[512] = "";

        std::vector<std::filesystem::path> m_RecentProjects;

        bool m_ShowNewProjectPopup = false;

        // Quando true, a proxima chamada de OnUpdate se remove da LayerStack.
        // Fazemos a remocao no OnUpdate (nao no meio do CreateAndOpenProject)
        // porque remover "this" da pilha enquanto ainda estamos dentro de uma
        // funcao membro chamada PELA propria pilha e arriscado (use-after-free
        // potencial). Adiar para o proximo OnUpdate e o padrao seguro.
        bool m_Finished = false;
    };

}
