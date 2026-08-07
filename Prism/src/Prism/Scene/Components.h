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

    // Tipos de luz suportados - nomes escolhidos para bater com o
    // vocabulario do guia do prototipo ("spotlight", "omnilight"/point
    // light) em vez de nomes tecnicos de shader.
    enum class LightType {
        Point,       // omnilight - brilha em todas as direcoes a partir de um ponto
        Spot,        // cone de luz, como uma lanterna
        Directional  // luz paralela vindo de uma direcao (ex: sol) - posicao da entidade e ignorada, so a rotacao importa
    };

    // Component de luz. Ainda NAO afeta a renderizacao (o Renderer hoje so
    // tem uma luz direcional fixa hardcoded dentro do shader - ver
    // Renderer.cpp) - isto e o dado/slot que o editor ja permite criar e
    // configurar, para quando o Renderer ganhar iluminacao de verdade
    // (multiplas luzes, sombras) essas entidades ja estarem prontas para
    // uso, sem precisar de outra rodada de migracao de dados salvos.
    struct LightComponent {
        LightType Type = LightType::Point;
        glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;

        // Relevante so para Point/Spot - alcance da luz antes de se
        // tornar imperceptivel. Directional ignora isso (luz "infinita").
        float Range = 10.0f;

        // Relevante so para Spot - metade do angulo do cone, em graus
        // (0-90). Point/Directional ignoram isso.
        float SpotAngle = 45.0f;

        LightComponent() = default;
        LightComponent(const LightComponent&) = default;
    };

    // Formas de colisao suportadas. Nomeado "Collider" (nao "Physics") de
    // proposito: um Collider pode existir SEM RigidBodyComponent (colisao
    // estatica, ex: as paredes de um mapa) - a combinacao dos dois e que
    // define o comportamento fisico completo (ver RigidBodyComponent).
    enum class ColliderShape {
        Box,
        Sphere,
        Capsule
    };

    // Define a FORMA de colisao de uma entidade. Ainda nao alimenta
    // nenhuma simulacao de fisica de verdade (Box3D - ver guia do
    // prototipo - ainda nao esta integrado); e o dado/slot que o editor
    // ja permite configurar. Uma entidade com só Collider (sem
    // RigidBodyComponent) e um obstaculo estatico - nao se move, mas
    // outras coisas colidem com ela (ex: o chao, paredes).
    struct ColliderComponent {
        ColliderShape Shape = ColliderShape::Box;

        // Interpretacao depende de Shape: Box usa Size como half-extents
        // (x,y,z); Sphere usa so Size.x como raio; Capsule usa Size.x como
        // raio e Size.y como altura (Size.z ignorado nesse caso).
        glm::vec3 Size = { 0.5f, 0.5f, 0.5f };

        // Trigger = detecta sobreposicao mas nao gera resposta fisica
        // (nao empurra nada) - usado para zonas de gatilho (ex: area que
        // dispara um script quando o player entra), nao para colisao solida.
        bool IsTrigger = false;

        ColliderComponent() = default;
        ColliderComponent(const ColliderComponent&) = default;
    };

    // Como o corpo fisico se comporta dentro da simulacao (quando existir -
    // ver ColliderComponent). Nomes que batem com o vocabulario padrao de
    // motores de fisica (Box3D incluido):
    //   Static    - nunca se move, simulacao nao gasta tempo com ele (paredes, chao)
    //   Kinematic - se move, mas so via codigo/script, nunca empurrado pela fisica (plataformas, portas)
    //   Dynamic   - totalmente simulado (gravidade, colisoes empurram) (caixas, o player com fisica)
    enum class BodyType {
        Static,
        Kinematic,
        Dynamic
    };

    // Define o comportamento fisico de uma entidade DENTRO da simulacao -
    // exige um ColliderComponent na mesma entidade para fazer sentido (uma
    // entidade so pode colidir/ser simulada se tiver uma forma definida).
    // Igual ColliderComponent, ainda nao alimenta nenhuma simulacao de
    // verdade - e o dado que o editor ja permite configurar, para quando
    // Box3D for integrado (proximo item grande do roadmap depois de
    // scripting) essas entidades ja estarem prontas.
    struct RigidBodyComponent {
        BodyType Type = BodyType::Dynamic;
        float Mass = 1.0f;              // kg - so relevante para Dynamic
        bool UseGravity = true;         // so relevante para Dynamic
        bool ContinuousCollisionDetection = false; // CCD - para objetos rapidos nao atravessarem paredes (ver guia do prototipo)

        RigidBodyComponent() = default;
        RigidBodyComponent(const RigidBodyComponent&) = default;
    };

    // Slot de script anexado a uma entidade - ainda NAO executa nada (Lua
    // ainda nao esta embutido na engine, ver guia do prototipo). Isto so
    // guarda QUAL arquivo .lua esta associado a entidade, para que:
    //   1) o editor ja tenha uma UI para o fluxo completo antes do Lua
    //      existir (evita re-projetar a UI depois)
    //   2) cenas salvas agora ja carreguem esse dado quando scripting for
    //      ligado, sem precisar migrar arquivos .prismmap antigos
    struct ScriptComponent {
        // Caminho RELATIVO a Project::GetScriptDirectory() (ex:
        // "player_controller.lua") - nao absoluto, para o projeto
        // continuar portavel entre maquinas/pastas.
        std::string ScriptPath;

        ScriptComponent() = default;
        ScriptComponent(const ScriptComponent&) = default;
        ScriptComponent(const std::string& scriptPath) : ScriptPath(scriptPath) {}
    };

}
