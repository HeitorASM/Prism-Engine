#include "Resource.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

// Formato texto do recurso (ver o comentario em Resource.h para a gramatica).

namespace Prism {

    namespace {

        bool IsIdentStart(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
        bool IsIdentChar(char c) { return IsIdentStart(c) || (c >= '0' && c <= '9') || c == '.' || c == '-'; }

        bool IsValidIdentifier(const std::string& s) {
            if (s.empty() || !IsIdentStart(s[0]))
                return false;
            for (char c : s)
                if (!IsIdentChar(c))
                    return false;
            return true;
        }

        // ------------------------------------------------------------------
        // Escritor
        // ------------------------------------------------------------------
        void AppendQuoted(std::string& out, const std::string& s) {
            out.push_back('"');
            for (unsigned char c : s) {
                switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20 || c == 0x7F) {
                        static const char* hex = "0123456789abcdef";
                        out += "\\u00";
                        out.push_back(hex[c >> 4]);
                        out.push_back(hex[c & 15]);
                    }
                    else {
                        out.push_back((char)c); // UTF-8 passa direto
                    }
                }
            }
            out.push_back('"');
        }

        void AppendKey(std::string& out, const std::string& key) {
            if (IsValidIdentifier(key))
                out += key;
            else
                AppendQuoted(out, key);
        }

        void AppendDouble(std::string& out, double d) {
            if (std::isnan(d)) { out += "nan"; return; }
            if (std::isinf(d)) { out += d < 0 ? "-inf" : "inf"; return; }
            char buffer[64];
            auto result = std::to_chars(buffer, buffer + sizeof(buffer), d); // menor texto que volta ao MESMO valor
            std::string text(buffer, result.ptr);
            if (text.find_first_of(".eE") == std::string::npos)
                text += ".0"; // decimal nunca volta como inteiro
            out += text;
        }

        // Tenta escrever numa linha so: listas de escalares/listas, curtas. Mapas/objetos nunca.
        bool TryInline(const ResourceValue& v, std::string& out, size_t budget) {
            switch (v.GetKind()) {
            case ResourceValue::Kind::Map:
            case ResourceValue::Kind::Object:
                return false;
            case ResourceValue::Kind::Array: {
                out.push_back('[');
                bool first = true;
                for (const auto& item : v.Items()) {
                    if (!first) out += ", ";
                    first = false;
                    if (!TryInline(item, out, budget) || out.size() > budget)
                        return false;
                }
                out.push_back(']');
                return out.size() <= budget;
            }
            case ResourceValue::Kind::Null: out += "null"; return true;
            case ResourceValue::Kind::Bool: out += v.AsBool() ? "true" : "false"; return true;
            case ResourceValue::Kind::Int: out += std::to_string(v.AsInt()); return true;
            case ResourceValue::Kind::Float: AppendDouble(out, v.AsDouble()); return true;
            case ResourceValue::Kind::String: AppendQuoted(out, v.AsString()); return out.size() <= budget;
            case ResourceValue::Kind::AssetRef: {
                out += "@asset(" + v.AsAssetID().ToString();
                if (!v.PathHint().empty()) {
                    out += ", ";
                    AppendQuoted(out, v.PathHint());
                }
                out += ")";
                return out.size() <= budget;
            }
            }
            return false;
        }

        void WriteValue(std::string& out, const ResourceValue& v, int indent) {
            std::string inlineText;
            if (TryInline(v, inlineText, 100)) {
                out += inlineText;
                return;
            }
            const std::string pad(indent * 4, ' ');
            const std::string padInner((indent + 1) * 4, ' ');
            switch (v.GetKind()) {
            case ResourceValue::Kind::Array:
                out += "[\n";
                for (size_t i = 0; i < v.Items().size(); i++) {
                    out += padInner;
                    WriteValue(out, v.Items()[i], indent + 1);
                    if (i + 1 < v.Items().size()) out.push_back(',');
                    out.push_back('\n');
                }
                out += pad + "]";
                break;
            case ResourceValue::Kind::Map:
            case ResourceValue::Kind::Object:
                if (v.GetKind() == ResourceValue::Kind::Object) {
                    std::string name = v.TypeName();
                    for (char& c : name)
                        if (!IsIdentChar(c)) c = '_';
                    if (name.empty() || !IsIdentStart(name[0]))
                        name.insert(name.begin(), '_'); // nome vazio/invalido vira um identificador valido
                    out += name + " ";
                }
                if (v.Fields().empty()) {
                    out += "{}";
                    break;
                }
                out += "{\n";
                for (const auto& field : v.Fields()) {
                    out += padInner;
                    AppendKey(out, field.first);
                    out += " = ";
                    WriteValue(out, field.second, indent + 1);
                    out.push_back('\n');
                }
                out += pad + "}";
                break;
            default: // escalares longos (texto enorme): uma linha so
                TryInline(v, out, std::string::npos);
                break;
            }
        }

