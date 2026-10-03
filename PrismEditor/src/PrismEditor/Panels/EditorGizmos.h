#pragma once

// ============================================================================
// EditorGizmos.h
// Gizmos de LINHA desenhados sobre a cena na viewport (camera, collider, luz,
// raycast). Antes eram 4 metodos de EditorLayer (~450 linhas); sao funcoes
// livres que so leem o EditorContext (cena e selecao) e desenham via
// Renderer::DrawLines. O gizmo de TRANSFORM (ImGuizmo) e interativo e fica no
// ViewportPanel.
// ============================================================================

#include "../Core/EditorContext.h"
#include <glm/glm.hpp>

namespace PrismEditor::EditorGizmos {

    // Desenha um wireframe de frustum (Renderer::DrawLines) para toda
    // entidade com CameraComponent na cena - a Primary usa uma cor
    // diferente das demais, para ficar claro qual camera o modo Play
    // vai usar. Chamado de dentro de ViewportPanel::RenderScene() (nao de
    // PropertiesPanel::RenderCameraPreview() - nao faz sentido a camera desenhar o
    // proprio gizmo dela mesma na sua propria preview), depois de
    // desenhar os meshes - ver EditorLayer.cpp.
    void RenderCameraGizmos(EditorContext& ctx, const glm::mat4& viewProjection);

    // Desenha o wireframe do ColliderComponent (Box/Sphere/Capsule) da
    // entidade atualmente SELECIONADA (EditorContext::SelectedEntity) - so dela, nao
    // de toda entidade com Collider da cena, para nao poluir a viewport
    // (diferente de RenderCameraGizmos, que desenha todas as cameras
    // sempre). Isso cobre o caso de querer ver a capsula de colisao de
    // um Character/player para saber exatamente onde ela esta (ex: para
    // posicionar uma camera fora dela) - clique na entidade na
    // Hierarchy ou na propria viewport para ver o gizmo. Chamado de
    // dentro de ViewportPanel::RenderScene(), depois de RenderCameraGizmos().
    void RenderSelectedColliderGizmo(EditorContext& ctx, const glm::mat4& viewProjection);

    // Desenha um wireframe indicando forma/alcance para toda entidade
    // com LightComponent na cena (Point: esfera de raio Range; Spot:
    // cone com angulo SpotAngle e comprimento Range; Directional: uma
    // seta indicando a direcao, sem alcance nenhum ja que e "infinita"
    // - ver LightComponent, Components.h). Desenhado para TODA luz da
    // cena (nao so a selecionada, mesmo padrao de RenderCameraGizmos)
    // porque, diferente de um Collider, a forma/alcance de uma luz e
    // informacao util para o layout geral da cena mesmo sem selecao -
    // sem isso, uma luz seria invisivel na viewport (nao tem
    // MeshRendererComponent, igual CameraComponent). A entidade
    // atualmente selecionada (EditorContext::SelectedEntity) e desenhada mais forte
    // (alpha maior via cor) que as demais, para se destacar sem
    // esconder as outras. Chamado de dentro de ViewportPanel::RenderScene(), depois
    // de RenderSelectedColliderGizmo().
    void RenderLightGizmos(EditorContext& ctx, const glm::mat4& viewProjection);

    // Desenha uma linha (Renderer::DrawLines) do centro de mundo ate o
    // ponto de impacto (RaycastComponent::HitPoint, se Hit) ou ate o
    // TargetPosition transformado para mundo (se nao acertou nada) de
    // TODA entidade com RaycastComponent na cena - mesmo padrao de
    // "toda entidade sempre visivel, nao so a selecionada" que
    // RenderLightGizmos ja usa (ver comentario la sobre o motivo: sem
    // gizmo, um RaycastComponent seria invisivel na viewport, ja que
    // nao tem MeshRendererComponent). Verde quando acertou algo,
    // cinza quando nao - mesma linguagem visual de "hit/miss" que a
    // maioria dos motores usa para debug de raycast. So mostra
    // resultado de verdade durante o modo Play (Scene::IsRunning) -
    // RaycastComponent::Hit fica congelado no ultimo valor fora disso
    // (ver Scene::UpdateRaycastComponents), entao o gizmo sempre
    // desenha a linha ATE TargetPosition (nunca um HitPoint desatualizado)
    // quando a Scene nao esta rodando.
    void RenderRaycastGizmos(EditorContext& ctx, const glm::mat4& viewProjection);

}
