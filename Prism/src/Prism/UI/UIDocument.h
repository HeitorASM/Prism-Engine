#pragma once

// ============================================================================
// UIDocument.h
// Modelo de um documento de UI (.prismui): a arvore de elementos que o parser
// (UIMarkup) le do texto e que o layout (UILayout) posiciona. So std: sem
// OpenGL, GLFW, EnTT nem GLM - compila e e testado isoladamente
// (tests/UITests.cpp).
//
// A arvore e PLANA: 'Nodes' guarda todos os elementos em ordem de documento
// (pai antes dos filhos) e cada No aponta para os vizinhos por INDICE. Indices
// sobrevivem a copias do Document (ponteiros nao sobreviveriam) e servem de
// "handle" para os scripts (UIDocument::Find devolve o indice).
//
// A ordem de documento e a ordem de DESENHO: o que vem depois fica por cima.
// ============================================================================

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Prism::Ui {

    struct Color {
        float R = 1.0f, G = 1.0f, B = 1.0f, A = 1.0f;
        Color() = default;
        Color(float r, float g, float b, float a = 1.0f) : R(r), G(g), B(b), A(a) {}
        Color operator*(const Color& o) const { return { R * o.R, G * o.G, B * o.B, A * o.A }; }
        bool operator==(const Color& o) const { return R == o.R && G == o.G && B == o.B && A == o.A; }
    };

    struct Rect {
        float X = 0, Y = 0, W = 0, H = 0;
        bool Contains(float px, float py) const { return px >= X && py >= Y && px < X + W && py < Y + H; }
    };

    struct Insets {
        float Left = 0, Top = 0, Right = 0, Bottom = 0;
        float Horizontal() const { return Left + Right; }
        float Vertical() const { return Top + Bottom; }
    };

    struct Size {
        float W = 0, H = 0;
    };

    enum class NodeType : int { Panel = 0, Text, Image, Button, Spacer };

    // Como um Panel posiciona os filhos.
    //  Stack:  cada filho se ancora dentro do pai (Anchor + Margin), um sobre o outro.
    //  Row:    filhos em linha, da esquerda para a direita.
    //  Column: filhos em coluna, de cima para baixo.
    enum class LayoutMode : int { Stack = 0, Row, Column };

    enum class Anchor : int {
        TopLeft = 0, Top, TopRight,
        Left, Center, Right,
        BottomLeft, Bottom, BottomRight
    };

    // Alinhamento no eixo transversal de Row/Column.
    enum class Align : int { Start = 0, Center, End, Stretch };
    // Distribuicao no eixo principal de Row/Column (so quando nenhum filho e 'fill').
    enum class Justify : int { Start = 0, Center, End, SpaceBetween };
    enum class TextAlign : int { Left = 0, Center, Right };

    // Tamanho de um eixo.
    //  Auto:    o que o conteudo pede (texto medido, imagem, filhos).
    //  Px:      pixels (multiplicados pela escala do documento).
    //  Percent: porcentagem da area de conteudo do pai.
    //  Fill:    em Row/Column, fatia (peso Value) do espaco que sobra; em Stack, 100% do pai menos as margens.
    struct Length {
        enum class Kind : int { Auto = 0, Px, Percent, Fill };
        Kind K = Kind::Auto;
        float V = 0.0f;

        static Length Auto() { return {}; }
        static Length Px(float v) { Length l; l.K = Kind::Px; l.V = v; return l; }
        static Length Percent(float v) { Length l; l.K = Kind::Percent; l.V = v; return l; }
        static Length Fill(float weight = 1.0f) { Length l; l.K = Kind::Fill; l.V = weight; return l; }
    };

    // Como a UI escala com a resolucao. 'Reference' (no documento) e o tamanho de projeto.
    enum class ScaleMode : int { None = 0, Height, Width, Min };

    struct Node {
        NodeType Type = NodeType::Panel;
        std::string Id;                    // "" = sem id (nao da para achar por script)
        bool Visible = true;

        // --- Arvore (indices em Document::Nodes; -1 = nenhum) ---
        int Parent = -1;
        std::vector<int> Children;

        // --- Layout ---
        Anchor Anchoring = Anchor::TopLeft;
        Length Width, Height;
        Insets Margin;                     // fora do elemento (px de projeto)
        Insets Padding;                    // dentro do elemento (px de projeto)
        float Gap = 0.0f;                  // entre filhos de Row/Column
        LayoutMode Layout = LayoutMode::Stack;
        Align AlignItems = Align::Start;
        Justify JustifyContent = Justify::Start;

        // --- Aparencia ---
        Color Tint;                        // Panel/Button/Image: fundo (ou tint da imagem); Text: cor do texto
        Color TextColor;                   // Button: cor do rotulo
        bool HasHoverColor = false;        // false: o hover multiplica Tint por kDefaultHover
        Color HoverColor;                  // com HasHoverColor: cor ABSOLUTA do fundo no hover
        bool HasPressedColor = false;
        Color PressedColor;
        std::string Src;                   // Image/Panel/Button: textura (relativo ao projeto)

        // --- Texto (Text e Button) ---
        std::string Text;
        float FontSize = 24.0f;            // px de projeto
        TextAlign Align_ = TextAlign::Left;
        bool Wrap = false;                 // quebra automatica de linha pela largura do elemento

        // --- Interacao ---
        bool Interactable = true;          // Button
        bool BlocksPointer = true;         // barra o mouse para o jogo (UI.IsPointerOverUI); Text e Spacer nao barram
        std::string OnClick;               // nome da funcao chamada no clique ("" = chama OnClick() do script)

        // --- Resultado do layout (UILayout.h; em pixels de TELA, origem no canto superior esquerdo) ---
        Rect Bounds;
        std::vector<std::string> Lines;    // Text/Button: linhas apos a quebra automatica
        float ScaledFontSize = 24.0f;

        static constexpr float kDefaultHover = 0.85f;
        static constexpr float kDefaultPressed = 0.65f;
    };

    struct Diagnostic {
        enum class Severity : int { Warning = 0, Error };
        Severity Level = Severity::Error;
        int Line = 0, Column = 0;          // 1-based; 0 = sem posicao
        std::string Message;
    };

    struct Document {
        std::vector<Node> Nodes;           // ordem de documento
        std::vector<int> Roots;            // indices dos elementos sem pai, em ordem

        ScaleMode Scale = ScaleMode::None;
        float ReferenceWidth = 1920.0f;
        float ReferenceHeight = 1080.0f;

        std::unordered_map<std::string, int> ById; // id -> indice (so o PRIMEIRO de cada id)

        // Indice do elemento com esse id, ou -1.
        int Find(const std::string& id) const {
            auto it = ById.find(id);
            return it == ById.end() ? -1 : it->second;
        }
        bool IsValidIndex(int index) const { return index >= 0 && index < (int)Nodes.size(); }
        // Todo o caminho ate a raiz esta visivel?
        bool IsEffectivelyVisible(int index) const {
            for (int i = index; i >= 0; i = Nodes[(size_t)i].Parent)
                if (!Nodes[(size_t)i].Visible)
                    return false;
            return true;
        }
    };

}
