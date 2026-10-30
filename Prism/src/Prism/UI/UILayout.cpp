#include "UILayout.h"

#include <algorithm>
#include <cmath>

namespace Prism::Ui {

    namespace {

        // Decodifica um codepoint UTF-8 em 's' a partir de 'i' e devolve o numero de bytes (>= 1).
        size_t Utf8Length(const std::string& s, size_t i) {
            unsigned char c = (unsigned char)s[i];
            size_t extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
            size_t length = 1;
            while (length <= extra && i + length < s.size() && (((unsigned char)s[i + length]) & 0xC0) == 0x80)
                length++;
            return length;
        }

        class Layouter {
        public:
            Layouter(Document& document, float scale, const Measure& measure)
                : m_Doc(document), m_Scale(scale), m_Measure(measure) {}

            void Run(float screenWidth, float screenHeight) {
                for (Node& node : m_Doc.Nodes) {
                    node.Bounds = {};
                    node.Lines.clear();
                    node.ScaledFontSize = node.FontSize * m_Scale;
                }
                const Rect screen{ 0, 0, screenWidth, screenHeight };
                for (int root : m_Doc.Roots)
                    if (m_Doc.Nodes[(size_t)root].Visible)
                        LayoutInStack(root, screen);
            }

        private:
            Node& N(int index) { return m_Doc.Nodes[(size_t)index]; }
            float S(float value) const { return value * m_Scale; }

            Insets ScaledInsets(const Insets& i) const { return { S(i.Left), S(i.Top), S(i.Right), S(i.Bottom) }; }

            // Area de conteudo de um elemento: Bounds menos o padding.
            Rect ContentOf(const Node& node) const {
                Insets padding = ScaledInsets(node.Padding);
                Rect r;
                r.X = node.Bounds.X + padding.Left;
                r.Y = node.Bounds.Y + padding.Top;
                r.W = std::max(0.0f, node.Bounds.W - padding.Horizontal());
                r.H = std::max(0.0f, node.Bounds.H - padding.Vertical());
                return r;
            }

            std::vector<int> VisibleChildren(int index) {
                std::vector<int> result;
                for (int child : N(index).Children)
                    if (N(child).Visible)
                        result.push_back(child);
                return result;
            }

            // ----- Tamanho natural ----------------------------------------------------
            // Tamanho (caixa da borda, sem margem) que o elemento pede, dado o espaco
            // horizontal disponivel 'availWidth' (usado para quebrar texto).
            Size Intrinsic(int index, float availWidth) {
                Node& node = N(index);
                switch (node.Type) {
                    case NodeType::Spacer:
                        return {};
                    case NodeType::Image: {
                        Size natural = m_Measure.ImageSize(node.Src);
                        return { S(natural.W), S(natural.H) };
                    }
                    case NodeType::Text: {
                        std::vector<std::string> lines;
                        BreakLines(node.Text, node.FontSize * m_Scale, node.Wrap, availWidth, m_Measure, lines);
                        return TextBlock(lines, node.FontSize * m_Scale);
                    }
                    case NodeType::Button: {
                        Insets padding = ScaledInsets(node.Padding);
                        std::vector<std::string> lines;
                        BreakLines(node.Text, node.FontSize * m_Scale, node.Wrap, availWidth - padding.Horizontal(), m_Measure, lines);
                        Size text = TextBlock(lines, node.FontSize * m_Scale);
                        return { text.W + padding.Horizontal(), text.H + padding.Vertical() };
                    }
                    case NodeType::Panel:
                        return PanelIntrinsic(index, availWidth);
                }
                return {};
            }

            Size TextBlock(const std::vector<std::string>& lines, float fontPx) const {
                float width = 0;
                for (const std::string& line : lines)
                    width = std::max(width, m_Measure.TextWidth(line, fontPx));
                return { width, fontPx * kLineHeightFactor * (float)lines.size() };
            }

