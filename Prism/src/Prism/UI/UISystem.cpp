#include "UISystem.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Renderer/Renderer.h"
#include "../Scripting/ScriptEngine.h"
#include "../Core/Input.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"
#include "../Project/Project.h"
#include "UIMarkup.h"
#include "UILayout.h"

#include <glad/gl.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Prism {

    bool UISystem::s_PointerOverUI = false;

    namespace {

        struct Rect {
            float X = 0, Y = 0, W = 0, H = 0;
            bool Contains(float px, float py) const { return px >= X && py >= Y && px < X + W && py < Y + H; }
        };

        // --- Fonte ----------------------------------------------------------
        // A fonte e um .ttf lido do disco (Prism/fonts/DejaVuSans.ttf, copiado ao
        // lado do executavel) e rasterizada UMA vez num atlas R8: Latin-1
        // (codepoints 32..255), 16 colunas x 14 linhas, celulas de tamanho fixo.
        constexpr int kFirstCodepoint = 32;
        constexpr int kLastCodepoint = 255;
        constexpr int kGlyphCount = kLastCodepoint - kFirstCodepoint + 1; // 224
        constexpr int kColumns = 16;
        constexpr int kRows = 14;
        constexpr float kEmSize = 40.0f; // tamanho de rasterizacao (px); o texto e escalado a partir dele

        struct FontAtlas {
            int CellWidth = 0, CellHeight = 0;
            int Baseline = 0;   // linha de base dentro da celula, em px do atlas
            int PadX = 0;       // margem esquerda dentro da celula
            float AscentRatio = 0.95f; // ascendente / em
            float Advance[kGlyphCount] = {}; // avanco horizontal, em px do atlas (0 = sem glifo)
        };

        FontAtlas s_Font;
        uint32_t s_FontTexture = 0;
        bool s_FontFailed = false;

        // Procura DejaVuSans.ttf: pasta do codigo-fonte (CMake), ao lado do
        // executavel e no diretorio de trabalho (mesma ordem dos shaders).
        std::filesystem::path FindFontFile() {
            const char* kName = "DejaVuSans.ttf";
            std::vector<std::filesystem::path> candidates;
#ifdef PRISM_FONT_SOURCE_DIR
            candidates.emplace_back(std::filesystem::path(PRISM_FONT_SOURCE_DIR) / kName);
#endif
            std::filesystem::path exeDir = GetExecutableDirectory();
            if (!exeDir.empty())
                candidates.push_back(exeDir / "fonts" / kName);
            std::error_code cwdError;
            std::filesystem::path cwd = std::filesystem::current_path(cwdError);
            if (!cwdError)
                candidates.push_back(cwd / "fonts" / kName);

            for (const auto& path : candidates) {
                std::error_code ec;
                if (std::filesystem::is_regular_file(path, ec))
                    return path;
            }
            std::string tried;
            for (const auto& path : candidates)
                tried += "\n  " + path.generic_string();
            PRISM_CORE_ERROR("UISystem: fonte nao encontrada (procurado:", tried, "\n). O texto da UI nao sera desenhado - confira se a pasta 'fonts' foi copiada junto do executavel.");
            return {};
        }

        void EnsureFont() {
            if (s_FontTexture != 0 || s_FontFailed)
                return;
            s_FontFailed = true; // so volta a false se tudo der certo (evita repetir erro/leitura a cada frame)

            std::filesystem::path path = FindFontFile();
            if (path.empty())
                return;
            std::ifstream file(path, std::ios::binary);
            std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            stbtt_fontinfo info;
            if (data.empty() || !stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) {
                PRISM_CORE_ERROR("UISystem: falha ao ler a fonte '", path.generic_string(), "'.");
                return;
            }

            const float scale = stbtt_ScaleForMappingEmToPixels(&info, kEmSize);
            int ascent = 0, descent = 0, lineGap = 0;
            stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

            s_Font = FontAtlas{};
            s_Font.PadX = (int)std::ceil(kEmSize * 0.15f);
            s_Font.CellWidth = (int)std::ceil(kEmSize * 1.3f);
            s_Font.Baseline = (int)std::ceil(ascent * scale) + 2;
            s_Font.CellHeight = s_Font.Baseline + (int)std::ceil(-descent * scale) + 3;
            s_Font.AscentRatio = ascent * scale / kEmSize;

            const int atlasWidth = kColumns * s_Font.CellWidth;
            const int atlasHeight = kRows * s_Font.CellHeight;
            std::vector<unsigned char> pixels((size_t)atlasWidth * atlasHeight, 0);

            for (int i = 0; i < kGlyphCount; i++) {
                int codepoint = kFirstCodepoint + i;
                int glyph = stbtt_FindGlyphIndex(&info, codepoint);
                if (glyph == 0)
                    continue; // sem glifo na fonte: avanco 0, nada desenhado
                int advance = 0, bearing = 0;
                stbtt_GetGlyphHMetrics(&info, glyph, &advance, &bearing);
                s_Font.Advance[i] = advance * scale;

                int x0, y0, x1, y1;
                stbtt_GetGlyphBitmapBox(&info, glyph, scale, scale, &x0, &y0, &x1, &y1);
                int w = x1 - x0, h = y1 - y0;
                if (w <= 0 || h <= 0)
                    continue; // espaco etc.
                int cellX = (i % kColumns) * s_Font.CellWidth;
                int cellY = (i / kColumns) * s_Font.CellHeight;
                int localX = s_Font.PadX + x0;
                int localY = s_Font.Baseline + y0;
                if (localX < 0 || localY < 0 || localX + w > s_Font.CellWidth || localY + h > s_Font.CellHeight)
                    continue; // nao cabe na celula (nao deve acontecer com esta fonte)
                stbtt_MakeGlyphBitmap(&info, pixels.data() + (size_t)(cellY + localY) * atlasWidth + (cellX + localX),
                                      w, h, atlasWidth, scale, scale, glyph);
            }

            GLint previousAlignment = 4;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glCreateTextures(GL_TEXTURE_2D, 1, &s_FontTexture);
            glTextureStorage2D(s_FontTexture, 1, GL_R8, atlasWidth, atlasHeight);
            glTextureSubImage2D(s_FontTexture, 0, 0, 0, atlasWidth, atlasHeight, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
            glTextureParameteri(s_FontTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(s_FontTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(s_FontTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(s_FontTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
            s_FontFailed = false;
        }

        // --- Quads ----------------------------------------------------------
        struct Batcher {
            std::vector<Renderer::UIQuad> Quads;
            const Texture2D* Texture = nullptr;
            float Width = 0, Height = 0;

            void Flush() {
                if (!Quads.empty())
                    Renderer::DrawUIQuads(Quads.data(), (uint32_t)Quads.size(), Width, Height, Texture, s_FontTexture);
                Quads.clear();
            }
            void Solid(const Rect& r, const glm::vec4& color) {
                Quads.push_back({ { r.X, r.Y, r.W, r.H }, { 0, 0, 1, 1 }, color, { 0, 0, 0, 0 } });
            }
            void Image(const Rect& r, const glm::vec4& color, const Texture2D* texture) {
                if (Texture != texture) {
                    Flush();
                    Texture = texture;
                }
                // Texture2D carrega com flip vertical: v=1 e o topo da imagem.
                Quads.push_back({ { r.X, r.Y, r.W, r.H }, { 0, 1, 1, 0 }, color, { 1, 0, 0, 0 } });
            }
            void Glyph(float x, float y, float w, float h, const glm::vec4& uv, const glm::vec4& color) {
                Quads.push_back({ { x, y, w, h }, uv, color, { 2, 0, 0, 0 } });
            }
        };

        // Decodifica um codepoint UTF-8 e avanca 'i'. Invalido vira '?'.
        uint32_t NextCodepoint(const std::string& s, size_t& i) {
            unsigned char c = (unsigned char)s[i++];
            if (c < 0x80)
                return c;
            int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
            if (extra < 0)
                return '?';
            uint32_t cp = c & (0x3F >> extra);
            for (int k = 0; k < extra; k++) {
                if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80)
                    return '?';
                cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
            }
            return cp;
        }

        int GlyphIndex(uint32_t cp) {
            if (cp == '\t') cp = ' ';
            if (cp < (uint32_t)kFirstCodepoint || cp > (uint32_t)kLastCodepoint)
                cp = '?';
            return (int)cp - kFirstCodepoint;
        }

        float LineWidth(const std::string& line, float scale) {
            float width = 0;
            for (size_t i = 0; i < line.size();)
                width += s_Font.Advance[GlyphIndex(NextCodepoint(line, i))] * scale;
            return width;
        }

        // Desenha 'text' dentro de 'box'. align: 0 esq, 1 centro, 2 dir.
        // verticalCenter: centraliza o bloco de linhas na altura da caixa.
        void DrawUIText(Batcher& batch, const std::string& text, const Rect& box, float fontSize, int align,
                      bool verticalCenter, const glm::vec4& color) {
            if (text.empty() || fontSize <= 0.0f || s_FontTexture == 0)
                return;
            const float scale = fontSize / kEmSize;
            const float lineHeight = fontSize * 1.2f;

            std::vector<std::string> lines(1);
            for (char ch : text) {
                if (ch == '\n') lines.emplace_back();
                else if (ch != '\r') lines.back().push_back(ch);
            }

            float top = box.Y;
            if (verticalCenter)
                top = box.Y + (box.H - lineHeight * (float)lines.size()) * 0.5f;

            const float atlasW = (float)(kColumns * s_Font.CellWidth);
            const float atlasH = (float)(kRows * s_Font.CellHeight);
            for (const std::string& line : lines) {
                float penX = box.X;
                float width = LineWidth(line, scale);
                if (align == 1)      penX = box.X + (box.W - width) * 0.5f;
                else if (align == 2) penX = box.X + box.W - width;
                // A linha de base fica 'ascendente' abaixo do topo da linha.
                float cellTop = top + fontSize * s_Font.AscentRatio - (float)s_Font.Baseline * scale;
                for (size_t i = 0; i < line.size();) {
                    int index = GlyphIndex(NextCodepoint(line, i));
                    float advance = s_Font.Advance[index] * scale;
                    if (index != 0) { // espaco nao desenha
                        int column = index % kColumns;
                        int row = index / kColumns;
                        glm::vec4 uv(
                            column * s_Font.CellWidth / atlasW, row * s_Font.CellHeight / atlasH,
                            (column + 1) * s_Font.CellWidth / atlasW, (row + 1) * s_Font.CellHeight / atlasH);
                        batch.Glyph(penX - (float)s_Font.PadX * scale, cellTop,
                                    (float)s_Font.CellWidth * scale, (float)s_Font.CellHeight * scale, uv, color);
                    }
                    penX += advance;
                }
                top += lineHeight;
            }
        }

        const Texture2D* LoadTexture(const std::string& path) {
            if (path.empty())
                return nullptr;
            Texture2D* texture = Renderer::GetOrLoadTexture(path, false);
            return (texture && texture->IsValid()) ? texture : nullptr;
        }

        // --- Documentos .prismui ---------------------------------------------
        class EngineMeasure : public Ui::Measure {
        public:
            float TextWidth(const std::string& utf8, float fontPx) const override {
                return LineWidth(utf8, fontPx / kEmSize);
            }
            Ui::Size ImageSize(const std::string& path) const override {
                const Texture2D* texture = LoadTexture(path);
                return texture ? Ui::Size{ (float)texture->GetWidth(), (float)texture->GetHeight() } : Ui::Size{};
            }
        };

        struct DocItem {
            entt::entity Handle;
            UIDocumentComponent* Component;
            UIDocumentRuntime* Runtime;
        };

        // Le/recarrega o .prismui quando o caminho muda ou o arquivo e modificado
        // (checa a data a cada 0.5 s). Erro de XML: mantem o documento anterior.
        void EnsureDocument(UIDocumentComponent& c) {
            if (!c.Runtime.Ptr)
                c.Runtime.Ptr = std::make_shared<UIDocumentRuntime>();
            UIDocumentRuntime& rt = *c.Runtime.Ptr;

            const auto now = std::chrono::steady_clock::now();
            const bool pathChanged = rt.LoadedPath != c.Path;
            if (!pathChanged && now < rt.NextCheck)
                return;
            rt.NextCheck = now + std::chrono::milliseconds(500);

            if (pathChanged) {
                rt.LoadedPath = c.Path;
                rt.HasDocument = false;
                rt.FailedOnce = false;
                rt.Doc = Ui::Document{};
                rt.Input.Reset();
                rt.WriteTime = {};
            }
            if (c.Path.empty())
                return;

            std::filesystem::path full = c.Path;
            if (auto project = Project::GetActive())
                full = project->GetProjectDirectory() / c.Path;

            std::error_code ec;
            auto writeTime = std::filesystem::last_write_time(full, ec);
            if (ec) {
                if (!rt.FailedOnce) {
                    PRISM_CORE_ERROR("UI: documento '", c.Path, "' nao encontrado.");
                    rt.FailedOnce = true;
                }
                return;
            }
            if (rt.HasDocument && writeTime == rt.WriteTime)
                return;
            if (!rt.HasDocument && rt.FailedOnce && writeTime == rt.WriteTime)
                return; // ja falhou com esta versao do arquivo

            rt.WriteTime = writeTime;
            std::ifstream file(full, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            Ui::ParseResult result = Ui::ParseDocument(text);
            const std::string name = std::filesystem::path(c.Path).generic_string();
            for (const Ui::Diagnostic& d : result.Diagnostics) {
                if (d.Level == Ui::Diagnostic::Severity::Error)
                    PRISM_CORE_ERROR(Ui::FormatDiagnostic(name, d));
                else
                    PRISM_CORE_WARN(Ui::FormatDiagnostic(name, d));
            }
            if (!result.Ok) {
                rt.FailedOnce = true; // mantem o documento anterior (se houver)
                return;
            }
            rt.Doc = std::move(result.Doc);
            rt.HasDocument = true;
            rt.FailedOnce = false;
            rt.Input.Reset();
        }

        glm::vec4 ToVec(const Ui::Color& c) { return { c.R, c.G, c.B, c.A }; }

        void DrawDocument(Batcher& batch, const Ui::Document& doc, int hovered, int pressed) {
            for (int i = 0; i < (int)doc.Nodes.size(); i++) {
                const Ui::Node& n = doc.Nodes[(size_t)i];
                if (!doc.IsEffectivelyVisible(i))
                    continue;
                const Rect r{ n.Bounds.X, n.Bounds.Y, n.Bounds.W, n.Bounds.H };
                switch (n.Type) {
                    case Ui::NodeType::Spacer:
                        break;
                    case Ui::NodeType::Panel: {
                        const Texture2D* texture = LoadTexture(n.Src);
                        if (texture)           batch.Image(r, ToVec(n.Tint), texture);
                        else if (n.Tint.A > 0) batch.Solid(r, ToVec(n.Tint));
                        break;
                    }
                    case Ui::NodeType::Image: {
                        if (const Texture2D* texture = LoadTexture(n.Src))
                            batch.Image(r, ToVec(n.Tint), texture);
                        break;
                    }
                    case Ui::NodeType::Text: {
                        std::string joined;
                        for (size_t k = 0; k < n.Lines.size(); k++)
                            joined += (k ? "\n" : "") + n.Lines[k];
                        DrawUIText(batch, joined, r, n.ScaledFontSize, (int)n.Align_, false, ToVec(n.Tint));
                        break;
                    }
                    case Ui::NodeType::Button: {
                        Ui::Color fill = n.Tint;
                        if (!n.Interactable)
                            fill = fill * Ui::Color(0.5f, 0.5f, 0.5f, 0.6f);
                        else if (i == pressed)
                            fill = n.HasPressedColor ? n.PressedColor : fill * Ui::Color(Ui::Node::kDefaultPressed, Ui::Node::kDefaultPressed, Ui::Node::kDefaultPressed, 1.0f);
                        else if (i == hovered)
                            fill = n.HasHoverColor ? n.HoverColor : fill * Ui::Color(Ui::Node::kDefaultHover, Ui::Node::kDefaultHover, Ui::Node::kDefaultHover, 1.0f);
                        const Texture2D* texture = LoadTexture(n.Src);
                        if (texture)           batch.Image(r, ToVec(fill), texture);
                        else if (fill.A > 0)   batch.Solid(r, ToVec(fill));
                        std::string joined;
                        for (size_t k = 0; k < n.Lines.size(); k++)
                            joined += (k ? "\n" : "") + n.Lines[k];
                        DrawUIText(batch, joined, r, n.ScaledFontSize, (int)n.Align_, true, ToVec(n.TextColor));
                        break;
                    }
                }
            }
        }

    } // namespace

    void UISystem::ResetInput() {
        s_PointerOverUI = false;
    }

    void UISystem::Shutdown() {
        if (s_FontTexture) {
            glDeleteTextures(1, &s_FontTexture);
            s_FontTexture = 0;
        }
        s_FontFailed = false;
        ResetInput();
    }

    Ui::Node* UISystem::FindDocumentNode(Entity entity, const std::string& id) {
        if (!entity.IsValid() || !entity.HasComponent<UIDocumentComponent>())
            return nullptr;
        UIDocumentComponent& component = entity.GetComponent<UIDocumentComponent>();
        EnsureDocument(component);
        if (!component.Runtime.Ptr || !component.Runtime.Ptr->HasDocument)
            return nullptr;
        Ui::Document& doc = component.Runtime.Ptr->Doc;
        int index = doc.Find(id);
        return doc.IsValidIndex(index) ? &doc.Nodes[(size_t)index] : nullptr;
    }

    void UISystem::Render(Scene& scene, float layoutWidth, float layoutHeight, bool interactive) {
        entt::registry& registry = scene.GetRegistry();
        auto docView = registry.view<UIDocumentComponent>();
        if (docView.begin() == docView.end()) {
            if (interactive)
                s_PointerOverUI = false;
            return;
        }
        if (layoutWidth <= 0.0f || layoutHeight <= 0.0f)
            return;

        // 0) Documentos .prismui: carrega (hot reload) e calcula o layout; ordem = Layer.
        EnsureFont();
        std::vector<DocItem> docs;
        {
            const EngineMeasure measure;
            for (entt::entity handle : docView) {
                auto& component = registry.get<UIDocumentComponent>(handle);
                EnsureDocument(component);
                if (!component.Visible || !component.Runtime.Ptr || !component.Runtime.Ptr->HasDocument)
                    continue;
                Ui::LayoutDocument(component.Runtime.Ptr->Doc, layoutWidth, layoutHeight, measure);
                docs.push_back({ handle, &component, component.Runtime.Ptr.get() });
            }
            std::stable_sort(docs.begin(), docs.end(), [](const DocItem& a, const DocItem& b) {
                return a.Component->Layer < b.Component->Layer;
            });
        }
        // Entrada dos documentos (o de cima primeiro).
        bool docBlocks = false;
        std::vector<std::pair<size_t, int>> docClicks; // (indice em 'docs', no)
        std::vector<int> docHovered(docs.size(), -1), docPressed(docs.size(), -1);
        if (interactive) {
            const bool pointerFree = Input::GetCursorMode() != CursorMode::Locked;
            const bool mouseDown = Input::IsMouseButtonDown(MouseButton::Left);
            for (size_t k = docs.size(); k-- > 0;) {
                Ui::Interaction::Result res = docs[k].Runtime->Input.Update(
                    docs[k].Runtime->Doc, Input::GetMouseX(), Input::GetMouseY(), mouseDown, pointerFree && !docBlocks);
                docBlocks = docBlocks || res.PointerOverUI;
                docHovered[k] = res.Hovered;
                docPressed[k] = docs[k].Runtime->Input.Pressed();
                for (int node : res.Clicked)
                    docClicks.emplace_back(k, node);
            }
        }

        if (interactive)
            s_PointerOverUI = docBlocks;

        // Desenho.
        Batcher batch;
        batch.Width = layoutWidth;
        batch.Height = layoutHeight;
        for (size_t k = 0; k < docs.size(); k++)
            DrawDocument(batch, docs[k].Runtime->Doc, docHovered[k], docPressed[k]);
        batch.Flush();

        // Cliques por ultimo: um script pode destruir/alterar elementos.
        for (const auto& [k, node] : docClicks) {
            const DocItem& item = docs[k];
            if (!registry.valid(item.Handle) || !item.Runtime->Doc.IsValidIndex(node))
                continue;
            const Ui::Node& n = item.Runtime->Doc.Nodes[(size_t)node];
            const std::string handler = n.OnClick, id = n.Id; // copia: o script pode recarregar o documento
            ScriptEngine::DispatchUIDocumentClick(Entity(item.Handle, &scene), handler, id);
        }
    }

}
