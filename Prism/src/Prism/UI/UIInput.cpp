#include "UIInput.h"

namespace Prism::Ui {

    Interaction::Result Interaction::Update(const Document& document, float mouseX, float mouseY, bool mouseDown, bool pointerFree) {
        Result result;

        if (pointerFree) {
            // Do ultimo desenhado (mais acima) para o primeiro.
            for (int i = (int)document.Nodes.size() - 1; i >= 0; i--) {
                const Node& node = document.Nodes[(size_t)i];
                if (!document.IsEffectivelyVisible(i))
                    continue;
                const bool isButton = node.Type == NodeType::Button;
                const bool blocks = isButton || (node.BlocksPointer && node.Type != NodeType::Text && node.Type != NodeType::Spacer);
                if (!blocks || !node.Bounds.Contains(mouseX, mouseY))
                    continue;
                result.PointerOverUI = true;
                if (isButton && node.Interactable)
                    result.Hovered = i;
                break; // o de cima barra os de baixo
            }
        }

        // Um botao apertado que sumiu/mudou de indice nao pode clicar.
        if (m_Pressed >= (int)document.Nodes.size())
            m_Pressed = -1;

        if (mouseDown && !m_PreviousDown && result.Hovered >= 0) {
            m_Pressed = result.Hovered;
        }
        else if (!mouseDown && m_PreviousDown && m_Pressed >= 0) {
            if (result.Hovered == m_Pressed)
                result.Clicked.push_back(m_Pressed);
            m_Pressed = -1;
        }
        if (!mouseDown)
            m_Pressed = -1;
        m_PreviousDown = mouseDown;

        result.Pressed = m_Pressed;
        return result;
    }

}
