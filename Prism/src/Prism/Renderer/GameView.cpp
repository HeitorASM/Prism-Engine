#include <glad/gl.h>
#include "GameView.h"
#include "Renderer.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../UI/UISystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Prism {

    bool GameView::Render(Scene& scene, float uiWidth, float uiHeight, bool uiInteractive) {
        // A CameraComponent::Primary da cena (a regra "no maximo uma" e
        // mantida pelo editor; aqui vale a primeira encontrada).
        Entity primaryCameraEntity;
        auto cameraView = scene.GetRegistry().view<TransformComponent, CameraComponent>();
        for (auto entityHandle : cameraView) {
            if (cameraView.get<CameraComponent>(entityHandle).Primary) {
                primaryCameraEntity = Entity(entityHandle, &scene);
                break;
            }
        }

        bool drewScene = false;
        if (primaryCameraEntity) {
            GLint viewport[4];
            glGetIntegerv(GL_VIEWPORT, viewport);
            float aspect = viewport[3] > 0 ? (float)viewport[2] / (float)viewport[3] : 1.0f;

            auto& camera = primaryCameraEntity.GetComponent<CameraComponent>();
            glm::mat4 worldTransform = scene.GetWorldTransform(primaryCameraEntity);
            glm::mat4 view = glm::inverse(worldTransform);
            glm::mat4 projection = camera.GetProjection(aspect);
            glm::vec3 worldPos = glm::vec3(worldTransform[3]);

            Renderer::DrawScene(scene, glm::value_ptr(view), glm::value_ptr(projection), glm::value_ptr(worldPos), CameraRenderSettings::From(camera));
            drewScene = true;
        }
        else {
            Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);
        }

        // UI in-game por cima (e da tela vazia, se nao ha camera).
        if (uiWidth > 0.0f && uiHeight > 0.0f)
            UISystem::Render(scene, uiWidth, uiHeight, uiInteractive);

        return drewScene;
    }

}
