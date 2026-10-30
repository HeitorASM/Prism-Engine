#pragma once

// ============================================================================
// UIInput.h
// Hover, aperto e clique dos botoes de UM Ui::Document. Mesma regra da UI antiga:
// o clique so dispara quando o mouse e SOLTO sobre o mesmo botao em que foi apertado,
// e o elemento mais acima que barra o ponteiro vence. So std; testado em tests/UITests.cpp.
// ============================================================================

#include "UIDocument.h"

#include <vector>

namespace Prism::Ui {

    class Interaction {
    public:
        struct Result {
            bool PointerOverUI = false;   // o mouse esta sobre algo que barra o ponteiro
            int Hovered = -1;             // botao sob o mouse (interagivel), ou -1
            int Pressed = -1;             // botao apertado, ou -1
            std::vector<int> Clicked;     // botoes clicados neste quadro
        };

        // 'pointerFree' = false quando o cursor esta travado (modo camera): a UI nao recebe mouse.
        // Chame depois de LayoutDocument (usa Node::Bounds).
        Result Update(const Document& document, float mouseX, float mouseY, bool mouseDown, bool pointerFree);

        // Esquece apertado/hover (ao iniciar ou parar o Play, ou recarregar o documento).
        void Reset() { m_Pressed = -1; m_PreviousDown = false; }

        int Pressed() const { return m_Pressed; }

    private:
        int m_Pressed = -1;
        bool m_PreviousDown = false;
    };

}
