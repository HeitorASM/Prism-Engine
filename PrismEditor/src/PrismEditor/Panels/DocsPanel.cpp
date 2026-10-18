#include "DocsPanel.h"
#include <Prism.h>
#include <Prism/Core/Paths.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace PrismEditor {

    namespace {

        std::string ToLower(const std::string& text) {
            std::string result = text;
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return result;
        }

        std::string Trim(const std::string& text) {
            size_t first = text.find_first_not_of(" \t\r");
            if (first == std::string::npos)
                return std::string();
            size_t last = text.find_last_not_of(" \t\r");
            return text.substr(first, last - first + 1);
        }

        // A fonte padrao do ImGui cobre ASCII + Latin-1. Pontuacao tipografica
        // comum nos docs vira um equivalente ASCII; o que sobrar vira '?'.
        void AppendCodepoint(std::string& out, uint32_t cp) {
            if (cp < 0x80) { out += (char)cp; return; }
            if (cp <= 0xFF) { // Latin-1: reemite em UTF-8
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
                return;
            }
            switch (cp) {
                case 0x2013: case 0x2014: case 0x2212: out += '-'; break;
                case 0x2192: out += "->"; break;
                case 0x2190: out += "<-"; break;
                case 0x2194: out += "<->"; break;
                case 0x2026: out += "..."; break;
                case 0x201C: case 0x201D: out += '"'; break;
                case 0x2018: case 0x2019: out += '\''; break;
                case 0x2022: out += '*'; break;
                case 0x2248: out += '~'; break;
                case 0x2265: out += ">="; break;
                case 0x2264: out += "<="; break;
                case 0x2713: case 0x2714: out += 'v'; break;
                case 0x2717: case 0x2718: out += 'x'; break;
                default: out += '?'; break;
            }
        }

        std::string ToDisplayText(const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size();) {
                unsigned char c = (unsigned char)s[i++];
                if (c < 0x80) { out += (char)c; continue; }
                int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
                if (extra < 0) { out += '?'; continue; }
                uint32_t cp = c & (0x3F >> extra);
                bool valid = true;
                for (int k = 0; k < extra; k++) {
                    if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) { valid = false; break; }
                    cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
                }
                if (valid) AppendCodepoint(out, cp);
                else out += '?';
            }
            return out;
        }

        // Remove formatacao em linha: [texto](url) -> texto (registrando o link),
        // `codigo` -> codigo, **negrito** -> negrito, \| -> |.
        std::string StripInline(const std::string& raw, std::vector<std::pair<std::string, std::string>>* links) {
            std::string out;
            for (size_t i = 0; i < raw.size();) {
                char c = raw[i];
                if (c == '[') {
                    size_t close = raw.find("](", i);
                    size_t end = close == std::string::npos ? std::string::npos : raw.find(')', close);
                    if (close != std::string::npos && end != std::string::npos) {
                        std::string label = StripInline(raw.substr(i + 1, close - i - 1), nullptr);
                        std::string url = raw.substr(close + 2, end - close - 2);
                        if (links)
                            links->emplace_back(label, url);
                        out += label;
                        i = end + 1;
                        continue;
                    }
                }
                if (c == '`' ) { i++; continue; }
                if (c == '*' && i + 1 < raw.size() && raw[i + 1] == '*') { i += 2; continue; }
                if (c == '\\' && i + 1 < raw.size() && raw[i + 1] == '|') { out += '|'; i += 2; continue; }
                out += c;
                i++;
            }
            return ToDisplayText(out);
        }

        bool IsInternalDocLink(const std::string& url, std::string& fileName) {
            if (url.find("://") != std::string::npos)
                return false;
            std::string path = url.substr(0, url.find('#'));
            if (path.size() < 3 || path.substr(path.size() - 3) != ".md")
                return false;
            fileName = std::filesystem::path(path).filename().string();
            return true;
        }

        std::filesystem::path FindDocsDirectory() {
            std::vector<std::filesystem::path> candidates;
#ifdef PRISM_DOCS_SOURCE_DIR
            candidates.emplace_back(PRISM_DOCS_SOURCE_DIR);
#endif
            std::filesystem::path exeDir = Prism::GetExecutableDirectory();
            if (!exeDir.empty())
                candidates.push_back(exeDir / "docs");
            std::error_code cwdError;
            std::filesystem::path cwd = std::filesystem::current_path(cwdError);
            if (!cwdError)
                candidates.push_back(cwd / "docs");

            for (const auto& dir : candidates) {
                std::error_code ec;
                if (!std::filesystem::is_directory(dir, ec))
                    continue;
                for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
                    if (entry.path().extension() == ".md")
                        return dir;
            }
            return {};
        }

        bool IsTableSeparator(const std::vector<std::string>& cells) {
            if (cells.empty())
                return false;
            for (const auto& cell : cells) {
                std::string t = Trim(cell);
                if (t.empty() || t.find_first_not_of("-: ") != std::string::npos)
                    return false;
            }
            return true;
        }

        std::vector<std::string> SplitTableRow(const std::string& line) {
            std::string t = Trim(line);
            if (!t.empty() && t.front() == '|') t.erase(t.begin());
            if (!t.empty() && t.back() == '|' && !(t.size() >= 2 && t[t.size() - 2] == '\\')) t.pop_back();
            std::vector<std::string> cells;
            std::string current;
            for (size_t i = 0; i < t.size(); i++) {
                if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') { current += "\\|"; i++; continue; }
                if (t[i] == '`') { // '|' dentro de `codigo` nao separa celulas
                    size_t end = t.find('`', i + 1);
                    if (end != std::string::npos) { current += t.substr(i, end - i + 1); i = end; continue; }
                }
                if (t[i] == '|') { cells.push_back(current); current.clear(); continue; }
                current += t[i];
            }
            cells.push_back(current);
            return cells;
        }

    } // namespace

    // ------------------------------------------------------------------------
    // Carga e parsing
    // ------------------------------------------------------------------------

    void DocsPanel::ParseMarkdown(const std::string& text, Doc& doc) {
        using Type = Block::Type;
        std::istringstream stream(text);
        std::string line;

        bool inCode = false;
        Block code;
        Block paragraph;
        bool paragraphOpen = false;
        bool bulletOpen = false; // a ultima coisa emitida foi um item de lista (aceita continuacao indentada)
        bool tableOpen = false;
        std::string paragraphRaw;

        auto flushParagraph = [&]() {
            if (!paragraphOpen)
                return;
            paragraph.Kind = Type::Paragraph;
            paragraph.Links.clear();
            paragraph.Text = StripInline(paragraphRaw, &paragraph.Links);
            doc.Blocks.push_back(paragraph);
            paragraph = Block{};
            paragraphRaw.clear();
            paragraphOpen = false;
        };

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::string trimmed = Trim(line);

            if (inCode) {
                if (trimmed.rfind("```", 0) == 0) {
                    inCode = false;
                    doc.Blocks.push_back(code);
                    code = Block{};
                }
                else {
                    code.Lines.push_back(ToDisplayText(line));
                }
                continue;
            }
            if (trimmed.rfind("```", 0) == 0) {
                flushParagraph();
                inCode = true;
                code = Block{};
                code.Kind = Type::Code;
                bulletOpen = tableOpen = false;
                continue;
            }
            if (trimmed.empty()) {
                flushParagraph();
                bulletOpen = tableOpen = false;
                continue;
            }

            // Tabela
            if (trimmed.front() == '|') {
                flushParagraph();
                std::vector<std::string> cells = SplitTableRow(trimmed);
                if (IsTableSeparator(cells))
                    continue;
                if (!tableOpen) {
                    Block table;
                    table.Kind = Type::Table;
                    doc.Blocks.push_back(table);
                    tableOpen = true;
                }
                std::vector<std::string> cleaned;
                for (const auto& cell : cells)
                    cleaned.push_back(StripInline(Trim(cell), &doc.Blocks.back().Links));
                doc.Blocks.back().Rows.push_back(std::move(cleaned));
                bulletOpen = false;
                continue;
            }
            tableOpen = false;

            // Titulo
            if (trimmed.front() == '#') {
                size_t level = trimmed.find_first_not_of('#');
                if (level != std::string::npos && level <= 6 && trimmed[level] == ' ') {
                    flushParagraph();
                    Block heading;
                    heading.Kind = Type::Heading;
                    heading.Level = (int)level;
                    heading.Text = StripInline(Trim(trimmed.substr(level)), nullptr);
                    if (level == 1 && doc.Title.empty())
                        doc.Title = heading.Text;
                    doc.Blocks.push_back(heading);
                    bulletOpen = false;
                    continue;
                }
            }

            // Separador
            if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
                flushParagraph();
                Block rule;
                rule.Kind = Type::Rule;
                doc.Blocks.push_back(rule);
                bulletOpen = false;
                continue;
            }

            // Citacao
            if (trimmed.front() == '>') {
                flushParagraph();
                Block quote;
                quote.Kind = Type::Quote;
                quote.Text = StripInline(Trim(trimmed.substr(1)), &quote.Links);
                doc.Blocks.push_back(quote);
                bulletOpen = false;
                continue;
            }

            // Item de lista (- * + ou 1.)
            size_t indent = line.find_first_not_of(' ');
            std::string marker;
            size_t contentStart = std::string::npos;
            if (trimmed.size() > 2 && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ') {
                marker = "-";
                contentStart = 2;
            }
            else {
                size_t digits = 0;
                while (digits < trimmed.size() && std::isdigit((unsigned char)trimmed[digits])) digits++;
                if (digits > 0 && digits + 1 < trimmed.size() && trimmed[digits] == '.' && trimmed[digits + 1] == ' ') {
                    marker = trimmed.substr(0, digits + 1);
                    contentStart = digits + 2;
                }
            }
            if (contentStart != std::string::npos) {
                flushParagraph();
                Block bullet;
                bullet.Kind = Type::Bullet;
                bullet.Level = (int)(indent == std::string::npos ? 0 : indent / 2);
                bullet.Marker = marker;
                bullet.Text = StripInline(Trim(trimmed.substr(contentStart)), &bullet.Links);
                doc.Blocks.push_back(bullet);
                bulletOpen = true;
                continue;
            }

            // Continuacao de item de lista (linha indentada logo apos o item)
            if (bulletOpen && indent != std::string::npos && indent >= 2) {
                Block& last = doc.Blocks.back();
                last.Text += " " + StripInline(trimmed, &last.Links);
                continue;
            }

            // Paragrafo
            bulletOpen = false;
            if (paragraphOpen)
                paragraphRaw += " ";
            paragraphRaw += trimmed;
            paragraphOpen = true;
        }
        if (inCode)
            doc.Blocks.push_back(code);
        flushParagraph();

        // Texto minusculo (busca) por bloco e por documento.
        for (Block& block : doc.Blocks) {
            std::string all = block.Text;
            for (const auto& l : block.Lines) all += "\n" + l;
            for (const auto& row : block.Rows) for (const auto& cell : row) all += " " + cell;
            block.Lower = ToLower(all);
            doc.Lower += block.Lower + "\n";
        }
    }

    void DocsPanel::LoadDocs() {
        std::string previous = (m_Current >= 0 && m_Current < (int)m_Docs.size()) ? m_Docs[m_Current].FileName : std::string();
        m_Docs.clear();
        m_History.clear();
        m_Current = -1;
        m_Directory = FindDocsDirectory();
        m_Loaded = true;
        if (m_Directory.empty()) {
            PRISM_CORE_WARN("Documentacao: pasta 'docs' nao encontrada (procurada ao lado do codigo-fonte, do executavel e no diretorio de trabalho).");
            return;
        }

        // Pasta da fonte atual. Sem docs/inengine, cai para os docs do GitHub.
        std::filesystem::path directory = m_Directory;
        std::error_code ec;
        if (m_Source == Source::InEngine) {
            std::filesystem::path inEngine = m_Directory / "inengine";
            if (std::filesystem::is_directory(inEngine, ec))
                directory = inEngine;
            else
                m_Source = Source::GitHub;
        }

        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
            if (entry.is_regular_file() && entry.path().extension() == ".md")
                files.push_back(entry.path());

        // Docs da engine: ordem alfabetica do arquivo (00-visao-geral.md primeiro).
        // Docs do GitHub: ordem de leitura sugerida abaixo; o que nao estiver na
        // lista vem depois, em ordem alfabetica.
        static const char* kOrder[] = { "arquitetura.md", "editor.md", "renderizacao.md", "fisica.md", "audio.md",
                                        "scripting-lua.md", "scripting-cpp.md", "ui.md", "formatos-de-arquivo.md", "roadmap.md" };
        auto rank = [&](const std::filesystem::path& p) {
            if (m_Source == Source::InEngine)
                return 0;
            std::string name = p.filename().string();
            for (size_t i = 0; i < std::size(kOrder); i++)
                if (name == kOrder[i]) return (int)i;
            return (int)std::size(kOrder);
        };
        std::sort(files.begin(), files.end(), [&](const auto& a, const auto& b) {
            int ra = rank(a), rb = rank(b);
            return ra != rb ? ra < rb : a.filename() < b.filename();
        });

        for (const auto& path : files) {
            std::ifstream file(path, std::ios::binary);
            std::stringstream buffer;
            buffer << file.rdbuf();
            Doc doc;
            doc.FileName = path.filename().string();
            ParseMarkdown(buffer.str(), doc);
            if (doc.Title.empty())
                doc.Title = path.stem().string();
            m_Docs.push_back(std::move(doc));
        }

        int restore = previous.empty() ? -1 : FindDoc(previous);
        if (restore < 0 && !m_Docs.empty())
            restore = 0;
        if (restore >= 0)
            SelectDoc(restore, false);
    }

    int DocsPanel::FindDoc(const std::string& fileName) const {
        for (int i = 0; i < (int)m_Docs.size(); i++)
            if (m_Docs[i].FileName == fileName)
                return i;
        return -1;
    }

    void DocsPanel::SelectDoc(int index, bool pushHistory) {
        if (index < 0 || index >= (int)m_Docs.size())
            return;
        if (pushHistory && m_Current >= 0 && m_Current != index)
            m_History.push_back(m_Current);
        m_Current = index;
        m_ScrollToTop = true;
    }

    void DocsPanel::SetSource(Source source) {
        if (m_Source == source)
            return;
        m_Source = source;
        m_Search[0] = '\0';
        m_Current = -1; // outra lista: nao tenta restaurar o doc anterior
        m_Docs.clear();
        LoadDocs();
    }

    void DocsPanel::Open(const std::string& fileName) {
        m_Open = true;
        m_FocusRequested = true;
        if (!fileName.empty())
            m_Source = Source::InEngine; // pedidos por nome (menu Ajuda) sao da documentacao da engine
        LoadDocs(); // relê do disco: reflete edicoes feitas no Editor de Codigo
        if (!fileName.empty()) {
            int index = FindDoc(fileName);
            if (index >= 0)
                SelectDoc(index, false);
        }
    }

    // ------------------------------------------------------------------------
    // Interface
    // ------------------------------------------------------------------------

    void DocsPanel::OnImGuiRender() {
        if (!m_Open)
            return;

        if (m_FocusRequested) {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Documentacao###DocsPanel", &m_Open)) {
            ImGui::End();
            return;
        }

        if (m_Docs.empty()) {
            ImGui::TextWrapped("Nenhum documento encontrado. A pasta 'docs' (arquivos .md) precisa estar ao lado do codigo-fonte ou do executavel do editor.");
            if (ImGui::Button("Tentar de novo"))
                LoadDocs();
            ImGui::End();
            return;
        }

        RenderSidebar();
        ImGui::SameLine();
        RenderContent();
        ImGui::End();
    }

    void DocsPanel::RenderSidebar() {
        ImGui::BeginChild("##DocsSidebar", ImVec2(210.0f, 0.0f), true);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##DocsSearch", "Buscar...", m_Search, sizeof(m_Search));
        std::string query = ToLower(Trim(m_Search));

        ImGui::TextDisabled(m_Source == Source::InEngine ? "Documentacao da engine" : "Docs do GitHub (tecnicos)");
        ImGui::BeginDisabled(m_History.empty());
        if (ImGui::SmallButton("< Voltar")) {
            int back = m_History.back();
            m_History.pop_back();
            SelectDoc(back, false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Recarregar"))
            LoadDocs();
        ImGui::Separator();

        for (int i = 0; i < (int)m_Docs.size(); i++) {
            const Doc& doc = m_Docs[i];
            if (!query.empty() && doc.Lower.find(query) == std::string::npos)
                continue;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(doc.Title.c_str(), i == m_Current))
                SelectDoc(i, true);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", doc.FileName.c_str());
            ImGui::PopID();
        }

        // Alterna entre a documentacao da engine e a do GitHub (pequeno, no fim da lista).
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        if (m_Source == Source::InEngine) {
            if (ImGui::SmallButton("Ver docs do GitHub"))
                SetSource(Source::GitHub);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Documentacao tecnica do repositorio (pasta docs/).");
        }
        else {
            if (ImGui::SmallButton("< Docs da engine"))
                SetSource(Source::InEngine);
        }
        ImGui::EndChild();
    }

    void DocsPanel::RenderContent() {
        ImGui::BeginChild("##DocsContent", ImVec2(0.0f, 0.0f), false);
        if (m_ScrollToTop) {
            ImGui::SetScrollY(0.0f);
            m_ScrollToTop = false;
        }

        if (m_Current >= 0 && m_Current < (int)m_Docs.size()) {
            const Doc& doc = m_Docs[m_Current];
            std::string query = ToLower(Trim(m_Search));
            ImGui::PushTextWrapPos(0.0f); // quebra de linha na borda da area de conteudo
            for (int i = 0; i < (int)doc.Blocks.size(); i++) {
                const Block& block = doc.Blocks[i];
                bool highlighted = !query.empty() && block.Lower.find(query) != std::string::npos;
                RenderBlock(block, i, highlighted);
            }
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0.0f, 24.0f));
        }
        ImGui::EndChild();
    }

    void DocsPanel::RenderBlock(const Block& block, int id, bool highlighted) {
        using Type = Block::Type;
        const ImVec4 accent(0.55f, 0.75f, 1.0f, 1.0f);
        const ImVec4 highlightColor(1.0f, 0.9f, 0.45f, 1.0f);
        ImGui::PushID(id);

        switch (block.Kind) {
            case Type::Heading: {
                ImGui::Dummy(ImVec2(0.0f, block.Level <= 2 ? 10.0f : 4.0f));
                ImVec4 color = block.Level == 1 ? ImVec4(1.0f, 0.85f, 0.5f, 1.0f) : block.Level == 2 ? accent : ImVec4(0.8f, 0.9f, 1.0f, 1.0f);
                if (highlighted) color = highlightColor;
                // Nivel 1-2 em MAIUSCULAS para destacar (a fonte padrao nao tem negrito/tamanhos).
                std::string text = block.Text;
                if (block.Level <= 2)
                    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)((c < 0x80) ? std::toupper(c) : c); });
                ImGui::TextColored(color, "%s", text.c_str());
                if (block.Level <= 2)
                    ImGui::Separator();
                break;
            }
            case Type::Paragraph:
                if (highlighted) ImGui::PushStyleColor(ImGuiCol_Text, highlightColor);
                ImGui::TextUnformatted(block.Text.c_str());
                if (highlighted) ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                break;
            case Type::Bullet: {
                float indent = 14.0f + 16.0f * (float)block.Level;
                ImGui::Indent(indent);
                if (block.Marker == "-") {
                    ImGui::Bullet();
                }
                else {
                    ImGui::TextDisabled("%s", block.Marker.c_str());
                    ImGui::SameLine();
                }
                if (highlighted) ImGui::PushStyleColor(ImGuiCol_Text, highlightColor);
                ImGui::TextUnformatted(block.Text.c_str());
                if (highlighted) ImGui::PopStyleColor();
                ImGui::Unindent(indent);
                break;
            }
            case Type::Quote:
                ImGui::Indent(12.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, highlighted ? highlightColor : ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::TextUnformatted(block.Text.c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent(12.0f);
                break;
            case Type::Rule:
                ImGui::Separator();
                break;
            case Type::Code: {
                float lineHeight = ImGui::GetTextLineHeightWithSpacing();
                float height = std::min(lineHeight * (float)block.Lines.size() + 14.0f, 420.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
                ImGui::BeginChild("##code", ImVec2(0.0f, height), true, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::PushTextWrapPos(-1.0f); // sem quebra: codigo rola na horizontal
                for (const std::string& codeLine : block.Lines)
                    ImGui::TextColored(highlighted ? highlightColor : ImVec4(0.82f, 0.88f, 0.82f, 1.0f), "%s", codeLine.empty() ? " " : codeLine.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                break;
            }
            case Type::Table: {
                if (block.Rows.empty())
                    break;
                int columns = 0;
                for (const auto& row : block.Rows)
                    columns = std::max(columns, (int)row.size());
                if (columns <= 0)
                    break;
                if (ImGui::BeginTable("##table", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                    for (int c = 0; c < columns; c++) {
                        const std::string& header = c < (int)block.Rows[0].size() ? block.Rows[0][c] : std::string();
                        ImGui::TableSetupColumn(header.c_str());
                    }
                    ImGui::TableHeadersRow();
                    for (size_t r = 1; r < block.Rows.size(); r++) {
                        ImGui::TableNextRow();
                        for (int c = 0; c < columns; c++) {
                            ImGui::TableSetColumnIndex(c);
                            if (c < (int)block.Rows[r].size()) {
                                ImGui::PushTextWrapPos(0.0f); // dentro da celula
                                ImGui::TextUnformatted(block.Rows[r][c].c_str());
                                ImGui::PopTextWrapPos();
                            }
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                break;
            }
        }

        // Links para outros documentos: botoes logo abaixo do bloco.
        if (!block.Links.empty()) {
            std::vector<std::string> shown;
            for (const auto& link : block.Links) {
                std::string fileName;
                if (IsInternalDocLink(link.second, fileName)) {
                    int target = FindDoc(fileName);
                    if (target < 0 || std::find(shown.begin(), shown.end(), fileName) != shown.end())
                        continue;
                    shown.push_back(fileName);
                    ImGui::PushStyleColor(ImGuiCol_Text, accent);
                    std::string label = "-> " + link.first + "##link" + fileName;
                    if (ImGui::SmallButton(label.c_str()))
                        SelectDoc(target, true);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                }
                else if (link.second.find("://") != std::string::npos) {
                    if (std::find(shown.begin(), shown.end(), link.second) != shown.end())
                        continue;
                    shown.push_back(link.second);
                    std::string label = "copiar link: " + link.first + "##ext" + link.second;
                    if (ImGui::SmallButton(label.c_str()))
                        ImGui::SetClipboardText(link.second.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", link.second.c_str());
                    ImGui::SameLine();
                }
            }
            if (!shown.empty()) {
                ImGui::NewLine();
            }
        }
        ImGui::PopID();
    }

}
