#include "UIMarkup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace Prism::Ui {

    namespace {

        constexpr int kMaxDepth = 64;
        constexpr size_t kMaxNodes = 10000;

        // --- Texto -------------------------------------------------------------
        std::string Lower(std::string text) {
            for (char& c : text)
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c + 32);
            return text;
        }

        // "top-left", "Top_Left", "topLeft" -> "topleft"
        std::string Normalize(const std::string& text) {
            std::string out;
            for (char c : text) {
                if (c == '-' || c == '_' || c == ' ')
                    continue;
                out.push_back((c >= 'A' && c <= 'Z') ? (char)(c + 32) : c);
            }
            return out;
        }

        std::string Trim(const std::string& text) {
            size_t b = 0, e = text.size();
            while (b < e && (text[b] == ' ' || text[b] == '\t' || text[b] == '\r' || text[b] == '\n')) b++;
            while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r' || text[e - 1] == '\n')) e--;
            return text.substr(b, e - b);
        }

        // Numero em 'text' a partir de 'pos' (sem usar o locale: strtod aceitaria virgula decimal).
        bool ParseFloatPrefix(const std::string& text, size_t& pos, float& out) {
            size_t i = pos;
            bool negative = false;
            if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
                negative = text[i] == '-';
                i++;
            }
            double value = 0.0;
            bool anyDigit = false;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                value = value * 10.0 + (text[i] - '0');
                anyDigit = true;
                i++;
            }
            if (i < text.size() && text[i] == '.') {
                i++;
                double scale = 0.1;
                while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                    value += (text[i] - '0') * scale;
                    scale *= 0.1;
                    anyDigit = true;
                    i++;
                }
            }
            if (!anyDigit)
                return false;
            if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
                size_t j = i + 1;
                bool expNegative = false;
                if (j < text.size() && (text[j] == '+' || text[j] == '-')) {
                    expNegative = text[j] == '-';
                    j++;
                }
                if (j < text.size() && text[j] >= '0' && text[j] <= '9') {
                    int exponent = 0;
                    while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
                        exponent = std::min(exponent * 10 + (text[j] - '0'), 400);
                        j++;
                    }
                    value *= std::pow(10.0, expNegative ? -exponent : exponent);
                    i = j;
                }
            }
            if (negative)
                value = -value;
            if (!std::isfinite(value) || std::fabs(value) > 1.0e9)
                return false;
            out = (float)value;
            pos = i;
            return true;
        }

        int EditDistance(const std::string& a, const std::string& b) {
            std::vector<int> previous(b.size() + 1), current(b.size() + 1);
            for (size_t j = 0; j <= b.size(); j++) previous[j] = (int)j;
            for (size_t i = 1; i <= a.size(); i++) {
                current[0] = (int)i;
                for (size_t j = 1; j <= b.size(); j++) {
                    int cost = a[i - 1] == b[j - 1] ? 0 : 1;
                    current[j] = std::min({ previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost });
                }
                std::swap(previous, current);
            }
            return previous[b.size()];
        }

        // " (quis dizer 'color'?)" ou "".
        std::string Suggest(const std::string& name, const std::vector<std::string>& candidates) {
            const std::string lowered = Lower(name);
            int best = 3;
            std::string bestName;
            for (const std::string& candidate : candidates) {
                int distance = EditDistance(lowered, Lower(candidate));
                if (distance < best) {
                    best = distance;
                    bestName = candidate;
                }
            }
            return bestName.empty() ? std::string() : " (quis dizer '" + bestName + "'?)";
        }

        void AppendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out.push_back((char)cp);
            }
            else if (cp < 0x800) {
                out.push_back((char)(0xC0 | (cp >> 6)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000) {
                out.push_back((char)(0xE0 | (cp >> 12)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
            else {
                out.push_back((char)(0xF0 | (cp >> 18)));
                out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
        }

        // ======================================================================
        // XML (subconjunto): elementos, atributos, texto, comentarios, CDATA,
        // entidades. Sem namespaces, DOCTYPE nem instrucoes alem de <?xml ... ?>.
        // ======================================================================
        struct XmlAttribute {
            std::string Name, Value;
            int Line = 0, Column = 0;
        };

        struct XmlElement {
            std::string Name;
            int Line = 0, Column = 0;
            std::vector<XmlAttribute> Attributes;
            std::string Text;                 // texto direto, ja com espacos colapsados e entidades decodificadas
            int TextLine = 0, TextColumn = 0; // onde comecou o primeiro texto nao vazio
            std::vector<XmlElement> Children;
        };

        struct XmlError {
            int Line, Column;
            std::string Message;
        };

        class XmlReader {
        public:
            explicit XmlReader(const std::string& text) : m_Text(text) {
                if (m_Text.size() >= 3 && (unsigned char)m_Text[0] == 0xEF && (unsigned char)m_Text[1] == 0xBB && (unsigned char)m_Text[2] == 0xBF)
                    m_Pos = 3; // BOM UTF-8
            }

            XmlElement ParseRoot() {
                SkipMisc(true);
                if (AtEnd())
                    Fail("o documento esta vazio (esperava <UI>).");
                if (Peek() != '<')
                    Fail("esperava '<' no inicio do documento.");
                XmlElement root = ParseElement(0);
                SkipMisc(false);
                if (!AtEnd())
                    Fail("ha conteudo depois do elemento raiz </" + root.Name + ">; um .prismui tem um unico <UI>.");
                return root;
            }

        private:
            [[noreturn]] void Fail(const std::string& message) const { throw XmlError{ m_Line, m_Column, message }; }
            [[noreturn]] void FailAt(int line, int column, const std::string& message) const { throw XmlError{ line, column, message }; }

            bool AtEnd() const { return m_Pos >= m_Text.size(); }
            char Peek(size_t ahead = 0) const { return m_Pos + ahead < m_Text.size() ? m_Text[m_Pos + ahead] : '\0'; }
            bool StartsWith(const char* literal) const {
                for (size_t i = 0; literal[i]; i++)
                    if (Peek(i) != literal[i] || m_Pos + i >= m_Text.size())
                        return false;
                return true;
            }
            char Get() {
                char c = m_Text[m_Pos++];
                if (c == '\n') { m_Line++; m_Column = 1; }
                else if (((unsigned char)c & 0xC0) != 0x80) m_Column++; // byte de continuacao UTF-8 nao conta coluna
                return c;
            }
            void Skip(size_t count) { for (size_t i = 0; i < count && !AtEnd(); i++) Get(); }

            static bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
            static bool IsNameStart(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
            static bool IsNameChar(char c) { return IsNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ':'; }

            void SkipSpaces() { while (!AtEnd() && IsSpace(Peek())) Get(); }

            void SkipComment() {
                int line = m_Line, column = m_Column;
                Skip(4); // <!--
                while (!AtEnd() && !StartsWith("-->"))
                    Get();
                if (AtEnd())
                    FailAt(line, column, "comentario <!-- nunca fechado (falta -->).");
                Skip(3);
            }

            // Espacos, comentarios e (so antes da raiz) a declaracao <?xml ... ?>.
            void SkipMisc(bool allowDeclaration) {
                while (true) {
                    SkipSpaces();
                    if (StartsWith("<!--")) {
                        SkipComment();
                    }
                    else if (allowDeclaration && StartsWith("<?")) {
                        int line = m_Line, column = m_Column;
                        while (!AtEnd() && !StartsWith("?>"))
                            Get();
                        if (AtEnd())
                            FailAt(line, column, "declaracao <? nunca fechada (falta ?>).");
                        Skip(2);
                    }
                    else if (StartsWith("<!DOCTYPE") || StartsWith("<!doctype")) {
                        Fail("<!DOCTYPE nao e suportado em .prismui.");
                    }
                    else {
                        return;
                    }
                }
            }

            std::string ParseName(const char* what) {
                if (AtEnd() || !IsNameStart(Peek()))
                    Fail(std::string("esperava ") + what + ".");
                std::string name;
                while (!AtEnd() && IsNameChar(Peek()))
                    name.push_back(Get());
                return name;
            }

            // '&' ja consumido. Devolve o texto decodificado (UTF-8).
            std::string ParseEntity() {
                int line = m_Line, column = m_Column - 1;
                std::string body;
                while (!AtEnd() && Peek() != ';' && body.size() < 12 && Peek() != '<' && !IsSpace(Peek()))
                    body.push_back(Get());
                if (AtEnd() || Peek() != ';')
                    FailAt(line, column, "'&' solto: use &amp; para um '&' literal (ou termine a entidade com ';').");
                Get(); // ;
                if (body == "lt") return "<";
                if (body == "gt") return ">";
                if (body == "amp") return "&";
                if (body == "quot") return "\"";
                if (body == "apos") return "'";
                if (!body.empty() && body[0] == '#') {
                    uint32_t cp = 0;
                    bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
                    size_t start = hex ? 2 : 1;
                    if (start >= body.size())
                        FailAt(line, column, "entidade numerica vazia (&#" + body.substr(1) + ";).");
                    for (size_t i = start; i < body.size(); i++) {
                        char c = body[i];
                        int digit = (c >= '0' && c <= '9') ? c - '0'
                            : (hex && c >= 'a' && c <= 'f') ? c - 'a' + 10
                            : (hex && c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                        if (digit < 0)
                            FailAt(line, column, "entidade numerica invalida (&" + body + ";).");
                        cp = cp * (hex ? 16u : 10u) + (uint32_t)digit;
                        if (cp > 0x10FFFF)
                            FailAt(line, column, "entidade numerica fora do intervalo (&" + body + ";).");
                    }
                    if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF))
                        FailAt(line, column, "entidade numerica invalida (&" + body + ";).");
                    std::string out;
                    AppendUtf8(out, cp);
                    return out;
                }
                FailAt(line, column, "entidade desconhecida '&" + body + ";' (suportadas: &lt; &gt; &amp; &quot; &apos; &#N; &#xH;).");
            }

            std::string ParseAttributeValue() {
                char quote = Peek();
                if (quote != '"' && quote != '\'')
                    Fail("o valor do atributo precisa estar entre aspas (\"...\" ou '...').");
                int line = m_Line, column = m_Column;
                Get();
                std::string value;
                while (true) {
                    if (AtEnd())
                        FailAt(line, column, "aspas do atributo nunca fechadas.");
                    char c = Peek();
                    if (c == quote) { Get(); break; }
                    if (c == '<')
                        Fail("'<' nao pode aparecer dentro de um valor de atributo (use &lt;).");
                    Get();
                    if (c == '&') value += ParseEntity();
                    else value.push_back(c);
                }
                return value;
            }

            XmlElement ParseElement(int depth) {
                if (depth > kMaxDepth)
                    Fail("elementos aninhados fundo demais (limite " + std::to_string(kMaxDepth) + ").");
                XmlElement element;
                element.Line = m_Line;
                element.Column = m_Column;
                Get(); // <
                element.Name = ParseName("o nome do elemento depois de '<'");

                // Atributos.
                while (true) {
                    bool hadSpace = !AtEnd() && IsSpace(Peek());
                    SkipSpaces();
                    if (AtEnd())
                        FailAt(element.Line, element.Column, "o elemento <" + element.Name + "> nunca foi fechado.");
                    if (Peek() == '/' || Peek() == '>')
                        break;
                    if (!hadSpace)
                        Fail("esperava espaco, '>' ou '/>' em <" + element.Name + ">.");
                    XmlAttribute attribute;
                    attribute.Line = m_Line;
                    attribute.Column = m_Column;
                    attribute.Name = ParseName("o nome de um atributo");
                    SkipSpaces();
                    if (Peek() != '=')
                        Fail("esperava '=' depois do atributo '" + attribute.Name + "'.");
                    Get();
                    SkipSpaces();
                    attribute.Value = ParseAttributeValue();
                    for (const XmlAttribute& other : element.Attributes)
                        if (other.Name == attribute.Name)
                            FailAt(attribute.Line, attribute.Column, "atributo '" + attribute.Name + "' repetido em <" + element.Name + ">.");
                    element.Attributes.push_back(std::move(attribute));
                }

                if (Peek() == '/') {
                    Get();
                    if (Peek() != '>')
                        Fail("esperava '>' depois de '/'.");
                    Get();
                    return element;
                }
                Get(); // >

                // Conteudo.
                bool pendingSpace = false;
                while (true) {
                    if (AtEnd())
                        FailAt(element.Line, element.Column, "o elemento <" + element.Name + "> nunca foi fechado (falta </" + element.Name + ">).");
                    if (StartsWith("<!--")) {
                        SkipComment();
                    }
                    else if (StartsWith("<![CDATA[")) {
                        int line = m_Line, column = m_Column;
                        Skip(9);
                        std::string raw;
                        while (!AtEnd() && !StartsWith("]]>"))
                            raw.push_back(Get());
                        if (AtEnd())
                            FailAt(line, column, "CDATA nunca fechado (falta ]]>).");
                        Skip(3);
                        if (!raw.empty() && element.TextLine == 0) { element.TextLine = line; element.TextColumn = column; }
                        if (pendingSpace && !element.Text.empty()) element.Text.push_back(' ');
                        pendingSpace = false;
                        element.Text += raw;
                    }
                    else if (StartsWith("</")) {
                        int line = m_Line, column = m_Column;
                        Skip(2);
                        std::string closing = ParseName("o nome do elemento depois de '</'");
                        SkipSpaces();
                        if (Peek() != '>')
                            Fail("esperava '>' para fechar </" + closing + ">.");
                        Get();
                        if (closing != element.Name)
                            FailAt(line, column, "esperava </" + element.Name + "> (elemento aberto na linha " + std::to_string(element.Line)
                                + ") mas achou </" + closing + ">.");
                        return element;
                    }
                    else if (Peek() == '<') {
                        if (!IsNameStart(Peek(1)))
                            Fail("'<' inesperado (use &lt; para um '<' literal).");
                        element.Children.push_back(ParseElement(depth + 1));
                    }
                    else {
                        char c = Get();
                        if (IsSpace(c)) {
                            pendingSpace = true;
                        }
                        else {
                            if (element.TextLine == 0) { element.TextLine = m_Line; element.TextColumn = m_Column - 1; }
                            if (pendingSpace && !element.Text.empty()) element.Text.push_back(' ');
                            pendingSpace = false;
                            if (c == '&') element.Text += ParseEntity();
                            else element.Text.push_back(c);
                        }
                    }
                }
            }

            const std::string& m_Text;
            size_t m_Pos = 0;
            int m_Line = 1, m_Column = 1;
        };

        // ======================================================================
        // Construcao do Document a partir da arvore XML.
        // ======================================================================
        enum TypeMask : unsigned {
            kPanel = 1u << 0, kText = 1u << 1, kImage = 1u << 2, kButton = 1u << 3, kSpacer = 1u << 4,
            kAll = 0x1Fu
        };

        unsigned MaskOf(NodeType type) {
            switch (type) {
                case NodeType::Panel: return kPanel;
                case NodeType::Text: return kText;
                case NodeType::Image: return kImage;
                case NodeType::Button: return kButton;
                case NodeType::Spacer: return kSpacer;
            }
            return 0;
        }

        bool IsValidId(const std::string& id) {
            if (id.empty() || !((id[0] >= 'A' && id[0] <= 'Z') || (id[0] >= 'a' && id[0] <= 'z') || id[0] == '_'))
                return false;
            for (char c : id)
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
                    return false;
            return true;
        }

        bool IsValidHandlerName(const std::string& name) {
            if (name.empty() || !((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
                return false;
            for (char c : name)
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                    return false;
            return true;
        }

        class Builder {
        public:
            Builder(ParseResult& result) : m_Result(result) {}

            void BuildRoot(const XmlElement& root) {
                if (root.Name != "UI") {
                    Error(root.Line, root.Column, "o elemento raiz precisa ser <UI>, nao <" + root.Name + ">.");
                    return;
                }
                BuildRootAttributes(root);
                if (!root.Text.empty())
                    Warn(root.TextLine, root.TextColumn, "texto solto dentro de <UI> foi ignorado (texto vai dentro de <Text> ou <Button>).");
                for (const XmlElement& child : root.Children)
                    BuildNode(child, -1, 0);
                if (m_Result.Doc.Nodes.empty())
                    Warn(root.Line, root.Column, "o documento nao tem nenhum elemento.");
            }

        private:
            void Warn(int line, int column, const std::string& message) {
                m_Result.Diagnostics.push_back({ Diagnostic::Severity::Warning, line, column, message });
            }
            void Error(int line, int column, const std::string& message) {
                m_Result.Diagnostics.push_back({ Diagnostic::Severity::Error, line, column, message });
            }

            void BuildRootAttributes(const XmlElement& root) {
                static const std::vector<std::string> kNames = { "scale", "reference" };
                for (const XmlAttribute& attribute : root.Attributes) {
                    const std::string value = Normalize(Trim(attribute.Value));
                    if (attribute.Name == "scale") {
                        if (value == "none") m_Result.Doc.Scale = ScaleMode::None;
                        else if (value == "height") m_Result.Doc.Scale = ScaleMode::Height;
                        else if (value == "width") m_Result.Doc.Scale = ScaleMode::Width;
                        else if (value == "min") m_Result.Doc.Scale = ScaleMode::Min;
                        else Warn(attribute.Line, attribute.Column, "scale='" + attribute.Value + "' invalido (use none, height, width ou min); usando none.");
                    }
                    else if (attribute.Name == "reference") {
                        std::string text = Trim(attribute.Value);
                        size_t x = text.find_first_of("xX");
                        float w = 0, h = 0;
                        size_t posW = 0, posH = 0;
                        std::string left = x == std::string::npos ? "" : Trim(text.substr(0, x));
                        std::string right = x == std::string::npos ? "" : Trim(text.substr(x + 1));
                        if (x != std::string::npos && ParseFloatPrefix(left, posW, w) && posW == left.size()
                            && ParseFloatPrefix(right, posH, h) && posH == right.size() && w >= 1.0f && h >= 1.0f) {
                            m_Result.Doc.ReferenceWidth = w;
                            m_Result.Doc.ReferenceHeight = h;
                        }
                        else {
                            Warn(attribute.Line, attribute.Column, "reference='" + attribute.Value + "' invalido (use LARGURAxALTURA, ex: 1920x1080); usando 1920x1080.");
                        }
                    }
                    else {
                        Warn(attribute.Line, attribute.Column, "atributo '" + attribute.Name + "' desconhecido em <UI>" + Suggest(attribute.Name, kNames) + ".");
                    }
                }
            }

            void BuildNode(const XmlElement& element, int parentIndex, int depth) {
                (void)depth;
                static const std::vector<std::string> kElements = { "Panel", "Row", "Column", "Text", "Image", "Button", "Spacer" };

                Node node;
                if (element.Name == "Panel") node.Type = NodeType::Panel;
                else if (element.Name == "Row") { node.Type = NodeType::Panel; node.Layout = LayoutMode::Row; }
                else if (element.Name == "Column") { node.Type = NodeType::Panel; node.Layout = LayoutMode::Column; }
                else if (element.Name == "Text") node.Type = NodeType::Text;
                else if (element.Name == "Image") node.Type = NodeType::Image;
                else if (element.Name == "Button") node.Type = NodeType::Button;
                else if (element.Name == "Spacer") node.Type = NodeType::Spacer;
                else {
                    Warn(element.Line, element.Column, "elemento <" + element.Name + "> desconhecido foi ignorado" + Suggest(element.Name, kElements) + ".");
                    return;
                }
                if (m_Result.Doc.Nodes.size() >= kMaxNodes) {
                    if (!m_ReportedLimit) {
                        Error(element.Line, element.Column, "elementos demais (limite " + std::to_string(kMaxNodes) + ").");
                        m_ReportedLimit = true;
                    }
                    return;
                }

                ApplyTypeDefaults(node);
                ApplyAttributes(node, element);

                // Texto: conteudo do elemento (ou o atributo text, aplicado la).
                if (!element.Text.empty()) {
                    if (node.Type == NodeType::Text || node.Type == NodeType::Button)
                        node.Text = element.Text;
                    else
                        Warn(element.TextLine, element.TextColumn, "texto solto dentro de <" + element.Name + "> foi ignorado.");
                }

                const int index = (int)m_Result.Doc.Nodes.size();
                node.Parent = parentIndex;
                node.ScaledFontSize = node.FontSize;
                m_Result.Doc.Nodes.push_back(std::move(node));
                if (parentIndex >= 0)
                    m_Result.Doc.Nodes[(size_t)parentIndex].Children.push_back(index);
                else
                    m_Result.Doc.Roots.push_back(index);

                const Node& stored = m_Result.Doc.Nodes[(size_t)index];
                if (!stored.Id.empty()) {
                    if (m_Result.Doc.ById.find(stored.Id) == m_Result.Doc.ById.end())
                        m_Result.Doc.ById[stored.Id] = index;
                    else
                        Warn(element.Line, element.Column, "id '" + stored.Id + "' repetido; so o primeiro pode ser achado por script.");
                }

                if (!element.Children.empty() && stored.Type != NodeType::Panel) {
                    Warn(element.Line, element.Column, "<" + element.Name + "> nao pode ter filhos; eles foram ignorados.");
                    return;
                }
                for (const XmlElement& child : element.Children)
                    BuildNode(child, index, depth + 1);
            }

            static void ApplyTypeDefaults(Node& node) {
                switch (node.Type) {
                    case NodeType::Panel:
                        node.Tint = Color(0, 0, 0, 0); // contêiner transparente
                        break;
                    case NodeType::Text:
                        node.BlocksPointer = false;
                        break;
                    case NodeType::Image:
                        break;
                    case NodeType::Button:
                        node.Tint = Color(0.25f, 0.25f, 0.30f, 1.0f);
                        node.Padding = { 16.0f, 8.0f, 16.0f, 8.0f };
                        node.Align_ = TextAlign::Center;
                        break;
                    case NodeType::Spacer:
                        node.Tint = Color(0, 0, 0, 0);
                        node.BlocksPointer = false;
                        break;
                }
            }

            void ApplyAttributes(Node& node, const XmlElement& element) {
                // Atributos validos por tipo (nome -> tipos em que faz sentido).
                static const std::map<std::string, unsigned> kApplicable = {
                    { "id", kAll }, { "visible", kAll }, { "anchor", kAll }, { "width", kAll }, { "height", kAll }, { "margin", kAll },
                    { "color", kAll },
                    { "padding", kPanel | kButton }, { "gap", kPanel }, { "layout", kPanel }, { "align", kPanel }, { "justify", kPanel },
                    { "src", kPanel | kImage | kButton },
                    { "text", kText | kButton }, { "size", kText | kButton }, { "textAlign", kText | kButton }, { "wrap", kText | kButton },
                    { "textColor", kButton }, { "hoverColor", kButton }, { "pressedColor", kButton },
                    { "interactable", kButton }, { "onClick", kButton },
                    { "blocksPointer", kPanel | kImage | kButton },
                };
                static const std::vector<std::string> kNames = [] {
                    std::vector<std::string> names;
                    for (const auto& [name, mask] : kApplicable) { (void)mask; names.push_back(name); }
                    return names;
                }();

                bool widthSet = false, heightSet = false, blocksSet = false, stretch = false;
                const unsigned myMask = MaskOf(node.Type);

                for (const XmlAttribute& attribute : element.Attributes) {
                    auto found = kApplicable.find(attribute.Name);
                    if (found == kApplicable.end()) {
                        Warn(attribute.Line, attribute.Column, "atributo '" + attribute.Name + "' desconhecido em <" + element.Name + ">"
                            + Suggest(attribute.Name, kNames) + ".");
                        continue;
                    }
                    if ((found->second & myMask) == 0) {
                        Warn(attribute.Line, attribute.Column, "o atributo '" + attribute.Name + "' nao se aplica a <" + element.Name + "> e foi ignorado.");
                        continue;
                    }
                    const std::string value = Trim(attribute.Value);
                    auto bad = [&](const char* expected) {
                        Warn(attribute.Line, attribute.Column, std::string(attribute.Name) + "='" + attribute.Value + "' invalido (" + expected + "); usando o padrao.");
                    };
                    const std::string& name = attribute.Name;

                    if (name == "id") {
                        if (IsValidId(value)) node.Id = value;
                        else bad("use letras, numeros, '_', '-' ou '.', comecando por letra ou '_'");
                    }
                    else if (name == "visible") { bool b; if (ParseBool(value, b)) node.Visible = b; else bad("true ou false"); }
                    else if (name == "anchor") {
                        const std::string key = Normalize(value);
                        static const std::map<std::string, Anchor> kAnchors = {
                            { "topleft", Anchor::TopLeft }, { "top", Anchor::Top }, { "topright", Anchor::TopRight },
                            { "left", Anchor::Left }, { "center", Anchor::Center }, { "right", Anchor::Right },
                            { "bottomleft", Anchor::BottomLeft }, { "bottom", Anchor::Bottom }, { "bottomright", Anchor::BottomRight } };
                        auto anchorIt = kAnchors.find(key);
                        if (anchorIt != kAnchors.end()) node.Anchoring = anchorIt->second;
                        else if (key == "stretch" || key == "fill") stretch = true;
                        else bad("top-left, top, top-right, left, center, right, bottom-left, bottom, bottom-right ou stretch");
                    }
                    else if (name == "width") { Length l; if (ParseLength(value, l)) { node.Width = l; widthSet = true; } else bad("auto, fill, 2fr, 50%, 120 ou 120px"); }
                    else if (name == "height") { Length l; if (ParseLength(value, l)) { node.Height = l; heightSet = true; } else bad("auto, fill, 2fr, 50%, 120 ou 120px"); }
                    else if (name == "margin") { Insets i; if (ParseInsets(value, i)) node.Margin = i; else bad("1, 2 ou 4 numeros, ex: 8 | 8 16 | 8 16 8 16"); }
                    else if (name == "padding") { Insets i; if (ParseInsets(value, i)) node.Padding = i; else bad("1, 2 ou 4 numeros, ex: 8 | 8 16 | 8 16 8 16"); }
                    else if (name == "gap") { float f; if (ParseFloatStrict(value, f) && f >= 0.0f) node.Gap = f; else bad("numero >= 0"); }
                    else if (name == "layout") {
                        const std::string key = Normalize(value);
                        if (key == "stack") node.Layout = LayoutMode::Stack;
                        else if (key == "row") node.Layout = LayoutMode::Row;
                        else if (key == "column") node.Layout = LayoutMode::Column;
                        else bad("stack, row ou column");
                    }
                    else if (name == "align") {
                        const std::string key = Normalize(value);
                        if (key == "start") node.AlignItems = Align::Start;
                        else if (key == "center") node.AlignItems = Align::Center;
                        else if (key == "end") node.AlignItems = Align::End;
                        else if (key == "stretch") node.AlignItems = Align::Stretch;
                        else bad("start, center, end ou stretch");
                    }
                    else if (name == "justify") {
                        const std::string key = Normalize(value);
                        if (key == "start") node.JustifyContent = Justify::Start;
                        else if (key == "center") node.JustifyContent = Justify::Center;
                        else if (key == "end") node.JustifyContent = Justify::End;
                        else if (key == "spacebetween") node.JustifyContent = Justify::SpaceBetween;
                        else bad("start, center, end ou space-between");
                    }
                    else if (name == "color") { Color c; if (ParseColor(value, c)) node.Tint = c; else bad("#RGB, #RRGGBB, #RRGGBBAA ou um nome como white"); }
                    else if (name == "textColor") { Color c; if (ParseColor(value, c)) node.TextColor = c; else bad("#RGB, #RRGGBB, #RRGGBBAA ou um nome como white"); }
                    else if (name == "hoverColor") { Color c; if (ParseColor(value, c)) { node.HoverColor = c; node.HasHoverColor = true; } else bad("#RRGGBB ou #RRGGBBAA"); }
                    else if (name == "pressedColor") { Color c; if (ParseColor(value, c)) { node.PressedColor = c; node.HasPressedColor = true; } else bad("#RRGGBB ou #RRGGBBAA"); }
                    else if (name == "src") node.Src = value;
                    else if (name == "text") node.Text = attribute.Value;
                    else if (name == "size") { float f; if (ParseFloatStrict(value, f) && f > 0.0f && f <= 1000.0f) { node.FontSize = f; } else bad("numero entre 0 e 1000"); }
                    else if (name == "textAlign") {
                        const std::string key = Normalize(value);
                        if (key == "left" || key == "start") node.Align_ = TextAlign::Left;
                        else if (key == "center") node.Align_ = TextAlign::Center;
                        else if (key == "right" || key == "end") node.Align_ = TextAlign::Right;
                        else bad("left, center ou right");
                    }
                    else if (name == "wrap") { bool b; if (ParseBool(value, b)) node.Wrap = b; else bad("true ou false"); }
                    else if (name == "interactable") { bool b; if (ParseBool(value, b)) node.Interactable = b; else bad("true ou false"); }
                    else if (name == "blocksPointer") { bool b; if (ParseBool(value, b)) { node.BlocksPointer = b; blocksSet = true; } else bad("true ou false"); }
                    else if (name == "onClick") {
                        if (IsValidHandlerName(value)) node.OnClick = value;
                        else bad("nome de funcao: letras, numeros e '_'");
                    }
                }

                if (stretch) {
                    if (!widthSet) node.Width = Length::Fill();
                    if (!heightSet) node.Height = Length::Fill();
                    node.Anchoring = Anchor::TopLeft;
                }
                // Um Panel so barra o mouse se for visivel (cor ou textura): um contêiner transparente
                // de tela cheia nao pode engolir os cliques do jogo.
                if (node.Type == NodeType::Panel && !blocksSet)
                    node.BlocksPointer = node.Tint.A > 0.0f || !node.Src.empty();
            }

            static bool ParseBool(const std::string& text, bool& out) {
                const std::string key = Lower(text);
                if (key == "true" || key == "1" || key == "yes" || key == "sim") { out = true; return true; }
                if (key == "false" || key == "0" || key == "no" || key == "nao") { out = false; return true; }
                return false;
            }

            ParseResult& m_Result;
            bool m_ReportedLimit = false;
        };

    } // namespace

    // ==========================================================================
    // API publica
    // ==========================================================================
    bool ParseFloatStrict(const std::string& text, float& out) {
        const std::string trimmed = Trim(text);
        size_t pos = 0;
        float value = 0;
        if (!ParseFloatPrefix(trimmed, pos, value) || pos != trimmed.size())
            return false;
        out = value;
        return true;
    }

    bool ParseLength(const std::string& text, Length& out) {
        const std::string value = Lower(Trim(text));
        if (value.empty())
            return false;
        if (value == "auto") { out = Length::Auto(); return true; }
        if (value == "fill") { out = Length::Fill(1.0f); return true; }
        size_t pos = 0;
        float number = 0;
        if (!ParseFloatPrefix(value, pos, number))
            return false;
        const std::string suffix = value.substr(pos);
        if (suffix.empty() || suffix == "px") {
            if (number < 0.0f) return false;
            out = Length::Px(number);
            return true;
        }
        if (suffix == "%") {
            if (number < 0.0f) return false;
            out = Length::Percent(number);
            return true;
        }
        if (suffix == "fr") {
            if (number <= 0.0f) return false;
            out = Length::Fill(number);
            return true;
        }
        return false;
    }

    bool ParseInsets(const std::string& text, Insets& out) {
        std::vector<float> numbers;
        const std::string value = Trim(text);
        size_t i = 0;
        while (i < value.size()) {
            while (i < value.size() && (value[i] == ' ' || value[i] == ',' || value[i] == '\t'))
                i++;
            if (i >= value.size())
                break;
            float number = 0;
            if (!ParseFloatPrefix(value, i, number))
                return false;
            // Aceita "8px".
            if (i + 1 < value.size() && value[i] == 'p' && value[i + 1] == 'x')
                i += 2;
            numbers.push_back(number);
        }
        if (numbers.empty() || numbers.size() == 3 || numbers.size() > 4)
            return false;
        for (float n : numbers)
            if (n < 0.0f)
                return false;
        if (numbers.size() == 1) out = { numbers[0], numbers[0], numbers[0], numbers[0] };
        else if (numbers.size() == 2) out = { numbers[1], numbers[0], numbers[1], numbers[0] };       // vertical horizontal
        else out = { numbers[3], numbers[0], numbers[1], numbers[2] };                                 // cima direita baixo esquerda (CSS)
        return true;
    }

    bool ParseColor(const std::string& text, Color& out) {
        const std::string value = Trim(text);
        if (value.empty())
            return false;
        if (value[0] == '#') {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const std::string digits = value.substr(1);
            for (char c : digits)
                if (hex(c) < 0)
                    return false;
            float channel[4] = { 0, 0, 0, 1 };
            if (digits.size() == 3 || digits.size() == 4) {
                for (size_t k = 0; k < digits.size(); k++)
                    channel[k] = (float)(hex(digits[k]) * 17) / 255.0f;
            }
            else if (digits.size() == 6 || digits.size() == 8) {
                for (size_t k = 0; k < digits.size() / 2; k++)
                    channel[k] = (float)(hex(digits[k * 2]) * 16 + hex(digits[k * 2 + 1])) / 255.0f;
            }
            else {
                return false;
            }
            out = Color(channel[0], channel[1], channel[2], channel[3]);
            return true;
        }
        static const std::map<std::string, Color> kNamed = {
            { "white", Color(1, 1, 1, 1) }, { "black", Color(0, 0, 0, 1) }, { "red", Color(1, 0, 0, 1) },
            { "green", Color(0, 0.5f, 0, 1) }, { "lime", Color(0, 1, 0, 1) }, { "blue", Color(0, 0, 1, 1) },
            { "yellow", Color(1, 1, 0, 1) }, { "orange", Color(1, 0.647f, 0, 1) }, { "gray", Color(0.5f, 0.5f, 0.5f, 1) },
            { "grey", Color(0.5f, 0.5f, 0.5f, 1) }, { "cyan", Color(0, 1, 1, 1) }, { "magenta", Color(1, 0, 1, 1) },
            { "transparent", Color(0, 0, 0, 0) },
        };
        auto it = kNamed.find(Lower(value));
        if (it == kNamed.end())
            return false;
        out = it->second;
        return true;
    }

    std::string FormatDiagnostic(const std::string& fileName, const Diagnostic& diagnostic) {
        std::string text = fileName;
        if (diagnostic.Line > 0)
            text += ":" + std::to_string(diagnostic.Line) + ":" + std::to_string(std::max(diagnostic.Column, 1));
        text += diagnostic.Level == Diagnostic::Severity::Error ? ": error: " : ": warning: ";
        text += diagnostic.Message;
        return text;
    }

    ParseResult ParseDocument(const std::string& text) {
        ParseResult result;
        try {
            XmlReader reader(text);
            XmlElement root = reader.ParseRoot();
            Builder builder(result);
            builder.BuildRoot(root);
        }
        catch (const XmlError& error) {
            result.Diagnostics.push_back({ Diagnostic::Severity::Error, error.Line, error.Column, error.Message });
        }
        result.Ok = !result.HasErrors();
        if (!result.Ok)
            result.Doc = Document{};
        return result;
    }

}
