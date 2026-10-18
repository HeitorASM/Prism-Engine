#include "UISystem.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Renderer/Renderer.h"
#include "../Scripting/ScriptEngine.h"
#include "../Core/Input.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"

#include <glad/gl.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include <algorithm>
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

        struct Item {
            entt::entity Handle;
            UIElementComponent* Element;
            Rect Bounds;
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

        // Estado de entrada (um botao apertado por vez).
        Scene* s_PressedScene = nullptr;
        entt::entity s_PressedHandle = entt::null;
        bool s_PreviousMouseDown = false;

        // --- Layout ---------------------------------------------------------
        Rect ComputeRect(const UIElementComponent& c, const Rect& parent) {
            Rect r;
            if (c.Anchor == UIAnchor::Stretch) {
                r.X = parent.X + c.Offset.x;
                r.Y = parent.Y + c.Offset.y;
                r.W = std::max(0.0f, parent.W - 2.0f * c.Offset.x);
                r.H = std::max(0.0f, parent.H - 2.0f * c.Offset.y);
                return r;
            }
            r.W = std::max(0.0f, c.Size.x);
            r.H = std::max(0.0f, c.Size.y);
            int a = (int)c.Anchor;
            int column = a % 3; // 0 esquerda, 1 centro, 2 direita
            int row = a / 3;    // 0 topo, 1 meio, 2 fundo
            if (column == 0)      r.X = parent.X + c.Offset.x;
            else if (column == 1) r.X = parent.X + (parent.W - r.W) * 0.5f + c.Offset.x;
            else                  r.X = parent.X + parent.W - c.Offset.x - r.W;
            if (row == 0)         r.Y = parent.Y + c.Offset.y;
            else if (row == 1)    r.Y = parent.Y + (parent.H - r.H) * 0.5f + c.Offset.y;
            else                  r.Y = parent.Y + parent.H - c.Offset.y - r.H;
            return r;
        }

        void CollectChildren(entt::registry& registry, entt::entity handle, const Rect& bounds, std::vector<Item>& out, int depth) {
            if (depth > 64)
                return;
            auto* relationship = registry.try_get<RelationshipComponent>(handle);
            if (!relationship)
                return;
            for (entt::entity child : relationship->Children) {
                auto* element = registry.try_get<UIElementComponent>(child);
                if (!element || !element->Visible)
                    continue;
                Rect childBounds = ComputeRect(*element, bounds);
                out.push_back({ child, element, childBounds });
                CollectChildren(registry, child, childBounds, out, depth + 1);
            }
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

    } // namespace

    void UISystem::ResetInput() {
        s_PressedScene = nullptr;
        s_PressedHandle = entt::null;
        s_PreviousMouseDown = false;
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

    void UISystem::Render(Scene& scene, float layoutWidth, float layoutHeight, bool interactive) {
        entt::registry& registry = scene.GetRegistry();
        auto view = registry.view<UIElementComponent>();
        if (view.begin() == view.end()) {
            if (interactive)
                s_PointerOverUI = false;
            return;
        }
        if (layoutWidth <= 0.0f || layoutHeight <= 0.0f)
            return;

        // 1) Raizes (sem pai de UI), ordenadas por Layer; depois layout em profundidade.
        std::vector<entt::entity> roots;
        for (entt::entity handle : view) {
            const auto* relationship = registry.try_get<RelationshipComponent>(handle);
            bool hasUIParent = relationship && relationship->Parent != entt::null
                && registry.valid(relationship->Parent) && registry.all_of<UIElementComponent>(relationship->Parent);
            if (!hasUIParent)
                roots.push_back(handle);
        }
        std::stable_sort(roots.begin(), roots.end(), [&](entt::entity a, entt::entity b) {
            return registry.get<UIElementComponent>(a).Layer < registry.get<UIElementComponent>(b).Layer;
        });

        const Rect screen{ 0, 0, layoutWidth, layoutHeight };
        std::vector<Item> items;
        for (entt::entity root : roots) {
            auto& element = registry.get<UIElementComponent>(root);
            if (!element.Visible)
                continue;
            Rect bounds = ComputeRect(element, screen);
            items.push_back({ root, &element, bounds });
            CollectChildren(registry, root, bounds, items, 0);
        }

        // 2) Entrada: o elemento mais acima que barra o mouse vence.
        entt::entity hovered = entt::null;
        std::vector<entt::entity> clicked;
        if (interactive) {
            bool pointerFree = Input::GetCursorMode() != CursorMode::Locked;
            bool mouseDown = Input::IsMouseButtonDown(MouseButton::Left);
            bool over = false;
            if (pointerFree) {
                float mx = Input::GetMouseX(), my = Input::GetMouseY();
                for (auto it = items.rbegin(); it != items.rend(); ++it) {
                    const UIElementComponent& e = *it->Element;
                    bool isButton = e.Type == UIElementType::Button;
                    bool blocks = isButton || (e.BlocksPointer && e.Type != UIElementType::Text);
                    if (!blocks || !it->Bounds.Contains(mx, my))
                        continue;
                    over = true;
                    if (isButton && e.Interactable)
                        hovered = it->Handle;
                    break; // o de cima barra os de baixo
                }
            }
            s_PointerOverUI = over;

            bool pressedThisScene = s_PressedScene == &scene && s_PressedHandle != entt::null;
            if (mouseDown && !s_PreviousMouseDown && hovered != entt::null) {
                s_PressedScene = &scene;
                s_PressedHandle = hovered;
            }
            else if (!mouseDown && s_PreviousMouseDown && pressedThisScene) {
                if (hovered == s_PressedHandle)
                    clicked.push_back(hovered);
                s_PressedScene = nullptr;
                s_PressedHandle = entt::null;
            }
            if (!mouseDown) {
                s_PressedScene = nullptr;
                s_PressedHandle = entt::null;
            }
            s_PreviousMouseDown = mouseDown;
        }

        // 3) Desenho.
        EnsureFont();
        Batcher batch;
        batch.Width = layoutWidth;
        batch.Height = layoutHeight;
        for (const Item& item : items) {
            const UIElementComponent& e = *item.Element;
            const Rect& r = item.Bounds;
            switch (e.Type) {
                case UIElementType::Panel:
                case UIElementType::Image: {
                    const Texture2D* texture = LoadTexture(e.TexturePath);
                    if (texture) batch.Image(r, e.Color, texture);
                    else         batch.Solid(r, e.Color);
                    break;
                }
                case UIElementType::Button: {
                    glm::vec4 tint(1.0f);
                    bool pressed = interactive && s_PressedScene == &scene && s_PressedHandle == item.Handle;
                    if (!e.Interactable)        tint = { 0.5f, 0.5f, 0.5f, 0.6f };
                    else if (pressed)           tint = e.PressedColor;
                    else if (item.Handle == hovered) tint = e.HoverColor;
                    glm::vec4 color = e.Color * tint;
                    const Texture2D* texture = LoadTexture(e.TexturePath);
                    if (texture) batch.Image(r, color, texture);
                    else         batch.Solid(r, color);
                    DrawUIText(batch, e.Text, r, e.FontSize, 1, true, e.TextColor);
                    break;
                }
                case UIElementType::Text:
                    DrawUIText(batch, e.Text, r, e.FontSize, (int)e.Align, false, e.Color);
                    break;
            }
        }
        batch.Flush();

        // 4) Cliques por ultimo: um script pode destruir/alterar elementos.
        for (entt::entity handle : clicked) {
            if (registry.valid(handle))
                ScriptEngine::DispatchUIClick(Entity(handle, &scene));
        }
    }

}