        // ------------------------------------------------------------------
        // Leitor
        // ------------------------------------------------------------------
        class TextParser {
        public:
            explicit TextParser(std::string_view text) : m_Text(text) {}

            bool ParseDocument(ResourceDocument& out) {
                if (m_Text.size() >= 3 && (unsigned char)m_Text[0] == 0xEF && (unsigned char)m_Text[1] == 0xBB && (unsigned char)m_Text[2] == 0xBF)
                    m_Pos = 3; // BOM UTF-8
                SkipWhitespace();
                std::string word;
                if (!ReadIdentifier(word) || word != "prism_resource")
                    return Fail("o arquivo precisa comecar com 'prism_resource type=\"...\" version=N'");

                ResourceDocument doc;
                bool hasType = false;
                // Atributos do cabecalho: na MESMA linha.
                for (;;) {
                    SkipInlineSpaces();
                    if (AtEnd() || Peek() == '\n' || Peek() == '\r' || Peek() == '#')
                        break;
                    std::string name;
                    if (!ReadIdentifier(name))
                        return Fail("atributo do cabecalho invalido");
                    SkipInlineSpaces();
                    if (Peek() != '=')
                        return Fail("esperado '=' depois de '" + name + "' no cabecalho");
                    m_Pos++;
                    SkipInlineSpaces();
                    ResourceValue value;
                    if (!ParseValue(value, 0))
                        return false;
                    if (name == "type") {
                        if (value.GetKind() != ResourceValue::Kind::String || value.AsString().empty())
                            return Fail("'type' precisa ser um texto nao vazio");
                        doc.Type = value.AsString();
                        hasType = true;
                    }
                    else if (name == "version") {
                        const int64_t version = value.AsInt(-1);
                        if (value.GetKind() != ResourceValue::Kind::Int || version < 0 || version > 0xFFFFFFFFll)
                            return Fail("'version' precisa ser um inteiro entre 0 e 4294967295");
                        doc.Version = (uint32_t)version;
                    }
                    // outros atributos: ignorados (compatibilidade para frente)
                }
                if (!hasType)
                    return Fail("cabecalho sem type=\"...\"");

                if (!ParseFields(doc.Root, /*untilBrace*/ false, 0))
                    return false;
                out = std::move(doc);
                return true;
            }

            const std::string& Error() const { return m_Error; }

        private:
            std::string_view m_Text;
            size_t m_Pos = 0;
            std::string m_Error;

            bool AtEnd() const { return m_Pos >= m_Text.size(); }
            char Peek() const { return AtEnd() ? '\0' : m_Text[m_Pos]; }

            bool Fail(const std::string& message) {
                if (m_Error.empty()) {
                    size_t line = 1;
                    const size_t end = std::min(m_Pos, m_Text.size());
                    for (size_t i = 0; i < end; i++)
                        if (m_Text[i] == '\n')
                            line++;
                    m_Error = "linha " + std::to_string(line) + ": " + message;
                }
                return false;
            }

            void SkipInlineSpaces() {
                while (!AtEnd() && (Peek() == ' ' || Peek() == '\t'))
                    m_Pos++;
            }

            // Espacos, quebras de linha e comentarios ('#' ate o fim da linha).
            void SkipWhitespace() {
                while (!AtEnd()) {
                    const char c = Peek();
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                        m_Pos++;
                    else if (c == '#') {
                        while (!AtEnd() && Peek() != '\n')
                            m_Pos++;
                    }
                    else
                        break;
                }
            }

            bool ReadIdentifier(std::string& out) {
                if (AtEnd() || !IsIdentStart(Peek()))
                    return false;
                const size_t start = m_Pos;
                while (!AtEnd() && IsIdentChar(Peek()))
                    m_Pos++;
                out.assign(m_Text.substr(start, m_Pos - start));
                return true;
            }

            bool ReadKey(std::string& out) {
                if (Peek() == '"') {
                    ResourceValue text;
                    if (!ParseString(text))
                        return false;
                    out = text.AsString();
                    return true;
                }
                if (!ReadIdentifier(out))
                    return Fail("esperado o nome de uma propriedade");
                return true;
            }