            // Largura/altura "preferidas" de um filho para o calculo do tamanho do pai.
            // auto e fill pedem o tamanho do conteudo (um fill dentro de um pai auto nao tem de onde
            // tirar espaco: o pai se ajusta ao conteudo e o fill ocupa o que sobrar). % conta 0
            // (depende do pai, que e justamente o que se esta calculando).
            Size Preferred(int child, float availWidth) {
                Node& node = N(child);
                Insets margin = ScaledInsets(node.Margin);
                const float availForChild = std::max(0.0f, availWidth - margin.Horizontal());
                auto contentSized = [](const Length& length) { return length.K == Length::Kind::Auto || length.K == Length::Kind::Fill; };

                Size natural{};
                if (contentSized(node.Width) || contentSized(node.Height))
                    natural = Intrinsic(child, node.Width.K == Length::Kind::Px ? S(node.Width.V) : availForChild);

                const float width = node.Width.K == Length::Kind::Px ? S(node.Width.V) : contentSized(node.Width) ? natural.W : 0.0f;
                const float height = node.Height.K == Length::Kind::Px ? S(node.Height.V) : contentSized(node.Height) ? natural.H : 0.0f;
                return { width + margin.Horizontal(), height + margin.Vertical() };
            }

            Size PanelIntrinsic(int index, float availWidth) {
                Node& node = N(index);
                Insets padding = ScaledInsets(node.Padding);
                const float innerWidth = std::max(0.0f, availWidth - padding.Horizontal());
                std::vector<int> children = VisibleChildren(index);

                float width = 0, height = 0;
                if (node.Layout == LayoutMode::Stack) {
                    for (int child : children) {
                        Size size = Preferred(child, innerWidth);
                        width = std::max(width, size.W);
                        height = std::max(height, size.H);
                    }
                }
                else {
                    const bool horizontal = node.Layout == LayoutMode::Row;
                    float main = 0, cross = 0;
                    for (size_t i = 0; i < children.size(); i++) {
                        Size size = Preferred(children[i], innerWidth);
                        main += horizontal ? size.W : size.H;
                        cross = std::max(cross, horizontal ? size.H : size.W);
                        if (i > 0)
                            main += S(node.Gap);
                    }
                    width = horizontal ? main : cross;
                    height = horizontal ? cross : main;
                }
                return { width + padding.Horizontal(), height + padding.Vertical() };
            }

            // ----- Layout -------------------------------------------------------------
            // Termina o elemento depois que Bounds esta definido: texto em linhas e filhos.
            void Finish(int index) {
                Node& node = N(index);
                node.ScaledFontSize = node.FontSize * m_Scale;
                if (node.Type == NodeType::Text) {
                    BreakLines(node.Text, node.ScaledFontSize, node.Wrap, node.Bounds.W, m_Measure, node.Lines);
                }
                else if (node.Type == NodeType::Button) {
                    Insets padding = ScaledInsets(node.Padding);
                    BreakLines(node.Text, node.ScaledFontSize, node.Wrap, node.Bounds.W - padding.Horizontal(), m_Measure, node.Lines);
                }
                else if (node.Type == NodeType::Panel) {
                    LayoutChildren(index);
                }
            }

            // Tamanho em um eixo para filhos de um Stack (ou raizes).
            float AxisInStack(const Length& length, float parentSize, float marginTotal, float intrinsic) const {
                switch (length.K) {
                    case Length::Kind::Px: return S(length.V);
                    case Length::Kind::Percent: return parentSize * length.V / 100.0f;
                    case Length::Kind::Fill: return std::max(0.0f, parentSize - marginTotal);
                    case Length::Kind::Auto: break;
                }
                return intrinsic;
            }

