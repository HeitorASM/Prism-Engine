#pragma once

// ============================================================================
// ParticleCurveWidgets.h
// Editores ImGui da curva e do gradiente de particulas (Prism/Particles/ParticleMath.h).
//
// Interacao (curva):    clique e arraste um ponto; duplo clique no vazio adiciona;
//                       clique direito num ponto remove (sempre sobram 2).
// Interacao (gradiente): clique numa parada para seleciona-la e arraste para mover;
//                       duplo clique no vazio adiciona; clique direito numa parada remove.
//
// UNDO: o widget NAO conhece o historico. Ele devolve um ParticleWidgetResult e o
// chamador registra UM comando por gesto (ver PropertiesPanel_ParticleEmitter.cpp):
//
//     auto before = emitter;                       // copia ANTES do widget (o gesto pode
//     auto r = ParticleCurveEditor(...);           // alterar o valor no mesmo quadro em que comeca)
//     if (r.Started)  m_BeforeEdit = before;
//     if (r.Finished) CommitComponentEdit(m_Ctx, m_BeforeEdit, emitter, title);
//
// 'Finished' so vem true se o valor mudou durante o gesto.
// ============================================================================

#include <Prism/Particles/ParticleMath.h>

namespace PrismEditor {

    struct ParticleWidgetResult {
        bool Changed = false;   // o valor mudou NESTE quadro
        bool Started = false;   // um gesto comecou neste quadro
        bool Finished = false;  // um gesto terminou neste quadro E alterou o valor
    };

    // Curva com eixo vertical [minValue, maxValue]. 'label' identifica o widget (unico na tela).
    ParticleWidgetResult ParticleCurveEditor(const char* label, Prism::ParticleCurve& curve, float minValue, float maxValue, float height = 90.0f);

    // Gradiente RGBA. A parada selecionada fica guardada no estado do ImGui:
    // use ParticleGradientSelection(label) para editar a cor dela (ImGui::ColorEdit4).
    ParticleWidgetResult ParticleGradientEditor(const char* label, Prism::ParticleGradient& gradient);

    // Indice da parada selecionada (0..Count-1) do gradiente 'label'. Chame no MESMO escopo de ID do editor.
    int ParticleGradientSelection(const char* label, const Prism::ParticleGradient& gradient);

}
