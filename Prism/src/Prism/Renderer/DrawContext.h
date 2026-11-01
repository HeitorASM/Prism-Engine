#pragma once

// ============================================================================
// DrawContext.h
// API de desenho IMEDIATO em C++ (e, via NativeEngineAPI, nos scripts nativos): linhas, caixas,
// esferas, setas, eixos, textos e retangulos no MUNDO (com teste de profundidade opcional) ou na TELA.
//
//   DrawContext::Line({0,0,0}, {0,2,0}, {1,0,0,1});                // 1 quadro
//   DrawContext::Sphere(hitPoint, 0.1f, {1,1,0,1}, 2.0f);          // fica 2 segundos
//   DrawContext::Text2D("FPS: 60", 8, 8, {1,1,1,1});
//
// Como funciona: as chamadas so ACUMULAM comandos (DrawCommandBuffer). O Application chama
// NewFrame(dt) no inicio de cada quadro (remove os de 1 quadro do quadro anterior); o Renderer
// desenha os de mundo dentro do DrawScene (depois das particulas) e o GameView desenha os de tela
// por cima da UI. Valem para TODA camera que desenhar o quadro (viewport, preview, jogo).
// Cores sao "de tela" (como o resto da UI/particulas). Tudo e no thread principal.
// ============================================================================

#include "DrawCommands.h"

#include <string>

namespace Prism {

    class DrawContext {
    public:
        // Liga/desliga (desligado, as chamadas viram no-ops e nada e desenhado). Ligado por padrao.
        static void SetEnabled(bool enabled);
        static bool IsEnabled();

        // --- vida (chamado pela engine) ---
        static void NewFrame(float deltaTime);
        static void Clear();
        static size_t GetCommandCount();

        // --- mundo ---
        static void Line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, float duration = 0.0f, bool depthTest = true, float thickness = 2.0f);
        static void Box(const glm::mat4& model, const glm::vec4& color, float duration = 0.0f, bool depthTest = true, float thickness = 2.0f);
        static void BoxAABB(const glm::vec3& minimum, const glm::vec3& maximum, const glm::vec4& color, float duration = 0.0f, bool depthTest = true, float thickness = 2.0f);
        static void Sphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTest = true, float thickness = 2.0f);
        static void Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, float duration = 0.0f, bool depthTest = true, float thickness = 2.0f);
        static void Axes(const glm::mat4& model, float size = 1.0f, float duration = 0.0f, bool depthTest = true);
        static void Text3D(const glm::vec3& position, const std::string& text, const glm::vec4& color, float heightMeters = 0.25f, float duration = 0.0f, bool depthTest = true);

        // --- tela (pixels, origem no canto superior esquerdo) ---
        static void Line2D(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color, float duration = 0.0f, float thickness = 2.0f);
        static void Rect2D(float x, float y, float width, float height, const glm::vec4& color, bool filled = true, float duration = 0.0f);
        static void Text2D(const std::string& text, float x, float y, const glm::vec4& color, float fontSize = 18.0f, float duration = 0.0f);

        // --- desenho (chamado pelo Renderer/GameView) ---
        static void RenderWorld(const float* view, const float* projection);
        static void RenderScreen(float width, float height);
    };

}