            void LayoutInStack(int child, const Rect& area) {
                Node& node = N(child);
                Insets margin = ScaledInsets(node.Margin);
                const float availWidth = std::max(0.0f, area.W - margin.Horizontal());

                // Largura primeiro (o texto quebrado depende dela para a altura).
                Size natural{};
                if (node.Width.K == Length::Kind::Auto || node.Height.K == Length::Kind::Auto) {
                    float probeWidth = availWidth;
                    if (node.Width.K != Length::Kind::Auto)
                        probeWidth = AxisInStack(node.Width, area.W, margin.Horizontal(), 0.0f);
                    natural = Intrinsic(child, probeWidth);
                }
                const float width = AxisInStack(node.Width, area.W, margin.Horizontal(), natural.W);
                const float height = AxisInStack(node.Height, area.H, margin.Vertical(), natural.H);

                float x = 0, y = 0;
                switch (node.Anchoring) {
                    case Anchor::TopLeft: case Anchor::Left: case Anchor::BottomLeft:
                        x = area.X + margin.Left; break;
                    case Anchor::Top: case Anchor::Center: case Anchor::Bottom:
                        x = area.X + (area.W - width) * 0.5f + (margin.Left - margin.Right) * 0.5f; break;
                    case Anchor::TopRight: case Anchor::Right: case Anchor::BottomRight:
                        x = area.X + area.W - margin.Right - width; break;
                }
                switch (node.Anchoring) {
                    case Anchor::TopLeft: case Anchor::Top: case Anchor::TopRight:
                        y = area.Y + margin.Top; break;
                    case Anchor::Left: case Anchor::Center: case Anchor::Right:
                        y = area.Y + (area.H - height) * 0.5f + (margin.Top - margin.Bottom) * 0.5f; break;
                    case Anchor::BottomLeft: case Anchor::Bottom: case Anchor::BottomRight:
                        y = area.Y + area.H - margin.Bottom - height; break;
                }
                node.Bounds = { x, y, width, height };
                Finish(child);
            }

            void LayoutChildren(int index) {
                Node& node = N(index);
                const Rect content = ContentOf(node);
                std::vector<int> children = VisibleChildren(index);
                if (children.empty())
                    return;

                if (node.Layout == LayoutMode::Stack) {
                    for (int child : children)
                        LayoutInStack(child, content);
                    return;
                }
                LayoutFlex(index, children, content);
            }

            struct Slot {
                int Index = -1;
                float Main = 0, Cross = 0;
                float MainStart = 0, MainEnd = 0, CrossStart = 0, CrossEnd = 0;
                float Weight = 0;
                bool Flex = false;
                bool CrossFixed = false; // cross definido pelo proprio elemento (nao e esticado pelo align)
            };

            float CrossSize(const Node& node, const Length& length, float contentCross, float availCross, Align align, float intrinsicCross, bool& fixedByElement) const {
                fixedByElement = true;
                switch (length.K) {
                    case Length::Kind::Px: return S(length.V);
                    case Length::Kind::Percent: return contentCross * length.V / 100.0f;
                    case Length::Kind::Fill: return std::max(0.0f, availCross);
                    case Length::Kind::Auto: break;
                }
                (void)node;
                fixedByElement = false;
                return align == Align::Stretch ? std::max(0.0f, availCross) : intrinsicCross;
            }

