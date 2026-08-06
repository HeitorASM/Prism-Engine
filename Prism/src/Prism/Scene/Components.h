#pragma once

// ============================================================================
// Components.h
// Components sao structs "burros": so dados, sem logica. Uma Entity e so um
// ID que aponta para um conjunto de components dentro do registro EnTT da
// Scene dona dela (ver Scene.h/Entity.h). Isso e a essencia de um ECS:
// comportamento vive em sistemas (funcoes/metodos de Scene) que iteram sobre
// components, nao em metodos dos proprios components.
// ============================================================================

#define GLM_ENABLE_EXPERIMENTAL // necessario para incluir headers gtx/* (yawPitchRoll vive em gtx/euler_angles.hpp)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp> // glm::yawPitchRoll (extensao GTX, header separado do gtc/quaternion)
#include <string>

namespace Prism {

    // Todo Entity criada por Scene::CreateEntity ja recebe este component -
    // e o que da um nome legivel a entidade na Hierarchy panel do editor.
    struct TagComponent {
        std::string Tag;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    // Todo Entity criada por Scene::CreateEntity tambem ja recebe este
    // component - posicao/rotacao/escala no espaco da cena. Rotacao em
    // graus (Euler XYZ) por simplicidade de UI no editor; convertida para
    // quaternion/matriz so na hora de montar GetTransform().
    struct TransformComponent {
        glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // graus
        glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& translation) : Translation(translation) {}

        glm::mat4 GetTransform() const {
            glm::mat4 rotation = glm::yawPitchRoll(
                glm::radians(Rotation.y), glm::radians(Rotation.x), glm::radians(Rotation.z));

            return glm::translate(glm::mat4(1.0f), Translation)
                 * rotation
                 * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    // Primitivas de mesh embutidas na engine (sem importacao de assets
    // ainda - ver guia do prototipo: FBX/OBJ/glTF ficam para uma fase
    // futura). Por ora, o Renderer so sabe desenhar Cube (ver
    // Renderer::DrawTestCube). Novas primitivas (Sphere, Plane, Cylinder)
    // entram aqui conforme o Renderer ganhar meshes para elas.
    enum class PrimitiveMesh {
        Cube
    };

    // Marca a entidade como algo visivel na viewport 3D. Ainda nao tem
    // Material de verdade (so uma cor solida) - isso e o proximo ponto de
    // extensao natural quando o sistema de assets/materiais existir.
    struct MeshRendererComponent {
        PrimitiveMesh Mesh = PrimitiveMesh::Cube;
        glm::vec3 Color = { 0.85f, 0.55f, 0.2f };

        MeshRendererComponent() = default;
        MeshRendererComponent(const MeshRendererComponent&) = default;
    };

    // Camera de cena (distinta da camera de editor em EditorLayer, que so
    // existe para navegar a viewport). Ainda nao usada em runtime - reservada
    // para quando existir um modo "Play" que roda a cena do ponto de vista
    // de uma camera de jogo em vez da camera de editor.
    struct CameraComponent {
        float FOV = 45.0f;
        float NearClip = 0.1f;
        float FarClip = 1000.0f;
        bool Primary = true;

        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;
    };

}
