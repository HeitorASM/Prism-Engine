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

#include <algorithm>
#include <cmath>
#include "ComponentRegistry.h"
#include "Components.h"
#include "../Core/Log.h"

namespace Prism {

    namespace {
        // Corpo do bloco de Material (versao 4), compartilhado por MaterialComponent e pelos
        // materiais por slot do LevelMeshComponent.
        void WriteMaterialBody(std::ostream& out, const MaterialComponent& c) {
            ComponentRegistry::WriteString(out, c.AlbedoPath);
            ComponentRegistry::WriteString(out, c.NormalPath);
            ComponentRegistry::WriteString(out, c.RoughnessMetallicPath);
            ComponentRegistry::WriteRaw(out, c.AlbedoTint);
            ComponentRegistry::WriteRaw(out, c.RoughnessFactor);
            ComponentRegistry::WriteRaw(out, c.MetallicFactor);
            ComponentRegistry::WriteRaw(out, c.LinkedAsset);
            ComponentRegistry::WriteRaw(out, c.BlendMode);
            ComponentRegistry::WriteRaw(out, c.Opacity);
            ComponentRegistry::WriteRaw(out, c.AlphaCutoff);
            ComponentRegistry::WriteRaw(out, c.UVTiling);
            ComponentRegistry::WriteRaw(out, c.UVOffset);
            ComponentRegistry::WriteRaw(out, c.EmissiveColor);
            ComponentRegistry::WriteRaw(out, c.EmissiveIntensity);
            ComponentRegistry::WriteRaw(out, c.DoubleSided);
            ComponentRegistry::WriteString(out, c.ShaderPath);
            ComponentRegistry::WriteRaw(out, (uint32_t)c.ShaderParams.size());
            for (const auto& param : c.ShaderParams) {
                ComponentRegistry::WriteString(out, param.Name);
                ComponentRegistry::WriteRaw(out, param.Value);
            }
        }

