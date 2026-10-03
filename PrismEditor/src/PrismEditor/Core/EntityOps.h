#pragma once

// ============================================================================
// EntityOps.h
// Operacoes sobre entidades do mapa ativo que mais de um painel precisa
// (menu, atalhos, Hierarquia, Viewport). Antes eram metodos privados de
// EditorLayer; funcoes livres sobre o EditorContext evitam que os paineis
// dependam do EditorLayer.
// ============================================================================

#include "EditorContext.h"
#include <filesystem>

namespace PrismEditor::EntityOps {

        // Chamado por LoadScene logo apos trocar EditorContext::ActiveScene: sincroniza
    // TODA instancia de prefab da cena recem-carregada com o
    // .prismprefab de origem atual (preserva overrides - ver
    // PrefabSyncer::UpdateAll e o comentario grande na implementacao).
    void SyncAllPrefabInstances(EditorContext& ctx);

        // Instancia o prefab em 'prefabPath' dentro da Scene ativa via
    // InstantiatePrefabCommand (undo/redo - ver EditorCommands.h) e
    // seleciona a raiz recem-criada. Se 'parent' for uma Entity
    // valida, a raiz instanciada e reparentada para debaixo dela logo
    // em seguida (via SetParentCommand - ver RenderHierarchyNode, soltar
    // um prefab EM CIMA de um node existente o torna filho dele, em vez
    // de mais uma raiz solta na cena); default e instanciar como raiz
    // (mesmo comportamento de soltar na area vazia da Hierarchy panel
    // ou na Viewport). Dois comandos separados no historico (instanciar
    // + reparentar) em vez de um so - aceitavel aqui porque Undo()
    // desfaz os dois em sequencia de qualquer forma (o usuario so
    // precisa apertar Ctrl+Z, nao percebe que sao dois comandos).
    void InstantiatePrefab(EditorContext& ctx, const std::filesystem::path& prefabPath, Prism::Entity parent = {});

        // Duplica 'entity' via DuplicateEntityCommand e seleciona a copia -
    // logica compartilhada entre o menu de contexto (m_EntityContextMenu,
    // ver Panels/EntityContextMenuPanel.h) e o atalho de teclado Ctrl+D
    // (ver OnUpdate).
    void DuplicateEntity(EditorContext& ctx, Prism::Entity entity);

        // Exclui 'entity' via DeleteEntityCommand e limpa a selecao se ela
    // era a entidade excluida - logica compartilhada entre o menu de
    // contexto, a tecla Delete/Backspace (ver OnUpdate) e o item de
    // menu "Excluir selecionada" (ver RenderMenuBar).
    void DeleteEntity(EditorContext& ctx, Prism::Entity entity);

        // Garante que no maximo UMA entidade da cena tenha
    // CameraComponent::Primary = true: ao marcar 'newPrimary' como
    // Primary, desmarca qualquer outra que estivesse marcada.
    // Chamado pela Properties panel quando o checkbox "Primary" e
    // ligado - ver PropertiesPanel::OnImGuiRender().
    void SetPrimaryCamera(EditorContext& ctx, Prism::Entity newPrimary);

}
