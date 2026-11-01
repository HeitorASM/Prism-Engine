#include <glad/gl.h>
#include "DrawContext.h"
#include "Renderer.h"
#include "../UI/UISystem.h"
#include "../UI/UIWorld.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Prism {

    namespace {
        DrawCommandBuffer s_Buffer;
        bool s_Enabled = true;

        Renderer::LineSegment ToSegment(const DrawLineCmd& c) {
            return { glm::vec4(c.A, c.Thickness), glm::vec4(c.B, 0.0f), c.Color };
        }
    }

    void DrawContext::SetEnabled(bool enabled) {
        s_Enabled = enabled;
        if (!enabled)
            s_Buffer.Clear();
    }
    bool DrawContext::IsEnabled() { return s_Enabled; }
    void DrawContext::NewFrame(float deltaTime) { s_Buffer.NewFrame(deltaTime); }
    void DrawContext::Clear() { s_Buffer.Clear(); }
    size_t DrawContext::GetCommandCount() { return s_Buffer.Count(); }

    void DrawContext::Line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, float duration, bool depthTest, float thickness) {
        if (s_Enabled) s_Buffer.Line(a, b, color, duration, depthTest, thickness);
    }
    void DrawContext::Box(const glm::mat4& model, const glm::vec4& color, float duration, bool depthTest, float thickness) {
        if (s_Enabled) s_Buffer.Box(model, color, duration, depthTest, thickness);
    }
    void DrawContext::BoxAABB(const glm::vec3& minimum, const glm::vec3& maximum, const glm::vec4& color, float duration, bool depthTest, float thickness) {
        if (s_Enabled) s_Buffer.BoxAABB(minimum, maximum, color, duration, depthTest, thickness);
    }
    void DrawContext::Sphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration, bool depthTest, float thickness) {
        if (s_Enabled) s_Buffer.Sphere(center, radius, color, duration, depthTest, thickness);
    }
    void DrawContext::Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, float duration, bool depthTest, float thickness) {
        if (s_Enabled) s_Buffer.Arrow(from, to, color, duration, depthTest, thickness);
    }
    void DrawContext::Axes(const glm::mat4& model, float size, float duration, bool depthTest) {
        if (s_Enabled) s_Buffer.Axes(model, size, duration, depthTest);
    }
    void DrawContext::Text3D(const glm::vec3& position, const std::string& text, const glm::vec4& color, float heightMeters, float duration, bool depthTest) {
        if (s_Enabled) s_Buffer.Text3D(position, text, color, heightMeters, duration, depthTest);
    }
    void DrawContext::Line2D(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color, float duration, float thickness) {
        if (s_Enabled) s_Buffer.Line2D(a, b, color, duration, thickness);
    }
    void DrawContext::Rect2D(float x, float y, float width, float height, const glm::vec4& color, bool filled, float duration) {
        if (s_Enabled) s_Buffer.Rect2D(x, y, width, height, color, filled, duration);
    }
    void DrawContext::Text2D(const std::string& text, float x, float y, const glm::vec4& color, float fontSize, float duration) {
        if (s_Enabled) s_Buffer.Text2D(text, x, y, color, fontSize, duration);
    }

    void DrawContext::RenderWorld(const float* view, const float* projection) {
        if (!s_Enabled || !view || !projection)
            return;
        const glm::mat4 viewMatrix = glm::make_mat4(view);
        const glm::mat4 viewProjection = glm::make_mat4(projection) * viewMatrix;

        // Linhas: primeiro as que testam profundidade, depois as sobrepostas (ver-atraves-de-tudo).
        if (!s_Buffer.Lines().empty()) {
            std::vector<Renderer::LineSegment> tested, overlay;
            for (const DrawLineCmd& c : s_Buffer.Lines())
                (c.Depth ? tested : overlay).push_back(ToSegment(c));
            Renderer::DrawLineSegments(tested.data(), (uint32_t)tested.size(), glm::value_ptr(viewProjection), true);
            Renderer::DrawLineSegments(overlay.data(), (uint32_t)overlay.size(), glm::value_ptr(viewProjection), false);
        }

        // Textos de mundo: um painel voltado para a camera por texto (a fonte e a da UI).
        const uint32_t fontTexture = s_Buffer.Texts().empty() ? 0u : UISystem::GetFontTexture();
        if (fontTexture == 0)
            return;
        constexpr float kFontPixels = 48.0f; // tamanho de rasterizacao do painel; a escala vem de pixelsPerMeter
        for (const DrawTextCmd& t : s_Buffer.Texts()) {
            if (!t.World)
                continue;
            const float width = UISystem::MeasureText(t.Text, kFontPixels);
            int lines = 1;
            for (char ch : t.Text) if (ch == '\n') lines++;
            const float height = kFontPixels * 1.2f * (float)lines;
            if (!(width > 0.0f))
                continue;
            std::vector<Renderer::UIQuad> quads;
            UISystem::BuildTextQuads(quads, t.Text, 0.0f, 0.0f, kFontPixels, 1, width, t.Color);
            if (quads.empty())
                continue;
            const float pixelsPerMeter = (kFontPixels * 1.2f) / t.Size; // uma linha = Size metros
            const glm::mat4 model = Ui::CanvasModel(glm::translate(glm::mat4(1.0f), t.Position), true, viewMatrix);
            Renderer::DrawUIQuadsWorld(quads.data(), (uint32_t)quads.size(), glm::value_ptr(model), glm::value_ptr(viewProjection),
                                       width, height, pixelsPerMeter, nullptr, fontTexture, t.Depth);
        }
    }

    void DrawContext::RenderScreen(float width, float height) {
        if (!s_Enabled || width <= 0.0f || height <= 0.0f)
            return;

        // Retangulos e textos: um unico lote, na ordem em que foram pedidos (retangulos antes dos textos).
        std::vector<Renderer::UIQuad> quads;
        for (const DrawRectCmd& r : s_Buffer.Rects()) {
            if (r.Filled) {
                quads.push_back({ { r.X, r.Y, r.W, r.H }, { 0, 0, 1, 1 }, r.Color, { 0, 0, 0, 0 } });
            } else {
                const float t = std::min(r.Thickness, std::min(r.W, r.H) * 0.5f);
                quads.push_back({ { r.X, r.Y, r.W, t }, { 0, 0, 1, 1 }, r.Color, { 0, 0, 0, 0 } });               // topo
                quads.push_back({ { r.X, r.Y + r.H - t, r.W, t }, { 0, 0, 1, 1 }, r.Color, { 0, 0, 0, 0 } });     // base
                quads.push_back({ { r.X, r.Y + t, t, r.H - 2 * t }, { 0, 0, 1, 1 }, r.Color, { 0, 0, 0, 0 } });   // esquerda
                quads.push_back({ { r.X + r.W - t, r.Y + t, t, r.H - 2 * t }, { 0, 0, 1, 1 }, r.Color, { 0, 0, 0, 0 } }); // direita
            }
        }
        for (const DrawTextCmd& t : s_Buffer.Texts())
            if (!t.World)
                UISystem::BuildTextQuads(quads, t.Text, t.Position.x, t.Position.y, t.Size, 0, 0.0f, t.Color);
        if (!quads.empty())
            Renderer::DrawUIQuads(quads.data(), (uint32_t)quads.size(), width, height, nullptr, UISystem::GetFontTexture());

        if (!s_Buffer.Lines2D().empty()) {
            std::vector<Renderer::LineSegment> segments;
            for (const DrawLineCmd& c : s_Buffer.Lines2D())
                segments.push_back(ToSegment(c));
            const glm::mat4 ortho = glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
            Renderer::DrawLineSegments(segments.data(), (uint32_t)segments.size(), glm::value_ptr(ortho), false);
        }
    }

}
