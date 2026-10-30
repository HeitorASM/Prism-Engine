#pragma once

// ============================================================================
// LevelEditing.h
// Logica (sem ImGui) do Level Builder no editor: sincronia do estado com a cena,
// selecao de vertices/arestas/faces e as OPERACOES sobre o LevelMeshComponent
// selecionado. Cada operacao e UM passo de undo (EditLevelMeshCommand).
//
// A interface (viewport, overlay, gizmo) fica em Panels/LevelEditTool.*; os
// botoes e o editor de materiais por slot em Panels/PropertiesPanel_LevelMesh.cpp.
// ============================================================================

#include "EditorContext.h"
#include <string>
#include <vector>

namespace PrismEditor::LevelEditing {

    using Mode = LevelEditState::Mode;

    // Formas do menu "Criar > Level Builder".
    enum class Shape { Box, Room, Plane, Cylinder, Stairs, Ramp };
    const char* ShapeLabel(Shape shape);
    Prism::Level::EditableMesh MakeShape(Shape shape);

    // Mantem o estado coerente: desliga a edicao se a entidade selecionada mudou/perdeu o
    // LevelMeshComponent, descarta indices fora da malha (undo/redo podem encolhe-la).
    // Devolve o componente editado (ou nullptr se nao ha edicao possivel).
    Prism::LevelMeshComponent* Sync(EditorContext& ctx);
    void SetActive(EditorContext& ctx, bool active);   // so liga se a selecao tem LevelMeshComponent

    // --- selecao -------------------------------------------------------------------------
    void SetMode(EditorContext& ctx, Mode mode);       // converte a selecao entre modos
    void ClearSelection(EditorContext& ctx);
    void SelectAll(EditorContext& ctx);
    void InvertSelection(EditorContext& ctx);
    bool HasSelection(const LevelEditState& state);
    // Selecao pelo clique: 'indices' = o que foi acertado (1 vertice, 2 de uma aresta, 1 face).
    // 'additive' (Shift) alterna os elementos; senao substitui. Lista vazia + !additive limpa.
    void Pick(EditorContext& ctx, const std::vector<uint32_t>& indices, bool additive);

    // O que as operacoes enxergam: faces (Face = selecionadas; Vertice/Aresta = totalmente
    // cobertas pela selecao) e vertices (Face = os das faces).
    std::vector<uint32_t> EffectiveFaces(const Prism::Level::EditableMesh& mesh, const LevelEditState& state);
    std::vector<uint32_t> EffectiveVertices(const Prism::Level::EditableMesh& mesh, const LevelEditState& state);
    glm::vec3 SelectionCenter(const Prism::Level::EditableMesh& mesh, const LevelEditState& state);   // espaco do objeto

    // --- operacoes (devolvem false se nada mudou; cada true = um passo de undo) --------------
    bool Extrude(EditorContext& ctx);
    bool Inset(EditorContext& ctx);
    bool Subdivide(EditorContext& ctx);
    bool Detach(EditorContext& ctx);
    bool FlipSelectedFaces(EditorContext& ctx);
    bool DeleteSelectedFaces(EditorContext& ctx);
    bool MergeVertices(EditorContext& ctx);            // solda a malha inteira (MergeEpsilon)
    bool MirrorWholeMesh(EditorContext& ctx, int axis);
    bool RecenterPivot(EditorContext& ctx);            // origem no centro da malha; o objeto nao se mexe no mundo
    bool AssignSlotToSelection(EditorContext& ctx, uint32_t slot);
    bool SetSmoothingOnSelection(EditorContext& ctx, uint32_t group);
    bool AddMaterialSlot(EditorContext& ctx);

    // Comeca/termina o arraste do gizmo (usados por LevelEditTool).
    void BeginMove(EditorContext& ctx, const glm::vec3& pivot);
    void UpdateMove(EditorContext& ctx, const glm::vec3& newPivot);
    void EndMove(EditorContext& ctx);

}
