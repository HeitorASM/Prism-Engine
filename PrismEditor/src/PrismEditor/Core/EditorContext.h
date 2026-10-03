#pragma once

// ============================================================================
// EditorContext.h
// Estado que os paineis do editor COMPARTILHAM. Antes tudo isso eram membros
// privados de EditorLayer (uma classe com 3750 linhas); agora cada painel
// recebe um EditorContext& e so conhece o que precisa. O EditorLayer e o dono
// (cria o contexto e liga os ponteiros em OnAttach).
//
// Regra para novos paineis: estado que SO o painel usa fica no painel; estado
// que dois ou mais paineis leem/escrevem entra aqui.
// ============================================================================

#include <Prism.h>
#include "EditorCamera.h"
#include "MaterialLinkSync.h"
#include <filesystem>

namespace PrismEditor {

    class PlayWindow;
    class ContentBrowserPanel;
    class ScriptEditorPanel;

    struct EditorContext {
        // A cena ativa do editor. Vem do StartMap do projeto (dentro de
        // Project::GetMapDirectory(), ver Project.h) ou, se nao houver, e
        // uma cena de exemplo criada em memoria por SceneDocument::LoadOrCreateScene().
        Prism::Ref<Prism::Scene> ActiveScene;

        // Caminho (absoluto) do arquivo .prismmap associado a ActiveScene
        // - vazio significa "esta cena ainda nao foi salva em lugar
        // nenhum" (cena nova, criada por SceneDocument::NewMap() ou pela cena de exemplo
        // do primeiro OnAttach). "Salvar Mapa" grava neste caminho quando
        // ele existe; quando esta vazio, se comporta como "Salvar Como".
        std::filesystem::path CurrentMapPath;

        // Entidade atualmente selecionada na Hierarchy panel. Invalida
        // (Entity{}) quando nada esta selecionado.
        Prism::Entity SelectedEntity;

        // Historico de undo/redo do editor (ver Prism::CommandHistory /
        // PrismEditor::EditorCommands). Compartilhado por toda edicao de
        // Scene feita atraves da UI - Transform, cor, criar/excluir
        // entidade.
        Prism::CommandHistory History;

        // Camera livre da viewport (ver EditorCamera.h).
        EditorCamera Camera;

        // Vinculo vivo de Material (ver MaterialLinkSync.h).
        MaterialLinkSync MaterialLinks;

        // Ponteiros nao-donos para paineis que outros paineis precisam chamar.
        // Ligados pelo EditorLayer em OnAttach; nunca nulos depois disso.
        PlayWindow* Play = nullptr;
        ContentBrowserPanel* ContentBrowser = nullptr;
        ScriptEditorPanel* ScriptEditor = nullptr;
    };

}