            void LayoutFlex(int index, const std::vector<int>& children, const Rect& content) {
                Node& node = N(index);
                const bool horizontal = node.Layout == LayoutMode::Row;
                const float contentMain = horizontal ? content.W : content.H;
                const float contentCross = horizontal ? content.H : content.W;
                const float gap = S(node.Gap);

                std::vector<Slot> slots;
                slots.reserve(children.size());
                float totalWeight = 0.0f;
                float used = 0.0f;

                for (int child : children) {
                    Node& c = N(child);
                    Insets margin = ScaledInsets(c.Margin);
                    Slot slot;
                    slot.Index = child;
                    slot.MainStart = horizontal ? margin.Left : margin.Top;
                    slot.MainEnd = horizontal ? margin.Right : margin.Bottom;
                    slot.CrossStart = horizontal ? margin.Top : margin.Left;
                    slot.CrossEnd = horizontal ? margin.Bottom : margin.Right;
                    const float availCross = contentCross - slot.CrossStart - slot.CrossEnd;
                    const Length& mainLength = horizontal ? c.Width : c.Height;
                    const Length& crossLength = horizontal ? c.Height : c.Width;

                    slot.Flex = mainLength.K == Length::Kind::Fill;
                    slot.Weight = slot.Flex ? std::max(mainLength.V, 0.0001f) : 0.0f;

                    if (horizontal) {
                        // Principal = largura. Fixa/auto agora; fill so depois de distribuir.
                        if (!slot.Flex) {
                            if (mainLength.K == Length::Kind::Px) slot.Main = S(mainLength.V);
                            else if (mainLength.K == Length::Kind::Percent) slot.Main = contentMain * mainLength.V / 100.0f;
                            else slot.Main = Intrinsic(child, std::max(0.0f, contentMain - slot.MainStart - slot.MainEnd)).W;
                        }
                    }
                    else {
                        // Principal = altura; depende da largura (transversal), que vem primeiro.
                        Size natural{};
                        if (crossLength.K == Length::Kind::Auto && node.AlignItems != Align::Stretch)
                            natural = Intrinsic(child, std::max(0.0f, availCross));
                        slot.Cross = CrossSize(c, crossLength, contentCross, availCross, node.AlignItems, natural.W, slot.CrossFixed);
                        if (!slot.Flex) {
                            if (mainLength.K == Length::Kind::Px) slot.Main = S(mainLength.V);
                            else if (mainLength.K == Length::Kind::Percent) slot.Main = contentMain * mainLength.V / 100.0f;
                            else slot.Main = Intrinsic(child, slot.Cross).H;
                        }
                    }

                    used += slot.MainStart + slot.MainEnd + (slot.Flex ? 0.0f : slot.Main);
                    totalWeight += slot.Weight;
                    slots.push_back(slot);
                }
                used += gap * (float)(slots.size() - 1);

                // Distribui o que sobra entre os filhos 'fill'.
                float leftover = contentMain - used;
                if (totalWeight > 0.0f) {
                    const float free = std::max(0.0f, leftover);
                    for (Slot& slot : slots)
                        if (slot.Flex)
                            slot.Main = free * slot.Weight / totalWeight;
                    leftover = 0.0f; // 'fill' consome tudo: justify nao se aplica
                }

                // Eixo transversal dos que dependiam do principal (Row) e dos 'fill'.
                for (Slot& slot : slots) {
                    Node& c = N(slot.Index);
                    const float availCross = contentCross - slot.CrossStart - slot.CrossEnd;
                    if (horizontal) {
                        Size natural{};
                        if (c.Height.K == Length::Kind::Auto && node.AlignItems != Align::Stretch)
                            natural = Intrinsic(slot.Index, slot.Main);
                        slot.Cross = CrossSize(c, c.Height, contentCross, availCross, node.AlignItems, natural.H, slot.CrossFixed);
                    }
                    else if (slot.Flex) {
                        // Coluna com altura fill: a altura ja e a fatia; o cross (largura) foi calculado acima.
                    }
                }

                // Justify.
                float cursor = 0.0f;
                float extraGap = 0.0f;
                if (leftover > 0.0f) {
                    switch (node.JustifyContent) {
                        case Justify::Start: break;
                        case Justify::Center: cursor = leftover * 0.5f; break;
                        case Justify::End: cursor = leftover; break;
                        case Justify::SpaceBetween:
                            if (slots.size() > 1) extraGap = leftover / (float)(slots.size() - 1);
                            break;
                    }
                }

                for (Slot& slot : slots) {
                    Node& c = N(slot.Index);
                    cursor += slot.MainStart;
                    const float mainPos = cursor;
                    cursor += slot.Main + slot.MainEnd + gap + extraGap;

                    float crossPos = slot.CrossStart;
                    const float room = contentCross - slot.Cross;
                    switch (node.AlignItems) {
                        case Align::Start: case Align::Stretch: crossPos = slot.CrossStart; break;
                        case Align::Center: crossPos = room * 0.5f + (slot.CrossStart - slot.CrossEnd) * 0.5f; break;
                        case Align::End: crossPos = room - slot.CrossEnd; break;
                    }
                    if (horizontal)
                        c.Bounds = { content.X + mainPos, content.Y + crossPos, slot.Main, slot.Cross };
                    else
                        c.Bounds = { content.X + crossPos, content.Y + mainPos, slot.Cross, slot.Main };
                    Finish(slot.Index);
                }
            }

