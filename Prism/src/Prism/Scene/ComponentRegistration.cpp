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
        // ModelAsset e gravado POR ULTIMO, depois de Mesh/Color - um arquivo
        // de cena salvo ANTES deste campo existir simplesmente nao tem esses
        // bytes no final; ver comentario de compatibilidade dentro do
        // deserializador abaixo sobre como isso e tratado sem quebrar cenas
        // antigas.
        Register<MeshRendererComponent>(
            "Mesh Renderer",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<MeshRendererComponent>();
                WriteRaw(out, c.Mesh);
                WriteRaw(out, c.Color);
                WriteRaw(out, c.ModelAsset); // AssetID e so um uint64_t (trivialmente copiavel) - mesmo padrao de MaterialComponent::LinkedAsset
                WriteRaw(out, c.SubmeshIndex); // v14
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
                // ModelAsset: campo ADICIONADO depois do formato original
                // (Mesh+Color) ja existir. Diferente dos campos acima, uma
                // falha de leitura aqui NAO e tratada como corrupcao fatal
                // - kSceneFormatVersion (SceneSerializer.cpp) ja garante
                // que arquivos de formato ANTIGO (sem este campo) sao
                // rejeitados antes mesmo de chegar neste deserializador,
                // entao, na pratica, qualquer arquivo que passe por aqui
                // DEVE ter os 8 bytes de ModelAsset. Mesmo assim,
                // ReadRaw falhando (EOF inesperado / arquivo truncado) so
                // deixa c.ModelAsset no valor padrao (invalido = "sem
                // modelo importado, usa a primitiva") em vez de abortar a
                // entidade inteira - degradar graciosamente para "vira
                // primitiva" e preferivel a perder a entidade toda por
                // causa so do campo mais novo.
                ReadRaw(in, c.ModelAsset);
                // SubmeshIndex (v14): mesmo tratamento gracioso de ModelAsset.
                // Valor invalido (< -1) vira -1 = malha mesclada.
                if (!ReadRaw(in, c.SubmeshIndex) || c.SubmeshIndex < -1)
                    c.SubmeshIndex = -1;
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
                if ((int)c.Shape < (int)ColliderShape::Box || (int)c.Shape > (int)ColliderShape::TriangleMesh) {
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
                auto& c = e.GetComponent<ScriptComponent>();
                WriteString(out, c.ScriptPath);
                // v16: overrides das variaveis publicas.
                uint32_t count = (uint32_t)c.Properties.size();
                WriteRaw(out, count);
                for (const ScriptPropertyValue& property : c.Properties) {
                    WriteString(out, property.Name);
                    WriteRaw(out, property.Type);
                    WriteRaw(out, property.Number);
                    WriteRaw(out, property.Bool);
                    WriteString(out, property.String);
                }
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<ScriptComponent>();
                uint32_t count = 0;
                if (!(ReadString(in, c.ScriptPath) && ReadRaw(in, count)) || count > 4096) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (script da entidade ", entityIndex, ").");
                    return false;
                }
                c.Properties.resize(count);
                for (ScriptPropertyValue& property : c.Properties) {
                    if (!(ReadString(in, property.Name) && ReadRaw(in, property.Type) && ReadRaw(in, property.Number)
                        && ReadRaw(in, property.Bool) && ReadString(in, property.String))) {
                        PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (variaveis do script da entidade ", entityIndex, ").");
                        return false;
                    }
                }
                return true;
            }
        );

        // --- NativeScriptComponent (v17) -------------------------------------------
        Register<NativeScriptComponent>(
            "Native Script",
            [](std::ofstream& out, Entity e) {
                WriteString(out, e.GetComponent<NativeScriptComponent>().ClassName);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<NativeScriptComponent>();
                if (!ReadString(in, c.ClassName)) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (script nativo da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );

        // --- ParticleEmitterComponent (v18) ----------------------------------------
        Register<ParticleEmitterComponent>(
            "Particle Emitter",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<ParticleEmitterComponent>();
                WriteRaw(out, c.Enabled);
                WriteRaw(out, c.PlayOnStart);
                WriteRaw(out, c.Loop);
                WriteRaw(out, c.Duration);
                WriteRaw(out, c.MaxParticles);
                WriteRaw(out, c.EmissionRate);
                WriteRaw(out, c.BurstCount);
                WriteRaw(out, c.LifetimeMin);
                WriteRaw(out, c.LifetimeMax);
                WriteRaw(out, c.SpeedMin);
                WriteRaw(out, c.SpeedMax);
                WriteRaw(out, c.SpreadAngle);
                WriteRaw(out, c.GravityScale);
                WriteRaw(out, c.SizeStart);
                WriteRaw(out, c.SizeEnd);
                WriteRaw(out, c.ColorStart);
                WriteRaw(out, c.ColorEnd);
                WriteRaw(out, c.Additive);
                WriteString(out, c.TexturePath);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<ParticleEmitterComponent>();
                bool ok = ReadRaw(in, c.Enabled) && ReadRaw(in, c.PlayOnStart) && ReadRaw(in, c.Loop)
                    && ReadRaw(in, c.Duration) && ReadRaw(in, c.MaxParticles) && ReadRaw(in, c.EmissionRate)
                    && ReadRaw(in, c.BurstCount) && ReadRaw(in, c.LifetimeMin) && ReadRaw(in, c.LifetimeMax)
                    && ReadRaw(in, c.SpeedMin) && ReadRaw(in, c.SpeedMax) && ReadRaw(in, c.SpreadAngle)
                    && ReadRaw(in, c.GravityScale) && ReadRaw(in, c.SizeStart) && ReadRaw(in, c.SizeEnd)
                    && ReadRaw(in, c.ColorStart) && ReadRaw(in, c.ColorEnd) && ReadRaw(in, c.Additive)
                    && ReadString(in, c.TexturePath);
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (emissor de particulas da entidade ", entityIndex, ").");
                    return false;
                }
                // Limites: um arquivo editado a mao nao pode pedir milhoes de particulas.
                c.MaxParticles = std::min<uint32_t>(std::max<uint32_t>(c.MaxParticles, 1u), 10000u);
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

        // --- AudioSourceComponent (v15) --------------------------------------
        Register<AudioSourceComponent>(
            "Audio Source",
            [](std::ofstream& out, Entity e) {
                auto& c = e.GetComponent<AudioSourceComponent>();
                WriteRaw(out, c.Clip);
                WriteRaw(out, c.Volume);
                WriteRaw(out, c.Pitch);
                WriteRaw(out, c.Loop);
                WriteRaw(out, c.PlayOnStart);
                WriteRaw(out, c.Spatial);
                WriteRaw(out, c.MinDistance);
                WriteRaw(out, c.MaxDistance);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<AudioSourceComponent>();
                if (!(ReadRaw(in, c.Clip) && ReadRaw(in, c.Volume) && ReadRaw(in, c.Pitch) && ReadRaw(in, c.Loop)
                    && ReadRaw(in, c.PlayOnStart) && ReadRaw(in, c.Spatial) && ReadRaw(in, c.MinDistance) && ReadRaw(in, c.MaxDistance))) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (audio source da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );

        // --- AudioListenerComponent (v15) ------------------------------------
        Register<AudioListenerComponent>(
            "Audio Listener",
            [](std::ofstream& out, Entity e) {
                WriteRaw(out, e.GetComponent<AudioListenerComponent>().Active);
            },
            [](std::ifstream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<AudioListenerComponent>();
                if (!ReadRaw(in, c.Active)) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (audio listener da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );
    }

}
