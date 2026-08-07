#include "SceneSerializer.h"
#include "Scene.h"
#include "Entity.h"
#include "../Core/Log.h"

#include <fstream>
#include <cstdint>
#include <cstring>

namespace Prism {

    // Incrementar sempre que o layout binario mudar de forma incompativel
    // (novo component obrigatorio, campo removido, etc). Deserialize()
    // recusa arquivos com versao diferente desta - ver comentario no .h.
    //
    // v1 -> v2: adicionados LightComponent, ColliderComponent,
    // RigidBodyComponent, ScriptComponent (todos opcionais, mesmo padrao de
    // flag-de-presenca que MeshRendererComponent ja usava). Arquivos v1 nao
    // sao lidos por este parser (ver Deserialize) - projetos criados antes
    // desta mudanca precisam ser resalvos uma vez.
    static constexpr uint32_t kSceneFormatVersion = 2;
    static constexpr char kMagic[4] = { 'P', 'R', 'S', 'M' };

    SceneSerializer::SceneSerializer(Ref<Scene> scene) : m_Scene(scene) {}

    // --- helpers de escrita/leitura binaria -----------------------------
    // Pequenos o suficiente para nao valer a pena um header separado ainda;
    // se outros serializers binarios aparecerem (ex: para Materiais), vale
    // extrair isso para um BinaryWriter/BinaryReader compartilhado.

    static void WriteString(std::ofstream& out, const std::string& str) {
        uint32_t length = (uint32_t)str.size();
        out.write(reinterpret_cast<const char*>(&length), sizeof(length));
        if (length > 0)
            out.write(str.data(), length);
    }

    static bool ReadString(std::ifstream& in, std::string& outStr) {
        uint32_t length = 0;
        in.read(reinterpret_cast<char*>(&length), sizeof(length));
        if (!in) return false;

        // Sanidade: um nome de entidade/cena gigante e sinal de arquivo
        // corrompido ou lido com offset errado - nao alocamos as cegas.
        if (length > (16 * 1024 * 1024)) {
            PRISM_CORE_ERROR("SceneSerializer: string absurdamente grande (", length, " bytes) - arquivo provavelmente corrompido.");
            return false;
        }

        outStr.resize(length);
        if (length > 0)
            in.read(outStr.data(), length);
        return (bool)in;
    }