            // Sequencia de "chave = valor". No topo do arquivo vai ate o fim; dentro de '{' ate o '}'.
            bool ParseFields(ResourceValue& target, bool untilBrace, int depth) {
                std::unordered_map<std::string, size_t> index; // evita O(n^2) com chaves repetidas (a ultima vence)
                for (;;) {
                    SkipWhitespace();
                    if (AtEnd()) {
                        if (untilBrace)
                            return Fail("'{' sem o '}' correspondente");
                        return true;
                    }
                    if (Peek() == '}') {
                        if (!untilBrace)
                            return Fail("'}' sem '{' correspondente");
                        m_Pos++;
                        return true;
                    }
                    if (Peek() == ',') { // virgula opcional entre propriedades
                        m_Pos++;
                        continue;
                    }
                    std::string key;
                    if (!ReadKey(key))
                        return false;
                    SkipWhitespace();
                    if (Peek() != '=')
                        return Fail("esperado '=' depois de '" + key + "'");
                    m_Pos++;
                    SkipWhitespace();
                    ResourceValue value;
                    if (!ParseValue(value, depth))
                        return false;
                    if (index.count(key)) {
                        target.Set(key, std::move(value)); // repetida: a ultima vence, na posicao da primeira
                    }
                    else {
                        index.emplace(key, target.Fields().size());
                        target.AppendField(std::move(key), std::move(value));
                    }
                }
            }

            bool ParseValue(ResourceValue& out, int depth) {
                if (depth > ResourceIO::kMaxDepth)
                    return Fail("aninhamento profundo demais");
                if (AtEnd())
                    return Fail("valor esperado, mas o arquivo acabou");
                const char c = Peek();
                if (c == '"')
                    return ParseString(out);
                if (c == '[')
                    return ParseArray(out, depth);
                if (c == '{') {
                    m_Pos++;
                    out = ResourceValue::MakeMap();
                    return ParseFields(out, true, depth + 1);
                }
                if (c == '@')
                    return ParseAssetRef(out);
                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.')
                    return ParseNumber(out);
                if (IsIdentStart(c)) {
                    std::string word;
                    ReadIdentifier(word);
                    if (word == "true") { out = ResourceValue::MakeBool(true); return true; }
                    if (word == "false") { out = ResourceValue::MakeBool(false); return true; }
                    if (word == "null") { out = ResourceValue::MakeNull(); return true; }
                    if (word == "inf") { out = ResourceValue::MakeFloat(INFINITY); return true; }
                    if (word == "nan") { out = ResourceValue::MakeFloat(std::nan("")); return true; }
                    // Tipo { ... }: objeto tipado
                    const size_t afterWord = m_Pos;
                    SkipWhitespace();
                    if (Peek() == '{') {
                        m_Pos++;
                        out = ResourceValue::MakeObject(std::move(word));
                        return ParseFields(out, true, depth + 1);
                    }
                    m_Pos = afterWord; // o erro aponta para a palavra, nao para o que vem depois
                    return Fail("valor inesperado '" + word + "' (use aspas para textos)");
                }
                return Fail(std::string("caractere inesperado '") + c + "'");
            }

            bool ParseArray(ResourceValue& out, int depth) {
                m_Pos++; // '['
                out = ResourceValue::MakeArray();
                for (;;) {
                    SkipWhitespace();
                    if (AtEnd())
                        return Fail("'[' sem o ']' correspondente");
                    if (Peek() == ']') {
                        m_Pos++;
                        return true;
                    }
                    if (Peek() == ',') {
                        m_Pos++;
                        continue;
                    }
                    ResourceValue item;
                    if (!ParseValue(item, depth + 1))
                        return false;
                    out.Add(std::move(item));
                }
            }

