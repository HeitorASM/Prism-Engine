#pragma once

// ============================================================================
// AnimatorRuntime.h
// Estado de execucao de um AnimatorComponent (tempo, clipe atual, crossfade e
// a paleta de ossos calculada). NAO e salvo na cena.
//
// Vive dentro do proprio component (ver AnimatorComponent, Components.h) via
// ResetOnCopy: copiar o component (Scene::Clone para o Play, desfazer/refazer,
// duplicar entidade) NAO copia o estado - a copia comeca "do zero" e o
// AnimationSystem recria o runtime sob demanda.
// ============================================================================

#include "ModelRig.h"
#include "AnimationGraph.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Prism {

    // Ponteiro que "esquece" o conteudo ao ser copiado (mover preserva).
    template<typename T>
    struct ResetOnCopy {
        std::shared_ptr<T> Ptr;

        ResetOnCopy() = default;
        ResetOnCopy(const ResetOnCopy&) {}
        ResetOnCopy& operator=(const ResetOnCopy&) { Ptr.reset(); return *this; }
        ResetOnCopy(ResetOnCopy&&) noexcept = default;
        ResetOnCopy& operator=(ResetOnCopy&&) noexcept = default;
    };

    struct AnimatorRuntime {
        std::shared_ptr<const ModelRig> Rig; // rig com o qual o estado abaixo foi montado

        bool Playing = false;
        bool Paused = false;
        bool Finished = false;   // clipe sem loop chegou ao fim (a pose final fica)

        int Clip = -1;
        float Time = 0.0f;
        bool Loop = true;

        // Crossfade: pose do clipe anterior mistura para a nova em FadeDuration s.
        int PrevClip = -1;
        float PrevTime = 0.0f;
        bool PrevLoop = true;
        float FadeDuration = 0.0f;
        float FadeElapsed = 0.0f;

        // --- Maquina de estados ---
        std::vector<float> ParamValues;      // por AnimGraph::Parameters (Bool 0/1; Trigger 0/1)
        int State = -1;                      // estado atual do grafo (-1 = grafo inativo / clipe avulso)
        float StateSpeed = 1.0f;             // AnimState::Speed do estado atual
        size_t GraphParamCount = 0;          // detecta mudanca do grafo (reinicia)
        bool WarnedMissingClip = false;

        // --- Eventos / root motion ---
        float EventPrevTime = -1.0f;         // tempo do clipe no passo anterior (-1 = acabou de comecar)
        int RootNode = -1;                   // no do osso raiz (root motion), -1 = nenhum

        std::vector<glm::mat4> Palette;      // Rig->Bones.size() matrizes (a que vai para o shader)
        std::vector<NodeTRS> PoseA, PoseB;   // buffers de trabalho
    };

}
