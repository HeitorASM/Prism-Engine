#pragma once

// ============================================================================
// EntityContextMenuPanel.h
// Menu de contexto (botao direito) de UMA entidade - Duplicar / Criar
// Prefab.../ Excluir. Extraido de EditorLayer::RenderEntityContextMenu para
// um arquivo proprio, mesmo padrao dos outros paineis (ContentBrowserPanel,
// ConsolePanel) - deixa mais facil adicionar novas acoes aqui (Renomear,
// Copiar/Colar, Isolar, etc) sem mexer no EditorLayer.cpp, que ja e grande.
//
// Compartilhado entre a Hierarchy panel (botao direito sobre um node) e a
// Viewport (quando/se um dia o botao direito la for liberado para isso -
// ver comentario grande em EditorLayer::RenderViewportPanel sobre RMB ja
// ser usado para "modo voar" da camera) - por isso este painel NAO conhece
// Hierarchy nem Viewport, so recebe a Entity e desenha o popup; quem chama
// decide ONDE isso aparece.
//
// Este painel NAO tem estado proprio nem dono de nenhum dado da Scene - as
// acoes de verdade (duplicar, excluir, criar prefab) continuam vivendo no
// EditorLayer (que tem o CommandHistory e o resto do estado do editor),
// entregues aqui como callbacks. Isso evita que este painel precise
// conhecer Prism::CommandHistory, popups de outros paineis, etc - ele so
// sabe desenhar o menu e AVISAR o que o usuario escolheu.
// ============================================================================

#include <Prism.h>
#include <functional>

namespace PrismEditor {

    class EntityContextMenuPanel {
    public:
        // Callback chamado quando o usuario escolhe "Duplicar" no menu.
        using DuplicateFn = std::function<void(Prism::Entity)>;
        void SetOnDuplicate(DuplicateFn fn) { m_OnDuplicate = std::move(fn); }

        // Callback chamado quando o usuario escolhe "Excluir" no menu.
        using DeleteFn = std::function<void(Prism::Entity)>;
        void SetOnDelete(DeleteFn fn) { m_OnDelete = std::move(fn); }

        // Callback chamado quando o usuario escolhe "Criar Prefab..." no
        // menu - o chamador (EditorLayer) e quem efetivamente abre o
        // popup de nomear/salvar o prefab (ver EditorLayer::RenderCreatePrefabPopup),
        // este painel so avisa QUAL entidade foi escolhida como origem.
        using CreatePrefabRequestedFn = std::function<void(Prism::Entity)>;
        void SetOnCreatePrefabRequested(CreatePrefabRequestedFn fn) { m_OnCreatePrefabRequested = std::move(fn); }

        // Desenha o popup de contexto para 'entity' - precisa ser chamado
        // logo apos o "gatilho" (o item que o botao direito deve abrir o
        // menu sobre, ex: o TreeNodeEx de um node da Hierarchy), pois
        // ImGui::BeginPopupContextItem() reage ao ULTIMO item desenhado.
        //
        // 'uniqueId' PRECISA ser diferente para cada entidade/local onde
        // este painel e chamado no mesmo frame (ex: o handle da entidade)
        // - sem isso, todo popup de todo node da Hierarchy compartilharia
        // o MESMO ID interno do ImGui (BeginPopupContextItem com uma
        // string fixa gera sempre o mesmo ID, independente de QUAL item
        // foi clicado), causando o aviso "2 visible items with
        // conflicting ID" e popups fantasmas/errados quando dois nodes
        // ficam com o popup "aberto" ao mesmo tempo internamente no
        // ImGui. Passar entity.GetHandle() (ja unico por definicao, ver
        // Entity.h) resolve isso.
        void OnImGuiRender(Prism::Entity entity, uint32_t uniqueId);

    private:
        DuplicateFn m_OnDuplicate;
        DeleteFn m_OnDelete;
        CreatePrefabRequestedFn m_OnCreatePrefabRequested;
    };

}
