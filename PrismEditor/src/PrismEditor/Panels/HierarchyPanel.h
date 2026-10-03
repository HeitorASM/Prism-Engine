#pragma once

// ============================================================================
// HierarchyPanel.h
// Painel Hierarquia: arvore de entidades (arrastar para reparentar, soltar
// prefab), menu de contexto por entidade e o popup "Criar Prefab". Antes:
// ~340 linhas e 4 membros de EditorLayer.
// ============================================================================

#include "../Core/EditorContext.h"
#include "EntityContextMenuPanel.h"

namespace PrismEditor {

    class HierarchyPanel {
    public:
        // Liga o menu de contexto de entidade (ver EntityContextMenuPanel.h)
        // as acoes que tem acesso a CommandHistory/Scene/popups.
        explicit HierarchyPanel(EditorContext& context);

        void OnImGuiRender();

        // Desenha o popup modal "Criar Prefab" (mesmo padrao de
        // RenderSaveAsPopup) - pede o nome do arquivo, chama
        // Prism::PrefabSerializer::Serialize(m_PrefabToCreateFrom, ...) ao
        // confirmar, salvando dentro de Project::GetPrefabDirectory().
        // Aberto pelo item "Criar Prefab..." do menu de contexto (ver
        // Panels/EntityContextMenuPanel.h, callback SetOnCreatePrefabRequested
        // ligado em OnAttach()). Chamado a cada frame de
        // RenderDockspace(), mesmo fechado (mesmo motivo de todo popup
        // modal aqui - ImGui::OpenPopup exige isso).
        void RenderCreatePrefabPopup();

        // ID do popup modal "Criar Prefab" (o EditorLayer precisa dele para nao
        // disparar atalhos enquanto o popup esta aberto).
        static constexpr const char* kCreatePrefabPopupId = "Criar Prefab";

    private:
        // Desenha um unico node da arvore (e recursivamente seus filhos) -
        // extraido de OnImGuiRender() porque a recursao precisa
        // chamar a si mesma para cada nivel da hierarquia.
        void RenderHierarchyNode(Prism::Entity entity);

        EditorContext& m_Ctx;

        // Menu de contexto (botao direito) de uma entidade - Duplicar/
        // Criar Prefab.../Excluir (ver Panels/EntityContextMenuPanel.h).
        // Os callbacks (SetOnDuplicate/SetOnDelete/SetOnCreatePrefabRequested)
        // sao ligados uma unica vez em OnAttach() as funcoes deste
        // EditorLayer que de fato conhecem CommandHistory/Scene/popups -
        // o painel em si nao guarda nenhum estado de Scene, so desenha o
        // popup e avisa o que foi escolhido.
        EntityContextMenuPanel m_EntityContextMenu;

        // Estado do popup modal "Criar Prefab" (mesmo padrao de
        // m_ShowSaveAsPopup/m_SaveAsNameBuffer acima) - aberto pelo item
        // "Criar Prefab..." do menu de contexto de uma entidade (ver
        // Panels/EntityContextMenuPanel.h). m_PrefabToCreateFrom guarda QUAL
        // entidade vai virar a raiz do prefab - capturada no momento do
        // clique no menu, nao lida de EditorContext::SelectedEntity de novo ao
        // confirmar (o usuario pode trocar a selecao clicando em outro
        // lugar antes de digitar o nome e confirmar o popup).
        bool m_ShowCreatePrefabPopup = false;

        char m_CreatePrefabNameBuffer[128] = "";

        Prism::Entity m_PrefabToCreateFrom;
    };

}
