#pragma once

// ============================================================================
// AnimationGraph.h
// Dados (serializados no AnimatorComponent) da maquina de estados de animacao,
// dos eventos de animacao e do root motion. So dados; a
// logica fica em AnimationSystem.
//
// Maquina de estados (estilo Unity/Godot AnimationTree simplificado):
//   - Parametros: Float, Bool ou Trigger (um Trigger vale ate ser consumido por
//     uma transicao). Scripts os mudam (entity:SetAnimFloat/Bool/Trigger).
//   - Estados: cada um toca um clipe (loop, velocidade propria).
//   - Transicoes: de um estado (ou de QUALQUER estado, From = -1) para outro,
//     com condicoes (todas verdadeiras), tempo de saida opcional (fracao do
//     clipe, 0..1) e duracao do crossfade.
// ============================================================================

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace Prism {

    enum class AnimParamType : uint8_t {
        Float = 0,
        Bool = 1,
        Trigger = 2
    };

    struct AnimParameter {
        std::string Name = "param";
        AnimParamType Type = AnimParamType::Float;
        float Default = 0.0f; // Bool: 0/1. Trigger: ignorado.
    };

    enum class AnimCompare : uint8_t {
        Greater = 0,   // Float > Value
        Less = 1,      // Float < Value
        Equal = 2,     // Float == Value (tolerancia)
        NotEqual = 3,
        IsTrue = 4,    // Bool
        IsFalse = 5,   // Bool
        Triggered = 6  // Trigger disparado
    };

    struct AnimCondition {
        std::string Param;
        AnimCompare Op = AnimCompare::Greater;
        float Value = 0.0f;
    };

    struct AnimState {
        std::string Name = "Estado";
        std::string Clip;          // nome do clipe do modelo
        bool Loop = true;
        float Speed = 1.0f;        // multiplica a velocidade do Animator
        glm::vec2 EditorPos{ 0.0f, 0.0f }; // posicao do no no editor de estados
    };

    struct AnimTransition {
        int From = -1;             // indice do estado; -1 = qualquer estado
        int To = 0;
        float Fade = 0.2f;         // crossfade, segundos
        bool HasExitTime = false;  // so transita depois de ExitTime do clipe
        float ExitTime = 0.9f;     // fracao 0..1 da duracao do clipe de origem
        std::vector<AnimCondition> Conditions; // todas devem valer (vazio + ExitTime = transicao automatica)
    };

    struct AnimGraph {
        std::vector<AnimParameter> Parameters;
        std::vector<AnimState> States;
        std::vector<AnimTransition> Transitions;
        int EntryState = 0;

        bool Empty() const { return States.empty(); }
    };

    // Evento disparado quando o clipe passa por 'Time' (segundos). O script da
    // entidade recebe OnAnimationEvent(nome).
    struct AnimEvent {
        std::string Clip;
        float Time = 0.0f;
        std::string Name;
    };

}