            static void AppendUtf8(std::string& out, uint32_t cp) {
                if (cp < 0x80) out.push_back((char)cp);
                else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
                else { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
            }

            bool ParseString(ResourceValue& out) {
                m_Pos++; // aspas de abertura
                std::string text;
                for (;;) {
                    if (AtEnd())
                        return Fail("texto sem as aspas de fechamento");
                    const char c = m_Text[m_Pos++];
                    if (c == '"')
                        break;
                    if (c == '\n' || c == '\r')
                        return Fail("quebra de linha dentro de um texto (use \\n)");
                    if (c != '\\') {
                        text.push_back(c);
                        continue;
                    }
                    if (AtEnd())
                        return Fail("escape incompleto no fim do texto");
                    const char e = m_Text[m_Pos++];
                    switch (e) {
                    case '"': text.push_back('"'); break;
                    case '\\': text.push_back('\\'); break;
                    case 'n': text.push_back('\n'); break;
                    case 'r': text.push_back('\r'); break;
                    case 't': text.push_back('\t'); break;
                    case 'u': {
                        if (m_Text.size() - m_Pos < 4)
                            return Fail("\\u precisa de 4 digitos hexadecimais");
                        uint32_t cp = 0;
                        for (int i = 0; i < 4; i++) {
                            const char h = m_Text[m_Pos + i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (uint32_t)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (uint32_t)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (uint32_t)(h - 'A' + 10);
                            else return Fail("\\u precisa de 4 digitos hexadecimais");
                        }
                        m_Pos += 4;
                        if (cp >= 0xD800 && cp <= 0xDFFF)
                            return Fail("\\u com par substituto (surrogate) nao e suportado; escreva o caractere direto em UTF-8");
                        AppendUtf8(text, cp);
                        break;
                    }
                    default:
                        return Fail(std::string("escape desconhecido '\\") + e + "'");
                    }
                }
                out = ResourceValue::MakeString(std::move(text));
                return true;
            }

            bool ParseNumber(ResourceValue& out) {
                const size_t start = m_Pos;
                bool negative = false;
                if (Peek() == '+' || Peek() == '-') {
                    negative = Peek() == '-';
                    m_Pos++;
                }
                if (!AtEnd() && IsIdentStart(Peek())) { // -inf
                    std::string word;
                    ReadIdentifier(word);
                    if (word == "inf") { out = ResourceValue::MakeFloat(negative ? -INFINITY : INFINITY); return true; }
                    m_Pos = start;
                    return Fail("numero invalido");
                }
                const size_t digitsStart = m_Pos;
                bool isFloat = false;
                while (!AtEnd()) {
                    const char c = Peek();
                    if ((c >= '0' && c <= '9')) { m_Pos++; continue; }
                    if (c == '.' || c == 'e' || c == 'E') { isFloat = true; m_Pos++; continue; }
                    if ((c == '+' || c == '-') && m_Pos > digitsStart && (m_Text[m_Pos - 1] == 'e' || m_Text[m_Pos - 1] == 'E')) { m_Pos++; continue; }
                    break;
                }
                if (m_Pos == digitsStart)
                    return Fail("numero invalido");
                const char* first = m_Text.data() + digitsStart;
                const char* last = m_Text.data() + m_Pos;
                if (isFloat) {
                    double d = 0.0;
                    auto result = std::from_chars(first, last, d);
                    if (result.ec != std::errc() || result.ptr != last)
                        return Fail("numero decimal invalido '" + std::string(m_Text.substr(start, m_Pos - start)) + "'");
                    out = ResourceValue::MakeFloat(negative ? -d : d);
                }
                else {
                    // Le sem sinal e aplica o sinal depois: aceita exatamente INT64_MIN..INT64_MAX.
                    uint64_t magnitude = 0;
                    auto result = std::from_chars(first, last, magnitude);
                    if (result.ec != std::errc() || result.ptr != last)
                        return Fail("inteiro invalido ou grande demais '" + std::string(m_Text.substr(start, m_Pos - start)) + "'");
                    if (negative ? magnitude > (uint64_t)INT64_MAX + 1 : magnitude > (uint64_t)INT64_MAX)
                        return Fail("inteiro grande demais");
                    out = ResourceValue::MakeInt(negative ? (int64_t)(0 - magnitude) : (int64_t)magnitude);
                }
                return true;
            }

            bool ParseAssetRef(ResourceValue& out) {
                static const char kPrefix[] = "@asset(";
                if (m_Text.substr(m_Pos, sizeof(kPrefix) - 1) != kPrefix)
                    return Fail("esperado @asset(<id>[, \"caminho\"])");
                m_Pos += sizeof(kPrefix) - 1;
                SkipInlineSpaces();
                const size_t start = m_Pos;
                while (!AtEnd() && std::isxdigit((unsigned char)Peek()))
                    m_Pos++;
                AssetID id;
                if (!AssetID::TryParse(std::string(m_Text.substr(start, m_Pos - start)), id))
                    return Fail("ID de asset invalido (sao 16 digitos hexadecimais, diferente de zero)");
                SkipWhitespace();
                std::string hint;
                if (Peek() == ',') {
                    m_Pos++;
                    SkipWhitespace();
                    if (Peek() != '"')
                        return Fail("esperado o caminho entre aspas");
                    ResourceValue text;
                    if (!ParseString(text))
                        return false;
                    hint = text.AsString();
                    SkipWhitespace();
                }
                if (Peek() != ')')
                    return Fail("esperado ')' no fim de @asset(...)");
                m_Pos++;
                out = ResourceValue::MakeAssetRef(id, std::move(hint));
                return true;
            }
        };

    }

    namespace ResourceIO {

        std::string WriteText(const ResourceDocument& doc) {
            std::string out = "prism_resource type=";
            AppendQuoted(out, doc.Type);
            out += " version=" + std::to_string(doc.Version) + "\n";
            if (!doc.Root.Fields().empty())
                out.push_back('\n');
            for (const auto& field : doc.Root.Fields()) {
                AppendKey(out, field.first);
                out += " = ";
                WriteValue(out, field.second, 0);
                out.push_back('\n');
            }
            return out;
        }

        bool ParseText(std::string_view text, ResourceDocument& out, std::string* error) {
            TextParser parser(text);
            ResourceDocument doc;
            if (!parser.ParseDocument(doc)) {
                if (error) *error = parser.Error();
                return false;
            }
            out = std::move(doc);
            return true;
        }

    }

}
