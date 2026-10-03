#pragma once

// ============================================================================
// ComponentEditUtils.h
// Ajudantes para as UIs de component (PropertiesPanel_<Nome>.cpp) registrarem
// Undo/Redo com EditComponentCommand<T> (ver Commands/EditorCommands.h).
//
// Dois casos, conforme o tipo de widget:
//
//  1) Widget CONTINUO (DragFloat, SliderFloat, ColorEdit): o valor muda a cada
//     frame do arraste. Chame TrackContinuousEdit logo APOS o widget (refere-se
//     ao ULTIMO item desenhado): guarda o estado "antes" quando o gesto comeca
//     e cria UM comando quando termina - mesmo padrao do Transform.
//
//         ImGui::DragFloat("FOV", &camera.FOV);
//         TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
//
//  2) Widget DISCRETO (Combo, Checkbox): o valor muda num unico frame, no
//     retorno 'true' do widget. Copie o component ANTES de desenhar e chame
//     CommitComponentEdit quando o widget devolver true:
//
//         auto before = light;
//         if (ImGui::Combo(...)) { light.Type = ...; CommitComponentEdit(m_Ctx, before, light, "Light"); }
//
//     (IsItemDeactivatedAfterEdit nao e confiavel para Combo: o valor muda
//     no popup, depois que o item do combo ja foi desativado.)
//
// O comando e executado ao ser criado (CommandHistory::Execute chama
// Execute()), o que reatribui o valor 'depois' que o component ja tem - e
// inofensivo.
// ============================================================================

#include "../Core/EditorContext.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>

namespace PrismEditor {

    template<typename T>
    inline void CommitComponentEdit(EditorContext& ctx, const T& before, const T& after, const char* displayName) {
        ctx.History.Execute(Prism::CreateScope<EditComponentCommand<T>>(ctx.SelectedEntity, before, after, displayName));
    }

    template<typename T>
    inline void TrackContinuousEdit(EditorContext& ctx, T& beforeSlot, const T& current, const char* displayName) {
        if (ImGui::IsItemActivated())
            beforeSlot = current;
        if (ImGui::IsItemDeactivatedAfterEdit())
            CommitComponentEdit(ctx, beforeSlot, current, displayName);
    }

}
