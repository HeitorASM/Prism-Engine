#pragma once

// ============================================================================
// DrawCommands.h
// Buffer de comandos do desenho imediato (DrawContext): acumula linhas, caixas, esferas,
// setas, retangulos e textos (no mundo ou na tela) ate o renderer desenhar. So glm + std;
// testado em tests/DrawCommandTests.cpp sem janela nem GPU.
//
// Vida de um comando:
//  - duration <= 0: dura UM quadro - e removido no proximo NewFrame (chame as funcoes de
//    desenho todo quadro, como num HUD imediato);
//  - duration > 0: fica ate acumular 'duration' segundos de NewFrame(dt) (marcas de debug).
// Dados invalidos (NaN/inf) sao descartados; espessura, duracao e segmentos sao limitados;
// o total e limitado (kMaxCommands) para um loop que esqueceu de usar duracao 0 nao crescer sem fim.
// ============================================================================

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace Prism {

    struct DrawLineCmd {
        glm::vec3 A{ 0.0f };
        glm::vec3 B{ 0.0f };
        glm::vec4 Color{ 1.0f };
        float Thickness = 2.0f;   // pixels
        bool Depth = true;        // testa contra a profundidade (so linhas de mundo)
        float Remaining = 0.0f;
        bool Timed = false;
    };

    struct DrawRectCmd {
        float X = 0.0f, Y = 0.0f, W = 0.0f, H = 0.0f; // pixels de tela (origem no canto superior esquerdo)
        glm::vec4 Color{ 1.0f };
        bool Filled = true;
        float Thickness = 1.0f;   // borda, quando nao preenchido
        float Remaining = 0.0f;
        bool Timed = false;
    };

    struct DrawTextCmd {
        std::string Text;
        glm::vec3 Position{ 0.0f }; // mundo (World) ou {x, y, 0} em pixels de tela
        glm::vec4 Color{ 1.0f };
        float Size = 0.0f;          // mundo: altura de uma linha em METROS; tela: tamanho da fonte em pixels
        bool World = false;
        bool Depth = true;          // so texto de mundo
        float Remaining = 0.0f;
        bool Timed = false;
    };

    class DrawCommandBuffer {
    public:
        static constexpr size_t kMaxCommands = 100000;
        static constexpr size_t kMaxTextLength = 512;

        // --- mundo ---
        bool Line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, float duration = 0.0f,
                  bool depthTest = true, float thickness = 2.0f) {
            if (!Finite(a) || !Finite(b) || !Finite(color) || Count() >= kMaxCommands)
                return false;
            DrawLineCmd c;
            c.A = a; c.B = b; c.Color = color;
            c.Thickness = ClampThickness(thickness);
            c.Depth = depthTest;
            SetLife(c, duration);
            m_Lines.push_back(c);
            return true;
        }

        // Caixa de aresta 1 centrada na origem, transformada por 'model' (posicao, rotacao, escala).
        bool Box(const glm::mat4& model, const glm::vec4& color, float duration = 0.0f, bool depthTest = true, float thickness = 2.0f) {
            glm::vec3 corners[8];
            for (int i = 0; i < 8; i++) {
                const glm::vec3 local((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
                corners[i] = glm::vec3(model * glm::vec4(local, 1.0f));
            }
            // 12 arestas: pares de cantos que diferem em exatamente um bit.
            bool any = false;
            for (int i = 0; i < 8; i++)
                for (int bit = 1; bit <= 4; bit <<= 1)
                    if (!(i & bit))
                        any |= Line(corners[i], corners[i | bit], color, duration, depthTest, thickness);
            return any;
        }

        bool BoxAABB(const glm::vec3& minimum, const glm::vec3& maximum, const glm::vec4& color, float duration = 0.0f,
                     bool depthTest = true, float thickness = 2.0f) {
            const glm::vec3 center = (minimum + maximum) * 0.5f;
            const glm::vec3 size = glm::abs(maximum - minimum);
            glm::mat4 model(1.0f);
            model[0][0] = size.x; model[1][1] = size.y; model[2][2] = size.z;
            model[3] = glm::vec4(center, 1.0f);
            return Box(model, color, duration, depthTest, thickness);
        }

        // Tres circulos maximos (XY, XZ, YZ).
        bool Sphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration = 0.0f,
                    bool depthTest = true, float thickness = 2.0f, int segments = 24) {
            if (!std::isfinite(radius) || radius <= 0.0f)
                return false;
            bool any = false;
            any |= Circle(center, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), radius, color, duration, depthTest, thickness, segments);
            any |= Circle(center, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), radius, color, duration, depthTest, thickness, segments);
            any |= Circle(center, glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), radius, color, duration, depthTest, thickness, segments);
            return any;
        }

        // Circulo no plano gerado por 'axisU' e 'axisV' (vetores unitarios perpendiculares).
        bool Circle(const glm::vec3& center, const glm::vec3& axisU, const glm::vec3& axisV, float radius, const glm::vec4& color,
                    float duration = 0.0f, bool depthTest = true, float thickness = 2.0f, int segments = 24) {
            if (!std::isfinite(radius) || radius <= 0.0f || !Finite(center) || !Finite(axisU) || !Finite(axisV))
                return false;
            segments = std::min(std::max(segments, 6), 64);
            bool any = false;
            glm::vec3 previous = center + axisU * radius;
            for (int i = 1; i <= segments; i++) {
                const float angle = 6.28318530718f * (float)i / (float)segments;
                const glm::vec3 point = center + (axisU * std::cos(angle) + axisV * std::sin(angle)) * radius;
                any |= Line(previous, point, color, duration, depthTest, thickness);
                previous = point;
            }
            return any;
        }

        // Seta de 'from' a 'to', com a ponta de comprimento 'headSize' (0 = 15% do comprimento).
        bool Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, float duration = 0.0f,
                   bool depthTest = true, float thickness = 2.0f, float headSize = 0.0f) {
            if (!Finite(from) || !Finite(to))
                return false;
            const glm::vec3 delta = to - from;
            const float length = glm::length(delta);
            if (!(length > 1e-6f))
                return false;
            const glm::vec3 dir = delta / length;
            if (!(headSize > 0.0f) || !std::isfinite(headSize))
                headSize = length * 0.15f;
            headSize = std::min(headSize, length);
            // Um vetor perpendicular qualquer a 'dir'.
            const glm::vec3 helper = std::fabs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 side = glm::normalize(glm::cross(dir, helper));
            const glm::vec3 up = glm::cross(side, dir);
            const glm::vec3 baseCenter = to - dir * headSize;
            const float spread = headSize * 0.4f;
            bool any = Line(from, to, color, duration, depthTest, thickness);
            any |= Line(to, baseCenter + side * spread, color, duration, depthTest, thickness);
            any |= Line(to, baseCenter - side * spread, color, duration, depthTest, thickness);
            any |= Line(to, baseCenter + up * spread, color, duration, depthTest, thickness);
            any |= Line(to, baseCenter - up * spread, color, duration, depthTest, thickness);
            return any;
        }

        // Eixos X (vermelho), Y (verde), Z (azul) de 'model', cada um com 'size' de comprimento (antes da escala do modelo).
        bool Axes(const glm::mat4& model, float size, float duration = 0.0f, bool depthTest = true, float thickness = 2.0f) {
            if (!std::isfinite(size) || size <= 0.0f)
                return false;
            const glm::vec3 origin(model[3]);
            bool any = false;
            any |= Line(origin, glm::vec3(model * glm::vec4(size, 0, 0, 1)), glm::vec4(1, 0.2f, 0.2f, 1), duration, depthTest, thickness);
            any |= Line(origin, glm::vec3(model * glm::vec4(0, size, 0, 1)), glm::vec4(0.2f, 1, 0.2f, 1), duration, depthTest, thickness);
            any |= Line(origin, glm::vec3(model * glm::vec4(0, 0, size, 1)), glm::vec4(0.3f, 0.5f, 1, 1), duration, depthTest, thickness);
            return any;
        }

        // Texto voltado para a camera, centrado em 'position'; 'heightMeters' = altura de uma linha.
        bool Text3D(const glm::vec3& position, const std::string& text, const glm::vec4& color, float heightMeters = 0.25f,
                    float duration = 0.0f, bool depthTest = true) {
            if (text.empty() || !Finite(position) || !Finite(color) || !std::isfinite(heightMeters) || heightMeters <= 0.0f || Count() >= kMaxCommands)
                return false;
            DrawTextCmd c;
            c.Text = text.substr(0, kMaxTextLength);
            c.Position = position; c.Color = color;
            c.Size = std::min(heightMeters, 1000.0f);
            c.World = true; c.Depth = depthTest;
            SetLife(c, duration);
            m_Texts.push_back(std::move(c));
            return true;
        }

        // --- tela (pixels, origem no canto superior esquerdo) ---
        bool Line2D(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color, float duration = 0.0f, float thickness = 2.0f) {
            if (!Finite(a) || !Finite(b) || !Finite(color) || Count() >= kMaxCommands)
                return false;
            DrawLineCmd c;
            c.A = glm::vec3(a, 0.0f); c.B = glm::vec3(b, 0.0f); c.Color = color;
            c.Thickness = ClampThickness(thickness);
            c.Depth = false;
            SetLife(c, duration);
            m_Lines2D.push_back(c);
            return true;
        }

        bool Rect2D(float x, float y, float width, float height, const glm::vec4& color, bool filled = true,
                    float duration = 0.0f, float thickness = 1.0f) {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || !Finite(color) ||
                width <= 0.0f || height <= 0.0f || Count() >= kMaxCommands)
                return false;
            DrawRectCmd c;
            c.X = x; c.Y = y; c.W = width; c.H = height; c.Color = color; c.Filled = filled;
            c.Thickness = ClampThickness(thickness);
            SetLife(c, duration);
            m_Rects.push_back(c);
            return true;
        }

        bool Text2D(const std::string& text, float x, float y, const glm::vec4& color, float fontSize = 18.0f, float duration = 0.0f) {
            if (text.empty() || !std::isfinite(x) || !std::isfinite(y) || !Finite(color) || !std::isfinite(fontSize) || fontSize <= 0.0f ||
                Count() >= kMaxCommands)
                return false;
            DrawTextCmd c;
            c.Text = text.substr(0, kMaxTextLength);
            c.Position = glm::vec3(x, y, 0.0f); c.Color = color;
            c.Size = std::min(fontSize, 1024.0f);
            c.World = false; c.Depth = false;
            SetLife(c, duration);
            m_Texts.push_back(std::move(c));
            return true;
        }

        // --- vida ---
        // Chamar UMA vez por quadro, antes de qualquer desenho do quadro.
        void NewFrame(float deltaTime) {
            if (!(deltaTime > 0.0f) || !std::isfinite(deltaTime))
                deltaTime = 0.0f;
            Expire(m_Lines, deltaTime);
            Expire(m_Lines2D, deltaTime);
            Expire(m_Rects, deltaTime);
            Expire(m_Texts, deltaTime);
        }

        void Clear() {
            m_Lines.clear(); m_Lines2D.clear(); m_Rects.clear(); m_Texts.clear();
        }

        size_t Count() const { return m_Lines.size() + m_Lines2D.size() + m_Rects.size() + m_Texts.size(); }

        const std::vector<DrawLineCmd>& Lines() const { return m_Lines; }
        const std::vector<DrawLineCmd>& Lines2D() const { return m_Lines2D; }
        const std::vector<DrawRectCmd>& Rects() const { return m_Rects; }
        const std::vector<DrawTextCmd>& Texts() const { return m_Texts; }

    private:
        static bool Finite(const glm::vec2& v) { return std::isfinite(v.x) && std::isfinite(v.y); }
        static bool Finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        static bool Finite(const glm::vec4& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); }
        static float ClampThickness(float t) { return std::isfinite(t) ? std::min(std::max(t, 1.0f), 32.0f) : 2.0f; }

        template<typename T>
        static void SetLife(T& command, float duration) {
            if (std::isfinite(duration) && duration > 0.0f) {
                command.Timed = true;
                command.Remaining = std::min(duration, 3600.0f);
            }
        }

        template<typename T>
        static void Expire(std::vector<T>& commands, float deltaTime) {
            commands.erase(std::remove_if(commands.begin(), commands.end(), [&](T& c) {
                if (!c.Timed) return true;      // 1 quadro: ja foi desenhado
                c.Remaining -= deltaTime;
                return c.Remaining <= 0.0f;
            }), commands.end());
        }

        std::vector<DrawLineCmd> m_Lines;    // mundo
        std::vector<DrawLineCmd> m_Lines2D;  // tela
        std::vector<DrawRectCmd> m_Rects;
        std::vector<DrawTextCmd> m_Texts;
    };

}
