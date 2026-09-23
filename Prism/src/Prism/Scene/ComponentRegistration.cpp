// ============================================================================
// ComponentRegistration.cpp
// Todo Component "opcional" da engine se registra AQUI, uma vez, dentro de
// ComponentRegistry::RegisterAll() - ver comentario grande em
// ComponentRegistry.h para o problema que isto resolve.
//
// NAO REGISTRADOS AQUI (de proposito):
//   - TransformComponent/TagComponent: toda entidade tem por definicao
//     (ver Scene::CreateEntity) - nunca precisam de flag de presenca nem
//     de menu "Add Component", entao nao se encaixam neste registro.
//   - RelationshipComponent: usa um esquema de serializacao FUNDAMENTALMENTE
//     diferente dos outros (indice POSICIONAL do pai dentro do arquivo,
//     resolvido para SetParent() so depois que TODAS as entidades da cena
//     existem - ver SceneSerializer::Serialize/Deserialize) em vez do
//     padrao simples "flag de presenca + campos" que todo Component aqui
//     usa. Alem disso, RelationshipComponent nunca aparece no menu "Add
//     Component" (parenting e feito via drag-and-drop na Hierarchy panel,
//     nao por um botao). Por isso continua tratado a parte, direto dentro
//     de SceneSerializer.cpp, como sempre foi.
//
// COMO ADICIONAR UM COMPONENT NOVO (ex: futuro MaterialComponent):
//   1. Definir o struct em Components.h, como sempre.
//   2. Adicionar UM bloco ComponentRegistry::Register<NovoComponent>(...)
//      aqui, seguindo o padrao dos blocos abaixo.
//   3. Se o formato binario precisar mudar de forma incompativel, seguir
//      o MESMO processo de sempre: incrementar kSceneFormatVersion em
//      SceneSerializer.cpp (o numero de versao continua vivendo la, nao
//      aqui - ver comentario nesse arquivo).
// Nenhum outro arquivo (SceneSerializer.cpp, EditorLayer.cpp) precisa ser
// tocado so por causa de um Component novo, a partir desta mudanca.
// ============================================================================

#include "ComponentRegistry.h"
#include "Components.h"
#include "../Core/Log.h"

namespace Prism {