        // 'version' = versao do bloco de Material que escreveu os dados (1..4).
        bool ReadMaterialBody(std::istream& in, MaterialComponent& c, uint32_t version) {
            using R = ComponentRegistry;
            bool ok = R::ReadString(in, c.AlbedoPath) && R::ReadString(in, c.NormalPath)
                   && R::ReadString(in, c.RoughnessMetallicPath) && R::ReadRaw(in, c.AlbedoTint)
                   && R::ReadRaw(in, c.RoughnessFactor) && R::ReadRaw(in, c.MetallicFactor)
                   && R::ReadRaw(in, c.LinkedAsset);
            if (ok && version >= 2) {
                ok = R::ReadRaw(in, c.BlendMode) && R::ReadRaw(in, c.Opacity) && R::ReadRaw(in, c.AlphaCutoff)
                  && R::ReadRaw(in, c.UVTiling) && R::ReadRaw(in, c.UVOffset) && R::ReadRaw(in, c.EmissiveColor)
                  && R::ReadRaw(in, c.EmissiveIntensity) && R::ReadRaw(in, c.DoubleSided);
            }
            if (ok && version >= 3)
                ok = R::ReadString(in, c.ShaderPath);
            if (ok && version >= 4) {
                uint32_t count = 0;
                ok = R::ReadRaw(in, count) && count <= 64;
                for (uint32_t i = 0; ok && i < count; i++) {
                    MaterialShaderParam param;
                    ok = R::ReadString(in, param.Name) && R::ReadRaw(in, param.Value);
                    if (ok) c.ShaderParams.push_back(std::move(param));
                }
            }
            if (ok)
                SanitizeMaterialSurface(c);
            return ok;
        }
    }

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
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<MeshRendererComponent>();
                WriteRaw(out, c.Mesh);
                WriteRaw(out, c.Color);
                WriteRaw(out, c.ModelAsset); // AssetID e so um uint64_t (trivialmente copiavel) - mesmo padrao de MaterialComponent::LinkedAsset
                WriteRaw(out, c.SubmeshIndex); // v14
                WriteRaw(out, c.Layers);       // v2 do bloco
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
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
                // Bloco v2: camadas de visibilidade (blocos v1 e arquivos antigos: camada 1).
                if (ComponentRegistry::CurrentBlockVersion() >= 2 && !ReadRaw(in, c.Layers))
                    c.Layers = Culling::kDefaultLayer;
                return true;
            }
        );
        s_Registry.back().Version = 2;

        // --- MaterialComponent -------------------------------------------------
        // Campos (ver WriteMaterialBody): v1 base, v2 superficie, v3 shader, v4 parametros do shader.
        Register<MaterialComponent>(
            "Material",
            [](std::ostream& out, Entity e) {
                WriteMaterialBody(out, e.GetComponent<MaterialComponent>());
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<MaterialComponent>();
                if (!ReadMaterialBody(in, c, ComponentRegistry::CurrentBlockVersion())) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (material da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );
        s_Registry.back().Version = 4;

        // --- LevelMeshComponent (Level Builder) ------------------------------------
        // v1: Layers, CastShadows, contagem de materiais (1..64) + corpo de cada material (v4), EditableMesh.
        Register<LevelMeshComponent>(
            "Level Mesh",
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<LevelMeshComponent>();
                WriteRaw(out, c.Layers);
                WriteRaw(out, (uint8_t)(c.CastShadows ? 1 : 0));
                const uint32_t count = (uint32_t)std::min<size_t>(std::max<size_t>(c.Materials.size(), 1), 64);
                WriteRaw(out, count);
                for (uint32_t i = 0; i < count; i++)
                    WriteMaterialBody(out, c.MaterialForSlot(i));
                c.Mesh.Write(out);
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<LevelMeshComponent>();
                uint8_t cast = 1;
                uint32_t count = 0;
                bool ok = ReadRaw(in, c.Layers) && ReadRaw(in, cast) && ReadRaw(in, count) && count >= 1 && count <= 64;
                c.Materials.clear();
                for (uint32_t i = 0; ok && i < count; i++) {
                    MaterialComponent m;
                    ok = ReadMaterialBody(in, m, 4);
                    if (ok) c.Materials.push_back(std::move(m));
                }
                ok = ok && Level::EditableMesh::Read(in, c.Mesh) && c.Mesh.IsValid();
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (level mesh da entidade ", entityIndex, ").");
                    return false;
                }
                c.CastShadows = cast != 0;
                c.Touch();
                return true;
            }
        );
        s_Registry.back().Version = 1;

        // --- LightComponent --------------------------------------------------
        Register<LightComponent>(
            "Light",
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<LightComponent>();
                WriteRaw(out, c.Type);
                WriteRaw(out, c.Color);
                WriteRaw(out, c.Intensity);
                WriteRaw(out, c.Range);
                WriteRaw(out, c.SpotAngle);
                WriteRaw(out, c.InnerSpotAngle);
                WriteRaw(out, c.CastShadows);
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
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
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<ColliderComponent>();
                WriteRaw(out, c.Shape);
                WriteRaw(out, c.Size);
                WriteRaw(out, c.IsTrigger);
                WriteRaw(out, c.Layer); // v2 do bloco
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<ColliderComponent>();
                bool ok = ReadRaw(in, c.Shape) && ReadRaw(in, c.Size) && ReadRaw(in, c.IsTrigger);
                // Bloco v2: camada de fisica (antes: camada 0). Valor fora de 0..31 vira 0.
                if (ok && ComponentRegistry::CurrentBlockVersion() >= 2) {
                    ok = ReadRaw(in, c.Layer);
                    if (ok && (c.Layer < 0 || c.Layer >= Physics::kLayerCount))
                        c.Layer = 0;
                }
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
        s_Registry.back().Version = 2;

        // --- RigidBodyComponent ------------------------------------------------
        Register<RigidBodyComponent>(
            "Rigid Body",
            [](std::ostream& out, Entity e) {
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
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
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
            [](std::ostream& out, Entity e) {
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
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
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
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<NativeScriptComponent>();
                WriteString(out, c.ClassName);
                // Bloco v2: overrides das variaveis publicas (mesmo formato do Script Lua).
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
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<NativeScriptComponent>();
                if (!ReadString(in, c.ClassName)) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (script nativo da entidade ", entityIndex, ").");
                    return false;
                }
                if (ComponentRegistry::CurrentBlockVersion() >= 2) {
                    uint32_t count = 0;
                    if (!ReadRaw(in, count) || count > 4096) {
                        PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (variaveis do script nativo da entidade ", entityIndex, ").");
                        return false;
                    }
                    c.Properties.resize(count);
                    for (ScriptPropertyValue& property : c.Properties) {
                        if (!(ReadString(in, property.Name) && ReadRaw(in, property.Type) && ReadRaw(in, property.Number)
                            && ReadRaw(in, property.Bool) && ReadString(in, property.String))) {
                            PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (variaveis do script nativo da entidade ", entityIndex, ").");
                            return false;
                        }
                    }
                }
                return true;
            }
        );
        // v2 do bloco: overrides de variaveis publicas no fim.
        s_Registry.back().Version = 2;

        // --- ParticleEmitterComponent (v18) ----------------------------------------
        Register<ParticleEmitterComponent>(
            "Particle Emitter",
            [](std::ostream& out, Entity e) {
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
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
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
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<CameraComponent>();
                WriteRaw(out, c.ProjectionType);
                WriteRaw(out, c.FOV);
                WriteRaw(out, c.OrthoSize);
                WriteRaw(out, c.NearClip);
                WriteRaw(out, c.FarClip);
                WriteRaw(out, c.Primary);
                // v20: opcoes de render da camera.
                WriteRaw(out, c.ClearMode);
                WriteRaw(out, c.ClearColor);
                WriteRaw(out, c.Exposure);
                WriteRaw(out, c.SSAO);
                // v3 do bloco: pos-processamento (sempre NO FIM).
                WriteRaw(out, c.Bloom);
                WriteRaw(out, c.BloomThreshold);
                WriteRaw(out, c.BloomIntensity);
                WriteRaw(out, c.FXAA);
                WriteRaw(out, c.Saturation);
                WriteRaw(out, c.Contrast);
                // v4 do bloco: camadas que a camera enxerga (sempre NO FIM).
                WriteRaw(out, c.CullMask);
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<CameraComponent>();
                bool ok = ReadRaw(in, c.ProjectionType) && ReadRaw(in, c.FOV)
                       && ReadRaw(in, c.OrthoSize) && ReadRaw(in, c.NearClip)
                       && ReadRaw(in, c.FarClip) && ReadRaw(in, c.Primary);
                // Bloco v2: opcoes de render da camera (blocos v1 nao as tem e
                // ficam com os padroes do component).
                if (ok && ComponentRegistry::CurrentBlockVersion() >= 2) {
                    ok = ReadRaw(in, c.ClearMode) && ReadRaw(in, c.ClearColor)
                      && ReadRaw(in, c.Exposure) && ReadRaw(in, c.SSAO);
                }
                // Bloco v3: pos-processamento (blocos v1/v2 ficam com os padroes).
                if (ok && ComponentRegistry::CurrentBlockVersion() >= 3) {
                    ok = ReadRaw(in, c.Bloom) && ReadRaw(in, c.BloomThreshold)
                      && ReadRaw(in, c.BloomIntensity) && ReadRaw(in, c.FXAA)
                      && ReadRaw(in, c.Saturation) && ReadRaw(in, c.Contrast);
                }
                // Bloco v4: mascara de camadas (blocos antigos: enxerga todas).
                if (ok && ComponentRegistry::CurrentBlockVersion() >= 4) {
                    ok = ReadRaw(in, c.CullMask);
                }
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (camera da entidade ", entityIndex, ").");
                    return false;
                }
                if (c.ProjectionType != CameraProjectionType::Perspective && c.ProjectionType != CameraProjectionType::Orthographic) {
                    PRISM_CORE_ERROR("SceneSerializer: CameraProjectionType invalido na entidade ", entityIndex, ".");
                    return false;
                }
                if (c.ClearMode != CameraClearMode::Skybox && c.ClearMode != CameraClearMode::SolidColor) {
                    PRISM_CORE_ERROR("SceneSerializer: CameraClearMode invalido na entidade ", entityIndex, ".");
                    return false;
                }
                // Valores de render fora de faixa nao sao fatais: o Renderer
                // tambem limita, mas corrigir aqui mantem o inspetor coerente.
                if (!std::isfinite(c.Exposure) || c.Exposure <= 0.0f) c.Exposure = 1.0f;
                if (!std::isfinite(c.BloomThreshold) || c.BloomThreshold < 0.0f) c.BloomThreshold = 1.0f;
                if (!std::isfinite(c.BloomIntensity) || c.BloomIntensity < 0.0f) c.BloomIntensity = 0.15f;
                if (!std::isfinite(c.Saturation) || c.Saturation < 0.0f) c.Saturation = 1.0f;
                if (!std::isfinite(c.Contrast) || c.Contrast <= 0.0f) c.Contrast = 1.0f;
                for (int i = 0; i < 3; i++)
                    if (!std::isfinite(c.ClearColor[i])) c.ClearColor[i] = 0.0f;
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

        // v2 do bloco: ClearMode/ClearColor/Exposure/SSAO no fim; v3: pos-
        // processamento (Bloom.../Contrast). Em arquivos LEGADOS (v19-v20, sem
        // blocos) o layout v2 vale a partir do formato equivalente 20 e a v3
        // nunca existiu neles.
        s_Registry.back().Version = 4;
        s_Registry.back().LegacyVersion = [](uint32_t sceneFormat) -> uint32_t { return sceneFormat >= 20 ? 2u : 1u; };

        // --- RaycastComponent ----------------------------------------------------
        Register<RaycastComponent>(
            "Raycast",
            [](std::ostream& out, Entity e) {
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
                WriteRaw(out, c.LayerMask);               // v2 do bloco
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                // Hit/HitEntity/HitPoint/HitNormal/HitDistance ficam nos
                // defaults de RaycastComponent (Hit=false etc) - o
                // primeiro frame do modo Play recalcula tudo de qualquer
                // forma (ver Scene::UpdateRaycastComponents).
                auto& c = e.AddComponent<RaycastComponent>();
                if (!(ReadRaw(in, c.TargetPosition) && ReadRaw(in, c.Enabled) && ReadRaw(in, c.IgnoreParentAndSiblings))) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (raycast da entidade ", entityIndex, ").");
                    return false;
                }
                // Bloco v2: mascara de camadas de fisica (antes: todas).
                if (ComponentRegistry::CurrentBlockVersion() >= 2 && !ReadRaw(in, c.LayerMask))
                    c.LayerMask = Physics::kAllLayers;
                return true;
            }
        );
        s_Registry.back().Version = 2;

        // --- AudioSourceComponent (v15) --------------------------------------
        Register<AudioSourceComponent>(
            "Audio Source",
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<AudioSourceComponent>();
                WriteRaw(out, c.Clip);
                WriteRaw(out, c.Volume);
                WriteRaw(out, c.Pitch);
                WriteRaw(out, c.Loop);
                WriteRaw(out, c.PlayOnStart);
                WriteRaw(out, c.Spatial);
                WriteRaw(out, c.MinDistance);
                WriteRaw(out, c.MaxDistance);
                WriteRaw(out, (int32_t)c.Bus); // bloco v2
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<AudioSourceComponent>();
                bool ok = ReadRaw(in, c.Clip) && ReadRaw(in, c.Volume) && ReadRaw(in, c.Pitch) && ReadRaw(in, c.Loop)
                       && ReadRaw(in, c.PlayOnStart) && ReadRaw(in, c.Spatial) && ReadRaw(in, c.MinDistance) && ReadRaw(in, c.MaxDistance);
                // Bloco v2: bus de audio (blocos v1 ficam em SFX).
                if (ok && ComponentRegistry::CurrentBlockVersion() >= 2) {
                    int32_t bus = 0;
                    ok = ReadRaw(in, bus);
                    c.Bus = IsValidAudioBus(bus) ? (AudioBus)bus : AudioBus::SFX;
                }
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (audio source da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );
        // v2 do bloco: campo Bus no fim. Cenas legadas (sem blocos) nunca o tiveram.
        s_Registry.back().Version = 2;

        // --- AudioListenerComponent (v15) ------------------------------------
        Register<AudioListenerComponent>(
            "Audio Listener",
            [](std::ostream& out, Entity e) {
                WriteRaw(out, e.GetComponent<AudioListenerComponent>().Active);
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<AudioListenerComponent>();
                if (!ReadRaw(in, c.Active)) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (audio listener da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );

        // --- EnvironmentComponent (v20) --------------------------------------
        // Ambiente da cena: luz ambiente + skybox. Vale o PRIMEIRO da cena;
        // um segundo e inutil e o editor avisa (ver PropertiesPanel_Environment.cpp).
        Register<EnvironmentComponent>(
            "Environment",
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<EnvironmentComponent>();
                WriteRaw(out, c.Ambient);
                WriteRaw(out, c.Zenith);
                WriteRaw(out, c.Horizon);
                WriteRaw(out, c.Ground);
                WriteRaw(out, c.Sky);
                WriteRaw(out, c.SkyColor);
                WriteRaw(out, c.SkyTexture); // AssetID e so um uint64_t - mesmo padrao de MeshRendererComponent::ModelAsset
                WriteRaw(out, c.SkyRotation);
                WriteRaw(out, c.SkyIntensity);
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<EnvironmentComponent>();
                bool ok = ReadRaw(in, c.Ambient) && ReadRaw(in, c.Zenith) && ReadRaw(in, c.Horizon)
                       && ReadRaw(in, c.Ground) && ReadRaw(in, c.Sky) && ReadRaw(in, c.SkyColor)
                       && ReadRaw(in, c.SkyTexture) && ReadRaw(in, c.SkyRotation) && ReadRaw(in, c.SkyIntensity);
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (ambiente da entidade ", entityIndex, ").");
                    return false;
                }
                if (c.Sky != SkyMode::Solid && c.Sky != SkyMode::Gradient && c.Sky != SkyMode::Panorama) {
                    PRISM_CORE_ERROR("SceneSerializer: SkyMode invalido na entidade ", entityIndex, ".");
                    return false;
                }
                return true;
            }
        );
        s_Registry.back().AddedInSceneFormat = 20; // nao existe em mapas v19 / prefabs v7

        // --- AnimatorComponent (bloco v2: v1 = clipe; v2 = grafo/eventos/root motion) ------------------------------------
        // Animacao esqueletica / de nos de um modelo importado. O estado de
        // execucao (AnimatorComponent::Runtime) nao e salvo.
        Register<AnimatorComponent>(
            "Animator",
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<AnimatorComponent>();
                WriteRaw(out, c.ModelAsset);
                WriteString(out, c.Clip);
                WriteRaw(out, c.PlayOnStart);
                WriteRaw(out, c.Loop);
                WriteRaw(out, c.Speed);
                // v2: maquina de estados, eventos, root motion
                WriteRaw(out, c.RootMotion);
                WriteRaw(out, c.Graph.EntryState);
                WriteRaw(out, (uint32_t)c.Graph.Parameters.size());
                for (const AnimParameter& p : c.Graph.Parameters) {
                    WriteString(out, p.Name);
                    WriteRaw(out, (uint8_t)p.Type);
                    WriteRaw(out, p.Default);
                }
                WriteRaw(out, (uint32_t)c.Graph.States.size());
                for (const AnimState& st : c.Graph.States) {
                    WriteString(out, st.Name);
                    WriteString(out, st.Clip);
                    WriteRaw(out, st.Loop);
                    WriteRaw(out, st.Speed);
                    WriteRaw(out, st.EditorPos);
                }
                WriteRaw(out, (uint32_t)c.Graph.Transitions.size());
                for (const AnimTransition& t : c.Graph.Transitions) {
                    WriteRaw(out, t.From);
                    WriteRaw(out, t.To);
                    WriteRaw(out, t.Fade);
                    WriteRaw(out, t.HasExitTime);
                    WriteRaw(out, t.ExitTime);
                    WriteRaw(out, (uint32_t)t.Conditions.size());
                    for (const AnimCondition& cond : t.Conditions) {
                        WriteString(out, cond.Param);
                        WriteRaw(out, (uint8_t)cond.Op);
                        WriteRaw(out, cond.Value);
                    }
                }
                WriteRaw(out, (uint32_t)c.Events.size());
                for (const AnimEvent& ev : c.Events) {
                    WriteString(out, ev.Clip);
                    WriteRaw(out, ev.Time);
                    WriteString(out, ev.Name);
                }
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<AnimatorComponent>();
                bool ok = ReadRaw(in, c.ModelAsset) && ReadString(in, c.Clip) && ReadRaw(in, c.PlayOnStart)
                       && ReadRaw(in, c.Loop) && ReadRaw(in, c.Speed);
                if (!ok) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (animator da entidade ", entityIndex, ").");
                    return false;
                }
                if (!std::isfinite(c.Speed))
                    c.Speed = 1.0f;
                c.Speed = std::clamp(c.Speed, 0.0f, 20.0f);
                if (ComponentRegistry::CurrentBlockVersion() >= 2) {
                    constexpr uint32_t kMax = 4096;
                    auto fail = [&]() {
                        PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (grafo do animator da entidade ", entityIndex, ").");
                        return false;
                    };
                    uint32_t n = 0;
                    uint8_t b = 0;
                    if (!ReadRaw(in, c.RootMotion) || !ReadRaw(in, c.Graph.EntryState) || !ReadRaw(in, n) || n > kMax)
                        return fail();
                    c.Graph.Parameters.resize(n);
                    for (AnimParameter& p : c.Graph.Parameters) {
                        if (!ReadString(in, p.Name) || !ReadRaw(in, b) || b > 2 || !ReadRaw(in, p.Default))
                            return fail();
                        p.Type = (AnimParamType)b;
                    }
                    if (!ReadRaw(in, n) || n > kMax)
                        return fail();
                    c.Graph.States.resize(n);
                    for (AnimState& st : c.Graph.States)
                        if (!ReadString(in, st.Name) || !ReadString(in, st.Clip) || !ReadRaw(in, st.Loop)
                            || !ReadRaw(in, st.Speed) || !ReadRaw(in, st.EditorPos))
                            return fail();
                    if (!ReadRaw(in, n) || n > kMax)
                        return fail();
                    c.Graph.Transitions.resize(n);
                    for (AnimTransition& t : c.Graph.Transitions) {
                        uint32_t cn = 0;
                        if (!ReadRaw(in, t.From) || !ReadRaw(in, t.To) || !ReadRaw(in, t.Fade) || !ReadRaw(in, t.HasExitTime)
                            || !ReadRaw(in, t.ExitTime) || !ReadRaw(in, cn) || cn > kMax)
                            return fail();
                        t.Conditions.resize(cn);
                        for (AnimCondition& cond : t.Conditions) {
                            if (!ReadString(in, cond.Param) || !ReadRaw(in, b) || b > 6 || !ReadRaw(in, cond.Value))
                                return fail();
                            cond.Op = (AnimCompare)b;
                        }
                    }
                    if (!ReadRaw(in, n) || n > kMax)
                        return fail();
                    c.Events.resize(n);
                    for (AnimEvent& ev : c.Events)
                        if (!ReadString(in, ev.Clip) || !ReadRaw(in, ev.Time) || !ReadString(in, ev.Name))
                            return fail();
                    // Sanidade: indices e floats.
                    const int stateCount = (int)c.Graph.States.size();
                    if (c.Graph.EntryState < 0 || c.Graph.EntryState >= std::max(stateCount, 1))
                        c.Graph.EntryState = 0;
                    for (AnimTransition& t : c.Graph.Transitions) {
                        if (t.From < -1 || t.From >= stateCount) t.From = -1;
                        if (t.To < 0 || t.To >= stateCount) t.To = 0;
                        if (!std::isfinite(t.Fade)) t.Fade = 0.2f;
                        t.Fade = std::clamp(t.Fade, 0.0f, 10.0f);
                        if (!std::isfinite(t.ExitTime)) t.ExitTime = 0.9f;
                    }
                    for (AnimState& st : c.Graph.States)
                        if (!std::isfinite(st.Speed)) st.Speed = 1.0f;
                    for (AnimEvent& ev : c.Events)
                        if (!std::isfinite(ev.Time) || ev.Time < 0.0f) ev.Time = 0.0f;
                }
                return true;
            }
        );
        s_Registry.back().Version = 2;
        s_Registry.back().AddedInSceneFormat = 22; // so existe no formato em blocos (v21+)

        // --- UIDocumentComponent (.prismui) ----------------------------------------
        Register<UIDocumentComponent>(
            "UI Document",
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<UIDocumentComponent>();
                WriteString(out, c.Path);
                WriteRaw(out, c.Visible);
                WriteRaw(out, c.Layer);
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<UIDocumentComponent>();
                if (!(ReadString(in, c.Path) && ReadRaw(in, c.Visible) && ReadRaw(in, c.Layer))) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (documento de UI da entidade ", entityIndex, ").");
                    return false;
                }
                return true;
            }
        );
        s_Registry.back().AddedInSceneFormat = 22; // so existe no formato em blocos (v21+)

        // --- JointComponent --------------------------------------------------------
        Register<JointComponent>(
            "Joint",
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<JointComponent>();
                WriteRaw(out, c.Type);
                WriteString(out, c.ConnectedEntity);
                WriteRaw(out, c.Anchor);
                WriteRaw(out, c.Axis);
                WriteRaw(out, c.UseLimits);
                WriteRaw(out, c.LimitMin);
                WriteRaw(out, c.LimitMax);
                WriteRaw(out, c.UseSpring);
                WriteRaw(out, c.SpringFrequency);
                WriteRaw(out, c.SpringDamping);
                WriteRaw(out, c.CollideConnected);
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<JointComponent>();
                bool ok = ReadRaw(in, c.Type) && ReadString(in, c.ConnectedEntity) && ReadRaw(in, c.Anchor)
                       && ReadRaw(in, c.Axis) && ReadRaw(in, c.UseLimits) && ReadRaw(in, c.LimitMin)
                       && ReadRaw(in, c.LimitMax) && ReadRaw(in, c.UseSpring) && ReadRaw(in, c.SpringFrequency)
                       && ReadRaw(in, c.SpringDamping) && ReadRaw(in, c.CollideConnected);
                if (!ok || (int)c.Type < (int)JointType::Fixed || (int)c.Type > (int)JointType::Point) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (junta da entidade ", entityIndex, ").");
                    return false;
                }
                auto finiteOr = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
                c.LimitMin = finiteOr(c.LimitMin, -45.0f);
                c.LimitMax = finiteOr(c.LimitMax, 45.0f);
                c.SpringFrequency = finiteOr(c.SpringFrequency, 2.0f);
                c.SpringDamping = finiteOr(c.SpringDamping, 0.5f);
                return true;
            }
        );
        s_Registry.back().AddedInSceneFormat = 22; // so existe no formato em blocos (v21+)

        // --- CharacterControllerComponent -------------------------------------------
        Register<CharacterControllerComponent>(
            "Character Controller",
            [](std::ostream& out, Entity e) {
                auto& c = e.GetComponent<CharacterControllerComponent>();
                WriteRaw(out, c.Radius);
                WriteRaw(out, c.Height);
                WriteRaw(out, c.StepHeight);
                WriteRaw(out, c.MaxSlopeAngle);
                WriteRaw(out, c.Mass);
                WriteRaw(out, c.GravityScale);
                WriteRaw(out, c.StickToFloor);
            },
            [](std::istream& in, Entity e, uint32_t entityIndex) -> bool {
                auto& c = e.AddComponent<CharacterControllerComponent>();
                if (!(ReadRaw(in, c.Radius) && ReadRaw(in, c.Height) && ReadRaw(in, c.StepHeight)
                      && ReadRaw(in, c.MaxSlopeAngle) && ReadRaw(in, c.Mass) && ReadRaw(in, c.GravityScale)
                      && ReadRaw(in, c.StickToFloor))) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (character controller da entidade ", entityIndex, ").");
                    return false;
                }
                auto finiteOr = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
                c.Radius = std::min(std::max(finiteOr(c.Radius, 0.35f), 0.05f), 50.0f);
                c.Height = std::min(std::max(finiteOr(c.Height, 1.8f), c.Radius * 2.0f), 200.0f);
                c.StepHeight = std::min(std::max(finiteOr(c.StepHeight, 0.4f), 0.0f), c.Height);
                c.MaxSlopeAngle = std::min(std::max(finiteOr(c.MaxSlopeAngle, 50.0f), 0.0f), 89.0f);
                c.Mass = std::max(finiteOr(c.Mass, 70.0f), 0.001f);
                c.GravityScale = finiteOr(c.GravityScale, 1.0f);
                return true;
            }
        );
        s_Registry.back().AddedInSceneFormat = 22;
    }

}