            Document& m_Doc;
            float m_Scale;
            const Measure& m_Measure;
        };

    } // namespace

    float ComputeScale(const Document& document, float screenWidth, float screenHeight) {
        float scale = 1.0f;
        const float byHeight = screenHeight / std::max(document.ReferenceHeight, 1.0f);
        const float byWidth = screenWidth / std::max(document.ReferenceWidth, 1.0f);
        switch (document.Scale) {
            case ScaleMode::None: scale = 1.0f; break;
            case ScaleMode::Height: scale = byHeight; break;
            case ScaleMode::Width: scale = byWidth; break;
            case ScaleMode::Min: scale = std::min(byHeight, byWidth); break;
        }
        if (!std::isfinite(scale))
            return 1.0f;
        return std::min(std::max(scale, 0.1f), 10.0f);
    }

    void BreakLines(const std::string& text, float fontPx, bool wrap, float maxWidth, const Measure& measure,
                    std::vector<std::string>& outLines) {
        outLines.clear();
        const bool doWrap = wrap && maxWidth > 0.0f;
        const float spaceWidth = doWrap ? measure.TextWidth(" ", fontPx) : 0.0f;
        // Tolerancia de arredondamento: a largura de um elemento "auto" vem de TextWidth da linha
        // inteira, e aqui ela e somada palavra a palavra; sem folga, a mesma linha poderia quebrar.
        maxWidth += 0.01f;

        // Divide em paragrafos por '\n' (descarta '\r').
        std::vector<std::string> paragraphs(1);
        for (char c : text) {
            if (c == '\n') paragraphs.emplace_back();
            else if (c != '\r') paragraphs.back().push_back(c);
        }

        for (const std::string& paragraph : paragraphs) {
            if (!doWrap) {
                outLines.push_back(paragraph);
                continue;
            }
            std::string current;
            float currentWidth = 0.0f;

            // Coloca uma palavra que nao cabe sozinha numa linha, quebrando por caractere.
            auto placeLongWord = [&](const std::string& word) {
                std::string piece;
                float pieceWidth = 0.0f;
                for (size_t i = 0; i < word.size();) {
                    const size_t length = Utf8Length(word, i);
                    const std::string glyph = word.substr(i, length);
                    const float glyphWidth = measure.TextWidth(glyph, fontPx);
                    if (!piece.empty() && pieceWidth + glyphWidth > maxWidth) {
                        outLines.push_back(piece);
                        piece.clear();
                        pieceWidth = 0.0f;
                    }
                    piece += glyph;
                    pieceWidth += glyphWidth;
                    i += length;
                }
                current = piece;
                currentWidth = pieceWidth;
            };

            size_t i = 0;
            while (i < paragraph.size()) {
                while (i < paragraph.size() && paragraph[i] == ' ')
                    i++;
                if (i >= paragraph.size())
                    break;
                size_t end = paragraph.find(' ', i);
                if (end == std::string::npos)
                    end = paragraph.size();
                const std::string word = paragraph.substr(i, end - i);
                i = end;
                const float wordWidth = measure.TextWidth(word, fontPx);

                if (current.empty()) {
                    if (wordWidth <= maxWidth) { current = word; currentWidth = wordWidth; }
                    else placeLongWord(word);
                }
                else if (currentWidth + spaceWidth + wordWidth <= maxWidth) {
                    current += ' ';
                    current += word;
                    currentWidth += spaceWidth + wordWidth;
                }
                else {
                    outLines.push_back(current);
                    current.clear();
                    currentWidth = 0.0f;
                    if (wordWidth <= maxWidth) { current = word; currentWidth = wordWidth; }
                    else placeLongWord(word);
                }
            }
            outLines.push_back(current); // tambem empurra a linha vazia de um paragrafo vazio
        }
    }

    void LayoutDocument(Document& document, float screenWidth, float screenHeight, const Measure& measure) {
        const float scale = ComputeScale(document, screenWidth, screenHeight);
        Layouter layouter(document, scale, measure);
        layouter.Run(screenWidth, screenHeight);
    }

}
