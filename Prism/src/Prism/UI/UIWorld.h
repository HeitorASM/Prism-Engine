#pragma once

// ============================================================================
// UIWorld.h
// Matematica da UI em ESPACO DE MUNDO (UIDocumentComponent::WorldSpace). So glm; testada em
// tests/UIWorldTests.cpp.
//
// O canvas e um retangulo de CanvasSize pixels, centrado na origem do modelo, no plano local
// XY, com a face visivel voltada para +Z (um pixel = 1 / PixelsPerMeter metros; o pixel (0,0)
// e o canto superior esquerdo, Y do canvas cresce para BAIXO). Estas funcoes convertem entre
// o mundo e os pixels do canvas - o mesmo espaco que Ui::LayoutDocument e Ui::Interaction usam.
// ============================================================================

#include <glm/glm.hpp>
#include <cmath>

namespace Prism::Ui {

    struct CanvasHit {
        bool Hit = false;
        float X = 0.0f;         // pixel do canvas
        float Y = 0.0f;
        float Distance = 0.0f;  // distancia no mundo ao longo do raio
    };

    // Modelo do canvas. 'entityWorld' = transform de mundo da entidade. Com 'billboard', mantem a
    // posicao e a ESCALA da entidade mas gira o canvas para olhar a camera ('view' = matriz de view).
    inline glm::mat4 CanvasModel(const glm::mat4& entityWorld, bool billboard, const glm::mat4& view) {
        if (!billboard)
            return entityWorld;
        const glm::vec3 right(view[0][0], view[1][0], view[2][0]);   // eixos da camera no mundo
        const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
        const glm::vec3 toCamera(view[0][2], view[1][2], view[2][2]); // +Z da camera aponta para tras: de volta a camera
        const float sx = glm::length(glm::vec3(entityWorld[0]));
        const float sy = glm::length(glm::vec3(entityWorld[1]));
        const float sz = glm::length(glm::vec3(entityWorld[2]));
        glm::mat4 model(1.0f);
        model[0] = glm::vec4(right * sx, 0.0f);
        model[1] = glm::vec4(up * sy, 0.0f);
        model[2] = glm::vec4(toCamera * sz, 0.0f);
        model[3] = entityWorld[3];
        return model;
    }

    // Raio (origem + direcao NORMALIZADA, no mundo) contra o canvas. So acerta a face da FRENTE
    // (o raio precisa andar para -Z local) e dentro do retangulo.
    inline CanvasHit RayCanvasHit(const glm::vec3& origin, const glm::vec3& direction, const glm::mat4& model,
                                  float canvasWidth, float canvasHeight, float pixelsPerMeter) {
        CanvasHit result;
        if (!(canvasWidth > 0.0f) || !(canvasHeight > 0.0f) || !(pixelsPerMeter > 0.0f))
            return result;
        const float det = glm::determinant(model);
        if (!std::isfinite(det) || std::fabs(det) < 1e-12f)
            return result; // escala zero / matriz invalida

        const glm::mat4 inverse = glm::inverse(model);
        const glm::vec3 o = glm::vec3(inverse * glm::vec4(origin, 1.0f));
        const glm::vec3 d = glm::vec3(inverse * glm::vec4(direction, 0.0f));
        if (!(d.z < -1e-8f))
            return result; // paralelo ao plano ou olhando o canvas por tras
        const float t = -o.z / d.z;
        if (!(t > 0.0f))
            return result; // plano atras da origem do raio

        const glm::vec3 local = o + d * t;
        const float px = local.x * pixelsPerMeter + canvasWidth * 0.5f;
        const float py = -local.y * pixelsPerMeter + canvasHeight * 0.5f;
        if (!(px >= 0.0f && py >= 0.0f && px < canvasWidth && py < canvasHeight))
            return result;

        const glm::vec3 worldPoint = glm::vec3(model * glm::vec4(local, 1.0f));
        result.Hit = true;
        result.X = px;
        result.Y = py;
        result.Distance = glm::length(worldPoint - origin);
        return result;
    }

    // Raio de mundo que passa pelo ponto (ndcX, ndcY) da tela (NDC, -1..1, Y para CIMA).
    // 'inverseViewProjection' = inverse(projection * view). False se a matriz for degenerada.
    inline bool ScreenRay(float ndcX, float ndcY, const glm::mat4& inverseViewProjection,
                          glm::vec3& outOrigin, glm::vec3& outDirection) {
        const glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        const glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        if (std::fabs(nearPoint.w) < 1e-12f || std::fabs(farPoint.w) < 1e-12f)
            return false;
        const glm::vec3 n = glm::vec3(nearPoint) / nearPoint.w;
        const glm::vec3 f = glm::vec3(farPoint) / farPoint.w;
        const glm::vec3 delta = f - n;
        const float length = glm::length(delta);
        if (!(length > 1e-12f) || !std::isfinite(length))
            return false;
        outOrigin = n;
        outDirection = delta / length;
        return true;
    }

}
