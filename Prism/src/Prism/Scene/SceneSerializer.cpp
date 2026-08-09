#include "SceneSerializer.h"
#include "Scene.h"
#include "Entity.h"
#include "../Core/Log.h"

#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

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
    // v1 -> v2: adicionados LightComponent, ColliderComponent,
    // RigidBodyComponent, ScriptComponent (todos opcionais, mesmo padrao de
    // flag-de-presenca que MeshRendererComponent ja usava). Arquivos v1 nao
    // sao lidos por este parser (ver Deserialize) - projetos criados antes
    // desta mudanca precisam ser resalvos uma vez.
    //
    // v2 -> v3: adicionado CameraComponent (opcional, mesmo padrao de flag
    // de presenca). Arquivos v2 nao sao lidos por este parser - mapas
    // salvos antes desta mudanca precisam ser resalvos uma vez.
    //
    // v3 -> v4: adicionado RelationshipComponent (parenting). Diferente
    // dos outros components opcionais, NAO usa flag de presenca + campos -
    // usa so um int32_t por entidade (indice do pai na ordem de escrita
    // deste arquivo, -1 = sem pai/raiz). entt::entity bruto NUNCA e salvo
    // (o handle e so valido durante a sessao atual - ao recarregar, o
    // EnTT pode reciclar/reordenar handles livremente); o indice posicional
    // e estavel porque Serialize()/Deserialize() sempre iteram/criam
    // entidades na MESMA ordem (ver ForEachEntity). Children nao e salvo
    // separado - Deserialize() reconstroi Children chamando
    // Scene::SetParent() para cada entidade que tem um Parent valido,
    // depois que TODAS as entidades ja foram criadas (precisa dos handles
    // novos de ambos os lados existirem antes de ligar o parentesco).
    //
    // v4 -> v5: LightComponent ganhou InnerSpotAngle (soft edge do cone
    // do Spot) e CastShadows (reservado - ver Components.h/Renderer, o
    // Renderer ainda nao produz sombra nenhuma). Size (reservado para o
    // futuro tipo Area) DELIBERADAMENTE nao e salvo ainda - nenhum
    // LightType usa esse campo hoje, entao gravar seria so ruido no
    // arquivo; sera adicionado ao formato quando Area for implementado.
    // Arquivos v4 nao sao lidos por este parser - mapas salvos antes
    // desta mudanca precisam ser resalvos uma vez (mesmo padrao de todo
    // bump anterior, ver comentarios acima).
    static constexpr uint32_t kSceneFormatVersion = 5;
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
        // diretamente) para escrever o cabecalho antes dos dados. Ao mesmo
        // tempo, monta o mapa handle -> indice posicional (0, 1, 2...) na
        // MESMA ordem que o loop de escrita abaixo vai seguir - e esse
        // indice, nao o entt::entity bruto, que vira o "id do pai" no
        // arquivo (ver comentario de v3->v4 em kSceneFormatVersion acima).
        uint32_t entityCount = 0;
        std::unordered_map<entt::entity, int32_t> handleToIndex;
        m_Scene->ForEachEntity([&](entt::entity handle, TagComponent&) {
            handleToIndex[handle] = (int32_t)entityCount;
            entityCount++;
        });
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
                WriteRaw(out, light.InnerSpotAngle);
                WriteRaw(out, light.CastShadows);
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

            // CameraComponent - adicionado na v3 do formato (ver
            // kSceneFormatVersion). Mesmo padrao de flag de presenca dos
            // outros components opcionais acima.
            bool hasCamera = entity.HasComponent<CameraComponent>();
            WriteRaw(out, hasCamera);
            if (hasCamera) {
                auto& camera = entity.GetComponent<CameraComponent>();
                WriteRaw(out, camera.ProjectionType);
                WriteRaw(out, camera.FOV);
                WriteRaw(out, camera.OrthoSize);
                WriteRaw(out, camera.NearClip);
                WriteRaw(out, camera.FarClip);
                WriteRaw(out, camera.Primary);
            }

            // RelationshipComponent - adicionado na v4 do formato. So o
            // indice do PAI e gravado (ver comentario de kSceneFormatVersion);
            // -1 quando a entidade nao tem RelationshipComponent ou nao tem
            // pai (e raiz). Um pai que aponta para fora deste mapa (nao
            // deveria acontecer, ja que so entidades da propria Scene podem
            // virar pai via Scene::SetParent) tambem vira -1 em vez de
            // gravar lixo.
            int32_t parentIndex = -1;
            if (auto* rel = m_Scene->GetRegistry().try_get<RelationshipComponent>(handle)) {
                if (rel->Parent != entt::null) {
                    auto it = handleToIndex.find(rel->Parent);
                    if (it != handleToIndex.end())
                        parentIndex = it->second;
                }
            }
            WriteRaw(out, parentIndex);

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

        // parentIndices[i] = indice do pai da entidade i no arquivo (-1 =
        // sem pai). Resolvido para handles/SetParent DEPOIS que todas as
        // entidades existirem (ver loop separado apos este) - nao da para
        // chamar SetParent no meio deste loop porque o pai de uma entidade
        // pode aparecer DEPOIS dela no arquivo (ordem de escrita nao
        // garante pai-antes-do-filho).
        std::vector<int32_t> parentIndices;
        parentIndices.reserve(entityCount);
        std::vector<Entity> createdEntities;
        createdEntities.reserve(entityCount);

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
                            && ReadRaw(in, light.Intensity) && ReadRaw(in, light.Range) && ReadRaw(in, light.SpotAngle)
                            && ReadRaw(in, light.InnerSpotAngle) && ReadRaw(in, light.CastShadows);
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

            bool hasCamera = false;
            if (!ReadRaw(in, hasCamera)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (flag de camera da entidade ", i, "): ", filepath.string());
                return false;
            }
            if (hasCamera) {
                auto& camera = entity.AddComponent<CameraComponent>();
                bool cameraOk = ReadRaw(in, camera.ProjectionType) && ReadRaw(in, camera.FOV)
                             && ReadRaw(in, camera.OrthoSize) && ReadRaw(in, camera.NearClip)
                             && ReadRaw(in, camera.FarClip) && ReadRaw(in, camera.Primary);
                if (!cameraOk) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (camera da entidade ", i, "): ", filepath.string());
                    return false;
                }
                if (camera.ProjectionType != CameraProjectionType::Perspective && camera.ProjectionType != CameraProjectionType::Orthographic) {
                    PRISM_CORE_ERROR("SceneSerializer: CameraProjectionType invalido na entidade ", i, " de '", filepath.string(), "'.");
                    return false;
                }
            }

            // RelationshipComponent - so o indice do pai (ver comentario
            // de kSceneFormatVersion/v3->v4 no topo do arquivo). Guardado
            // aqui, resolvido em SetParent() so depois que TODAS as
            // entidades desta cena existirem (loop logo abaixo).
            int32_t parentIndex = -1;
            if (!ReadRaw(in, parentIndex)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (indice de pai da entidade ", i, "): ", filepath.string());
                return false;
            }
            parentIndices.push_back(parentIndex);
            createdEntities.push_back(entity);
        }

        // Segunda passada: resolve os indices de pai em chamadas reais de
        // SetParent, agora que createdEntities[i] existe para qualquer i
        // (inclusive indices que aparecem "a frente" no arquivo). Indices
        // fora do range [0, entityCount) sao tratados como -1 (sem pai) -
        // protecao extra contra um arquivo corrompido/de origem duvidosa,
        // ja que aceitar um indice invalido aqui acessaria
        // createdEntities fora dos limites.
        for (uint32_t i = 0; i < entityCount; i++) {
            int32_t parentIndex = parentIndices[i];
            if (parentIndex < 0 || (uint32_t)parentIndex >= entityCount)
                continue;
            loaded->SetParent(createdEntities[i], createdEntities[(uint32_t)parentIndex]);
        }

        m_Scene = loaded;
        PRISM_CORE_INFO("Cena '", m_Scene->GetName(), "' carregada de: ", filepath.string(), " (", entityCount, " entidades)");
        return true;
    }

}
