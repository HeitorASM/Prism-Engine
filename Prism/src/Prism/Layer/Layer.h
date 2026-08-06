#pragma once

// ============================================================================
// Layer.h
// Uma Layer e uma "camada" da aplicacao com seu proprio ciclo de vida
// (OnAttach/OnDetach), sua propria logica por frame (OnUpdate) e seu proprio
// desenho de UI (OnImGuiRender), alem de poder reagir a eventos (OnEvent).
//
// Isto e o que torna "Editor = engine em modo editor" possivel sem hardcodar
// nada: o Editor nao e um caso especial dentro da Application - ele e so
// mais uma Layer empilhada. Quando, no futuro, existir um modo "Runtime"
// para jogos exportados, ele tambem sera so outra Layer (ou substituira a
// Layer do editor), sem exigir mudar Application, Window ou o core.
//
// Ordem de execucao: layers sao atualizadas na ordem em que foram inseridas;
// eventos sao propagados de tras pra frente (a layer do topo, geralmente UI/
// Editor, tem prioridade para "engolir" o evento antes que chegue embaixo).
// ============================================================================

#include "../Core/Event.h"
#include <string>

namespace Prism {

    class Layer {
    public:
        Layer(const std::string& name = "Layer") : m_DebugName(name) {}
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnImGuiRender() {}
        virtual void OnEvent(Event& event) {}

        const std::string& GetName() const { return m_DebugName; }

    protected:
        std::string m_DebugName;
    };

}
