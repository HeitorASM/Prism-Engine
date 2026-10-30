#pragma once

// ============================================================================
// LevelEditTool.h
// Ferramenta de edicao de geometria (Level Builder) DENTRO do painel Viewport:
//   - overlay 2D (ImGui draw list) com arestas, vertices e faces selecionadas;
//   - selecao por clique (vertice / aresta / face; Shift soma/alterna);
//   - gizmo de mover (ImGuizmo) no centro da selecao, com snap de grade;
//   - atalhos (1/2/3 modo, Del, Ctrl+E extrudar, Ctrl+I inset, Ctrl+A, Esc, Tab).
// A logica das operacoes e da selecao esta em Core/LevelEditing.*; aqui so ha entrada e desenho.
//
// O overlay NAO faz oclusao (e um wireframe sobre a imagem), como o modo "wireframe" de editores 3D.
// ============================================================================

#include "../Core/EditorContext.h"
#include <imgui.h>
#include <glm/glm.hpp>

namespace PrismEditor::LevelEditTool {

    // True se a entidade selecionada tem LevelMeshComponent e a edicao esta ligada.
    bool IsEditing(EditorContext& ctx);

    // Botoes da barra da viewport: "Editar nivel (Tab)" e, ligado, os modos Vertice/Aresta/Face.
    // So desenha algo quando a entidade selecionada tem LevelMeshComponent (usa ImGui::SameLine).
    void DrawToolbar(EditorContext& ctx);

    // Atalhos globais da viewport (Tab liga/desliga a edicao quando ha um LevelMesh selecionado).
    // Chamar quando a viewport esta focada e a camera nao esta em modo voar.
    void HandleToggleShortcut(EditorContext& ctx);

    // Desenha overlay + trata clique/atalhos/gizmo. Chamar dentro da janela "Viewport", depois de
    // ImGui::Image. 'imageMin' = canto superior esquerdo da imagem na tela; 'size' = tamanho em pixels.
    // Devolve true se esta ferramenta usou o clique esquerdo deste quadro (a viewport nao deve
    // selecionar entidades).
    bool Render(EditorContext& ctx, const glm::mat4& view, const glm::mat4& projection,
                const ImVec2& imageMin, const ImVec2& size, bool hovered, bool focused);

}