    void ComponentRegistry::RegisterAll() {
        if (!s_Registry.empty())
            return; // idempotente - ver comentario em RegisterAll (ComponentRegistry.h)

        // --- MeshRendererComponent ------------------------------------------
        Register<MeshRendererComponent>(
            "Mesh Renderer",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<MeshRendererComponent>();
                WriteRaw(out, c.Mesh);
                WriteRaw(out, c.Color);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<MeshRendererComponent>();
                if (!(ReadRaw(in, c.Mesh) && ReadRaw(in, c.Color))) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (mesh renderer da entidade ", entityIndex, ").");
                    return false;
                }
                // Validacao do enum: um valor fora do range conhecido e
                // sinal de arquivo corrompido (ou de uma versao futura do
                // formato com mais primitivas) - sem checar isto, um
                // PrimitiveMesh invalido seguiria para dentro da Scene e
                // so falharia (silenciosamente, sem desenhar nada) la na
                // frente em Renderer::DrawMesh.
                if (c.Mesh != PrimitiveMesh::Cube && c.Mesh != PrimitiveMesh::Sphere
                    && c.Mesh != PrimitiveMesh::Capsule && c.Mesh != PrimitiveMesh::Cylinder
                    && c.Mesh != PrimitiveMesh::Plane) {
                    PRISM_CORE_ERROR("SceneSerializer: PrimitiveMesh invalido na entidade ", entityIndex, ".");
                    return false;
                }
                return true;
            }
        );

        // --- MaterialComponent -------------------------------------------------
        Register<MaterialComponent>(
            "Material",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<MaterialComponent>();
                WriteString(out, c.AlbedoPath);
                WriteString(out, c.NormalPath);
                WriteString(out, c.RoughnessMetallicPath);
                WriteRaw(out, c.AlbedoTint);
                WriteRaw(out, c.RoughnessFactor);
                WriteRaw(out, c.MetallicFactor);
                WriteRaw(out, c.LinkedAsset); // AssetID e so um uint64_t (trivialmente copiavel) - ver Assets/AssetID.h
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<MaterialComponent>();
                bool ok = ReadString(in, c.AlbedoPath) && ReadString(in, c.NormalPath)
                       && ReadString(in, c.RoughnessMetallicPath) && ReadRaw(in, c.AlbedoTint)
                       && ReadRaw(in, c.RoughnessFactor) && ReadRaw(in, c.MetallicFactor)
                       && ReadRaw(in, c.LinkedAsset);
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (material da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );

        // --- LightComponent --------------------------------------------------
        Register<LightComponent>(
            "Light",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<LightComponent>();
                WriteRaw(out, c.Type);
                WriteRaw(out, c.Color);
                WriteRaw(out, c.Intensity);
                WriteRaw(out, c.Range);
                WriteRaw(out, c.SpotAngle);
                WriteRaw(out, c.InnerSpotAngle);
                WriteRaw(out, c.CastShadows);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<LightComponent>();
                bool ok = ReadRaw(in, c.Type) && ReadRaw(in, c.Color)
                       && ReadRaw(in, c.Intensity) && ReadRaw(in, c.Range) && ReadRaw(in, c.SpotAngle)
                       && ReadRaw(in, c.InnerSpotAngle) && ReadRaw(in, c.CastShadows);
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (light da entidade ", entityIndex, ").");
                    return false;
                }
                if (c.Type != LightType::Point && c.Type != LightType::Spot && c.Type != LightType::Directional) {
                    PRISM_CORE_ERROR("SceneSerializer: LightType invalido na entidade ", entityIndex, ".");
                    return false;
                }
                return true;
            }
        );

        // --- ColliderComponent -----------------------------------------------
        Register<ColliderComponent>(
            "Collider",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<ColliderComponent>();
                WriteRaw(out, c.Shape);
                WriteRaw(out, c.Size);
                WriteRaw(out, c.IsTrigger);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<ColliderComponent>();
                bool ok = ReadRaw(in, c.Shape) && ReadRaw(in, c.Size) && ReadRaw(in, c.IsTrigger);
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (collider da entidade ", entityIndex, ").");
                    return false;
                }
                if (c.Shape != ColliderShape::Box && c.Shape != ColliderShape::Sphere && c.Shape != ColliderShape::Capsule) {
                    PRISM_CORE_ERROR("SceneSerializer: ColliderShape invalido na entidade ", entityIndex, ".");
                    return false;
                }
                return true;
            }
        );

        // --- RigidBodyComponent ------------------------------------------------
        Register<RigidBodyComponent>(
            "Rigid Body",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<RigidBodyComponent>();
                WriteRaw(out, c.Type);
                WriteRaw(out, c.Mass);
                WriteRaw(out, c.UseGravity);
                WriteRaw(out, c.ContinuousCollisionDetection);
                WriteRaw(out, c.FixedRotation);
                WriteRaw(out, c.Friction);
                WriteRaw(out, c.Restitution);
                WriteRaw(out, c.LinearDamping);
                WriteRaw(out, c.AngularDamping);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<RigidBodyComponent>();
                bool ok = ReadRaw(in, c.Type) && ReadRaw(in, c.Mass)
                       && ReadRaw(in, c.UseGravity) && ReadRaw(in, c.ContinuousCollisionDetection)
                       && ReadRaw(in, c.FixedRotation)
                       && ReadRaw(in, c.Friction) && ReadRaw(in, c.Restitution)
                       && ReadRaw(in, c.LinearDamping) && ReadRaw(in, c.AngularDamping);
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (rigidbody da entidade ", entityIndex, ").");
                    return false;
                }
                if (c.Type != BodyType::Static && c.Type != BodyType::Kinematic && c.Type != BodyType::Dynamic) {
                    PRISM_CORE_ERROR("SceneSerializer: BodyType invalido na entidade ", entityIndex, ".");
                    return false;
                }
                return true;
            }
        );

        // --- ScriptComponent ---------------------------------------------------
        Register<ScriptComponent>(
            "Script",
            [](std::ofstream& out, Entity e) {
                WriteString(out, e.GetComponent<ScriptComponent>().ScriptPath);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<ScriptComponent>();
                if (!ReadString(in, c.ScriptPath)) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (script da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );

        // --- CameraComponent -----------------------------------------------------
        Register<CameraComponent>(
            "Camera",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<CameraComponent>();
                WriteRaw(out, c.ProjectionType);
                WriteRaw(out, c.FOV);
                WriteRaw(out, c.OrthoSize);
                WriteRaw(out, c.NearClip);
                WriteRaw(out, c.FarClip);
                WriteRaw(out, c.Primary);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<CameraComponent>();
                bool ok = ReadRaw(in, c.ProjectionType) && ReadRaw(in, c.FOV)
                       && ReadRaw(in, c.OrthoSize) && ReadRaw(in, c.NearClip)
                       && ReadRaw(in, c.FarClip) && ReadRaw(in, c.Primary);
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (camera da entidade ", entityIndex, ").");
                    return false;
                }
                if (c.ProjectionType != CameraProjectionType::Perspective && c.ProjectionType != CameraProjectionType::Orthographic) {
                    PRISM_CORE_ERROR("SceneSerializer: CameraProjectionType invalido na entidade ", entityIndex, ".");
                    return false;
                }
                return true;
            },
            // OnAfterAddInEditor: CameraComponent nasce com Primary=true
            // por padrao (ver Components.h) - se ja existe outra camera
            // Primary na cena, isso violaria a regra de "no maximo uma"
            // ate o usuario mexer manualmente no checkbox. Corrige aqui,
            // FORA do historico de Undo (e so um ajuste de consistencia,
            // nao uma edicao que o usuario pediu).
            [](Entity e, Scene& scene) {
                bool anyOtherPrimary = false;
                auto view = scene.GetRegistry().view<CameraComponent>();
                for (auto entityHandle : view) {
                    Entity other(entityHandle, &scene);
                    if (other != e && other.GetComponent<CameraComponent>().Primary) {
                        anyOtherPrimary = true;
                        break;
                    }
                }
                if (anyOtherPrimary)
                    e.GetComponent<CameraComponent>().Primary = false;
            }
        );

        // --- RaycastComponent ----------------------------------------------------
        Register<RaycastComponent>(
            "Raycast",
            [](std::ofstream& out, Entity e) {
                // So TargetPosition/Enabled/IgnoreParentAndSiblings sao
                // gravados - os campos de resultado
                // (Hit/HitEntity/HitPoint/HitNormal/HitDistance) sao
                // TRANSIENTES de runtime (recalculados todo frame por
                // Scene::UpdateRaycastComponents enquanto a Scene esta
                // rodando, ver Components.h) - gravar isso no mapa seria
                // so lixo que nunca reflete a realidade no proximo
                // carregamento.
                auto& c = e.GetComponent<RaycastComponent>();
                WriteRaw(out, c.TargetPosition);
                WriteRaw(out, c.Enabled);
                WriteRaw(out, c.IgnoreParentAndSiblings); // v9+ (ver kSceneFormatVersion, SceneSerializer.cpp)
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                // Hit/HitEntity/HitPoint/HitNormal/HitDistance ficam nos
                // defaults de RaycastComponent (Hit=false etc) - o
                // primeiro frame do modo Play recalcula tudo de qualquer
                // forma (ver Scene::UpdateRaycastComponents).
                auto& c = e.AddComponent<RaycastComponent>();
                if (!(ReadRaw(in, c.TargetPosition) && ReadRaw(in, c.Enabled) && ReadRaw(in, c.IgnoreParentAndSiblings))) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (raycast da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );
    }

}