    template<typename T>
    static void WriteRaw(std::ofstream& out, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "WriteRaw so deve ser usado com tipos POD (floats, ints, glm::vec3, etc).");
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template<typename T>
    static bool ReadRaw(std::ifstream& in, T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "ReadRaw so deve ser usado com tipos POD (floats, ints, glm::vec3, etc).");
        in.read(reinterpret_cast<char*>(&value), sizeof(T));
        return (bool)in;
    }

    // --- Serialize --------------------------------------------------------

    bool SceneSerializer::Serialize(const std::filesystem::path& filepath) {
        std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            PRISM_CORE_ERROR("SceneSerializer: nao foi possivel criar o arquivo de cena: ", filepath.string());
            return false;
        }

        out.write(kMagic, sizeof(kMagic));
        WriteRaw(out, kSceneFormatVersion);
        WriteString(out, m_Scene->GetName());

        // Conta entidades primeiro (ForEachEntity nao expoe o total
        // diretamente) para escrever o cabecalho antes dos dados.
        uint32_t entityCount = 0;
        m_Scene->ForEachEntity([&](entt::entity, TagComponent&) { entityCount++; });
        WriteRaw(out, entityCount);

        bool writeFailed = false;
        m_Scene->ForEachEntity([&](entt::entity handle, TagComponent& tag) {
            Entity entity(handle, m_Scene.get());

            WriteString(out, tag.Tag);

            // TransformComponent: toda entidade tem (garantido por
            // Scene::CreateEntity) - nao precisa de flag de presenca.
            auto& transform = entity.GetComponent<TransformComponent>();
            WriteRaw(out, transform.Translation);
            WriteRaw(out, transform.Rotation);
            WriteRaw(out, transform.Scale);

            // MeshRendererComponent e opcional - flag de 1 byte diz se os
            // campos seguintes existem no stream ou nao.
            bool hasMesh = entity.HasComponent<MeshRendererComponent>();
            WriteRaw(out, hasMesh);
            if (hasMesh) {
                auto& meshRenderer = entity.GetComponent<MeshRendererComponent>();
                WriteRaw(out, meshRenderer.Mesh);
                WriteRaw(out, meshRenderer.Color);
            }

            // Os quatro components abaixo seguem o mesmo padrao de flag de
            // presenca + campos - adicionados na v2 do formato (ver
            // kSceneFormatVersion).
            bool hasLight = entity.HasComponent<LightComponent>();
            WriteRaw(out, hasLight);
            if (hasLight) {
                auto& light = entity.GetComponent<LightComponent>();
                WriteRaw(out, light.Type);
                WriteRaw(out, light.Color);
                WriteRaw(out, light.Intensity);
                WriteRaw(out, light.Range);
                WriteRaw(out, light.SpotAngle);
            }

            bool hasCollider = entity.HasComponent<ColliderComponent>();
            WriteRaw(out, hasCollider);
            if (hasCollider) {
                auto& collider = entity.GetComponent<ColliderComponent>();
                WriteRaw(out, collider.Shape);
                WriteRaw(out, collider.Size);
                WriteRaw(out, collider.IsTrigger);
            }

            bool hasRigidBody = entity.HasComponent<RigidBodyComponent>();
            WriteRaw(out, hasRigidBody);
            if (hasRigidBody) {
                auto& rigidBody = entity.GetComponent<RigidBodyComponent>();
                WriteRaw(out, rigidBody.Type);
                WriteRaw(out, rigidBody.Mass);
                WriteRaw(out, rigidBody.UseGravity);
                WriteRaw(out, rigidBody.ContinuousCollisionDetection);
            }

            bool hasScript = entity.HasComponent<ScriptComponent>();
            WriteRaw(out, hasScript);
            if (hasScript) {
                auto& script = entity.GetComponent<ScriptComponent>();
                WriteString(out, script.ScriptPath);
            }

            if (!out) writeFailed = true;
        });

        if (writeFailed) {
            PRISM_CORE_ERROR("SceneSerializer: falha ao escrever dados da cena em: ", filepath.string());
            return false;
        }

        PRISM_CORE_INFO("Cena '", m_Scene->GetName(), "' salva em: ", filepath.string());
        return true;
    }

    // --- Deserialize --------------------------------------------------------

    bool SceneSerializer::Deserialize(const std::filesystem::path& filepath) {
        std::ifstream in(filepath, std::ios::binary);
        if (!in.is_open()) {
            PRISM_CORE_ERROR("SceneSerializer: nao foi possivel abrir o arquivo de cena: ", filepath.string());
            return false;
        }

        char magic[4];
        in.read(magic, sizeof(magic));
        if (!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
            PRISM_CORE_ERROR("SceneSerializer: '", filepath.string(), "' nao e um arquivo de cena Prism valido (magic incorreto).");
            return false;
        }

        uint32_t version = 0;
        if (!ReadRaw(in, version)) {
            PRISM_CORE_ERROR("SceneSerializer: falha ao ler versao do arquivo: ", filepath.string());
            return false;
        }
        if (version != kSceneFormatVersion) {
            // Sem migracao entre versoes por enquanto (ver comentario no
            // .h) - melhor recusar claramente do que interpretar bytes com
            // o layout errado e corromper a cena silenciosamente.
            PRISM_CORE_ERROR("SceneSerializer: versao de formato incompativel em '", filepath.string(),
                "' (arquivo=v", version, ", esperado=v", kSceneFormatVersion, ").");
            return false;
        }

        std::string sceneName;
        if (!ReadString(in, sceneName)) {
            PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (nome): ", filepath.string());
            return false;
        }

        uint32_t entityCount = 0;
        if (!ReadRaw(in, entityCount)) {
            PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (contagem de entidades): ", filepath.string());
            return false;
        }

        // Sanidade: um arquivo truncado/corrompido pode fazer este campo
        // vir com um valor absurdo (bytes de lixo interpretados como
        // uint32_t). Sem este teto, o loop abaixo tentaria criar milhoes
        // de entidades e cada ReadString() dentro dele tentaria alocar
        // memoria para dados que nao existem no arquivo - e exatamente
        // esse tipo de leitura descontrolada que causa abort() por
        // corrupcao de heap no CRT em modo Debug.
        constexpr uint32_t kMaxReasonableEntityCount = 1'000'000;
        if (entityCount > kMaxReasonableEntityCount) {
            PRISM_CORE_ERROR("SceneSerializer: contagem de entidades absurda (", entityCount,
                ") em '", filepath.string(), "' - arquivo provavelmente corrompido/truncado.");
            return false;
        }

        // So substituimos o estado da Scene depois de ler tudo com sucesso
        // (ver loop abaixo) - assim uma leitura que falha no meio nao deixa
        // a Scene ativa pela metade.
        Ref<Scene> loaded = Scene::Create(sceneName);

        for (uint32_t i = 0; i < entityCount; i++) {
            std::string tag;
            if (!ReadString(in, tag)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (tag da entidade ", i, "): ", filepath.string());
                return false;
            }

            Entity entity = loaded->CreateEntity(tag);
            auto& transform = entity.GetComponent<TransformComponent>();

            bool ok = ReadRaw(in, transform.Translation)
                   && ReadRaw(in, transform.Rotation)
                   && ReadRaw(in, transform.Scale);
            if (!ok) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (transform da entidade ", i, "): ", filepath.string());
                return false;
            }

            bool hasMesh = false;
            if (!ReadRaw(in, hasMesh)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (flag de mesh da entidade ", i, "): ", filepath.string());
                return false;
            }
            if (hasMesh) {
                auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
                bool meshOk = ReadRaw(in, meshRenderer.Mesh) && ReadRaw(in, meshRenderer.Color);
                if (!meshOk) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (mesh renderer da entidade ", i, "): ", filepath.string());
                    return false;
                }
                // Validacao do enum: um valor fora do range conhecido e
                // sinal de arquivo corrompido (ou de uma versao futura do
                // formato com mais primitivas). Sem checar isto, um
                // PrimitiveMesh invalido seguiria para dentro da Scene e
                // so falharia (silenciosamente, sem desenhar nada) la na
                // frente em Renderer::DrawMesh.
                if (meshRenderer.Mesh != PrimitiveMesh::Cube && meshRenderer.Mesh != PrimitiveMesh::Sphere
                    && meshRenderer.Mesh != PrimitiveMesh::Capsule && meshRenderer.Mesh != PrimitiveMesh::Cylinder
                    && meshRenderer.Mesh != PrimitiveMesh::Plane) {
                    PRISM_CORE_ERROR("SceneSerializer: PrimitiveMesh invalido na entidade ", i, " de '", filepath.string(), "'.");
                    return false;
                }
            }

            bool hasLight = false;
            if (!ReadRaw(in, hasLight)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (flag de light da entidade ", i, "): ", filepath.string());
                return false;
            }
            if (hasLight) {
                auto& light = entity.AddComponent<LightComponent>();
                bool lightOk = ReadRaw(in, light.Type) && ReadRaw(in, light.Color)
                            && ReadRaw(in, light.Intensity) && ReadRaw(in, light.Range) && ReadRaw(in, light.SpotAngle);
                if (!lightOk) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (light da entidade ", i, "): ", filepath.string());
                    return false;
                }
                if (light.Type != LightType::Point && light.Type != LightType::Spot && light.Type != LightType::Directional) {
                    PRISM_CORE_ERROR("SceneSerializer: LightType invalido na entidade ", i, " de '", filepath.string(), "'.");
                    return false;
                }
            }

            bool hasCollider = false;
            if (!ReadRaw(in, hasCollider)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (flag de collider da entidade ", i, "): ", filepath.string());
                return false;
            }
            if (hasCollider) {
                auto& collider = entity.AddComponent<ColliderComponent>();
                bool colliderOk = ReadRaw(in, collider.Shape) && ReadRaw(in, collider.Size) && ReadRaw(in, collider.IsTrigger);
                if (!colliderOk) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (collider da entidade ", i, "): ", filepath.string());
                    return false;
                }
                if (collider.Shape != ColliderShape::Box && collider.Shape != ColliderShape::Sphere && collider.Shape != ColliderShape::Capsule) {
                    PRISM_CORE_ERROR("SceneSerializer: ColliderShape invalido na entidade ", i, " de '", filepath.string(), "'.");
                    return false;
                }
            }

            bool hasRigidBody = false;
            if (!ReadRaw(in, hasRigidBody)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (flag de rigidbody da entidade ", i, "): ", filepath.string());
                return false;
            }
            if (hasRigidBody) {
                auto& rigidBody = entity.AddComponent<RigidBodyComponent>();
                bool rigidBodyOk = ReadRaw(in, rigidBody.Type) && ReadRaw(in, rigidBody.Mass)
                                && ReadRaw(in, rigidBody.UseGravity) && ReadRaw(in, rigidBody.ContinuousCollisionDetection);
                if (!rigidBodyOk) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (rigidbody da entidade ", i, "): ", filepath.string());
                    return false;
                }
                if (rigidBody.Type != BodyType::Static && rigidBody.Type != BodyType::Kinematic && rigidBody.Type != BodyType::Dynamic) {
                    PRISM_CORE_ERROR("SceneSerializer: BodyType invalido na entidade ", i, " de '", filepath.string(), "'.");
                    return false;
                }
            }

            bool hasScript = false;
            if (!ReadRaw(in, hasScript)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (flag de script da entidade ", i, "): ", filepath.string());
                return false;
            }
            if (hasScript) {
                auto& script = entity.AddComponent<ScriptComponent>();
                if (!ReadString(in, script.ScriptPath)) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (script da entidade ", i, "): ", filepath.string());
                    return false;
                }
            }
        }

        m_Scene = loaded;
        PRISM_CORE_INFO("Cena '", m_Scene->GetName(), "' carregada de: ", filepath.string(), " (", entityCount, " entidades)");
        return true;
    }

}
