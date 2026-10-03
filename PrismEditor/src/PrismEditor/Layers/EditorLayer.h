#pragma once

// ============================================================================
// EditorLayer.h
// O editor de verdade: dockspace com viewport 3D, hierarquia, propriedades,
// console, content browser e editor de script. So existe depois que um
// Project esta ativo (ver ProjectManagerLayer).
//
// Esta classe e so o ORQUESTRADOR: cria o estado compartilhado
// (EditorContext), os paineis e o documento (SceneDocument), desenha o
// dockspace e a barra de menus, trata atalhos e liga o modo Play. Cada painel
// e cada responsabilidade mora no seu proprio arquivo:
//
//   Core/EditorContext.h      estado compartilhado (cena, selecao, undo, camera)
//   Core/SceneDocument.h      novo/carregar/salvar mapa e "alteracoes nao salvas"
//   Core/EntityOps.h          duplicar/excluir/instanciar prefab/camera primaria
//   Core/MaterialLinkSync.h   vinculo vivo de material (.prismmat)
//   Panels/ViewportPanel.h    viewport 3D, camera livre, selecao, gizmo de transform
//   Panels/EditorGizmos.h     gizmos de linha (camera, collider, luz, raycast)
//   Panels/HierarchyPanel.h   arvore de entidades e popup "Criar Prefab"
//   Panels/PropertiesPanel.h  propriedades por component, prefab, popups de material/script
//   Panels/RenderSettingsPanel.h  menu Renderizacao
//   Panels/Console/ContentBrowser/ScriptEditor  paineis ja independentes
//
// Undo/Redo (Ctrl+Z / Ctrl+Y): ver EditorContext::History e
// PrismEditor/Commands/EditorCommands.h.
// ============================================================================

#include <Prism.h>
#include <imgui.h>
#include "../Core/EditorContext.h"
#include "../Core/SceneDocument.h"
#include "../Panels/ViewportPanel.h"
#include "../Panels/HierarchyPanel.h"
#include "../Panels/PropertiesPanel.h"
#include "../Panels/RenderSettingsPanel.h"
#include "../Panels/ConsolePanel.h"
#include "../Panels/ContentBrowserPanel.h"
#include "../Panels/ScriptEditorPanel.h"
#include "../Play/PlayWindow.h"

namespace PrismEditor {

    class EditorLayer : public Prism::Layer {
    public:
        EditorLayer();

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnImGuiRender() override;
        void OnEvent(Prism::Event& event) override;

    private:
        void RenderDockspace();
        void RenderMenuBar();

        // Chamado pelo botao "Play" da menu bar - abre a PlayWindow (janela
        // separada do SO, ver Play/PlayWindow.h) com uma COPIA clonada da
        // Scene ativa. A Scene de edicao nunca e tocada por scripts/fisica,
        // entao nao ha nada para restaurar ao parar.
        void OnPlayButtonClicked();

        // Chamado pelo botao "Parar" (so aparece quando a PlayWindow esta
        // aberta) - fecha a PlayWindow (para scripts/fisica da copia e
        // destroi a janela). A Scene de edicao, que nunca foi tocada, nao
        // precisa de nenhuma restauracao.
        void OnStopButtonClicked();

        void RenderConsolePanel();

        // ^ RenderConsolePanel() so delega para m_ConsolePanel.OnImGuiRender()
        //   - ver EditorLayer.cpp. O painel de verdade vive em
        //   PrismEditor/Panels/ConsolePanel.h porque tem estado e logica
        //   proprios (filtros, auto-scroll) grandes o bastante para nao
        //   fazer sentido inline aqui.
        void RenderContentBrowserPanel();

        // ^ mesmo padrao do Console: delega para m_ContentBrowser.OnImGuiRender().
        void RenderScriptEditorPanel();

    private:
        // A ORDEM dos membros importa: m_Ctx vem primeiro porque os paineis o
        // recebem por referencia no construtor.
        EditorContext m_Ctx;

        SceneDocument m_Document;
        ViewportPanel m_Viewport;
        HierarchyPanel m_Hierarchy;
        PropertiesPanel m_Properties;
        RenderSettingsPanel m_RenderSettings;

        // Painel de Console - le do Prism::LogBuffer (ver Prism.h) e tem
        // seu proprio estado de UI (filtros, auto-scroll), por isso vive em
        // uma classe separada em vez de ser so metodos soltos aqui.
        ConsolePanel m_ConsolePanel;

        // Painel de navegacao pelos arquivos do projeto (Assets/Maps/
        // Scripts/Cache) - primeira forma de ver o conteudo de um projeto
        // sem sair do editor. Duplo-clique num .prismmap chama SceneDocument::LoadScene().
        ContentBrowserPanel m_ContentBrowser;

        // Painel de edicao de scripts .lua (ver Panels/ScriptEditorPanel.h) -
        // aberto pelo botao "Editar" da UI do ScriptComponent (Properties
        // panel). So um script pode estar aberto por vez neste painel (nao e
        // um editor com abas ainda - ver ScriptEditorPanel::Open, que troca
        // o arquivo ativo em vez de abrir uma segunda instancia).
        ScriptEditorPanel m_ScriptEditor;

        // Janela de Play (janela separada do SO, ver Play/PlayWindow.h) -
        // dona de uma Scene CLONADA, nunca a mesma instancia de
        // EditorContext::ActiveScene. OnPlayButtonClicked()/OnStopButtonClicked() abrem/
        // fecham; OnUpdate() e chamado uma vez por frame (ver
        // EditorLayer::OnUpdate) enquanto m_PlayWindow.IsOpen().
        PrismEditor::PlayWindow m_PlayWindow;
    };

}
