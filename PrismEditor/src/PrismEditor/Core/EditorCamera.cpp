#include "EditorCamera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace PrismEditor {

    glm::mat4 EditorCamera::GetViewMatrix() const {
        // Direcao "frente" da camera a partir de yaw/pitch - mesma convencao
        // de eixos que a antiga camera de orbita usava implicitamente (yaw
        // gira em torno de Y, pitch em torno de X local). Antes este vetor
        // era "da posicao da orbita para a origem"; agora e simplesmente a
        // direcao que a camera olha a partir da posicao atual.
        //
        // -Z e a "frente" padrao de camera em OpenGL/glm (mesma convencao
        // que CameraComponent usa via glm::inverse(worldTransform) em
        // PlayWindow/RenderCameraPreview, e que o gizmo de camera em
        // RenderCameraGizmos assume ao desenhar o frustum para -Z local).
        // Os sinais negativos aqui seguem essa mesma convencao para que a
        // camera do editor e a camera de jogo (Play) olhem "para o mesmo
        // lado" dado o mesmo yaw/pitch.
        float yawRad = glm::radians(Yaw);
        float pitchRad = glm::radians(Pitch);

        glm::vec3 forward(
            -cosf(pitchRad) * cosf(yawRad),
            -sinf(pitchRad),
            -cosf(pitchRad) * sinf(yawRad)
        );

        return glm::lookAt(Position, Position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    }

}
