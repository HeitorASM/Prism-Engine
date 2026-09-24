#include "PrefabSerializer.h"
#include "Scene.h"
#include "Entity.h"
#include "ComponentRegistry.h"
#include "../Core/Log.h"

#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Prism {

    // Formato do arquivo .prismprefab (little-endian) - deliberadamente
    // paralelo ao de SceneSerializer (ver comentario la e no .h deste
    // arquivo), so que para UMA subarvore em vez da Scene inteira:
    //   [4 bytes] magic "PPFB" (diferente de "PRSM" - um .prismprefab
    //             NUNCA deveria ser aberto como se fosse um .prismmap, e
    //             vice-versa; o magic errado falha alto e claro em vez de
    //             ler bytes com o layout errado)
    //   [4 bytes] versao do formato (kPrefabFormatVersion abaixo)
    //   [4 bytes] quantidade de entidades (uint32_t) - raiz + descendentes
    //   por entidade (MESMO layout por-entidade de SceneSerializer):
    //     [string] tag
    //     [Transform] Translation, Rotation, Scale
    //     [ComponentRegistry::GetAll()] flag + campos, um bloco por tipo
    //     [4 bytes] indice do pai (int32_t, -1 = e a raiz do prefab)
    //
    // Versionado separadamente de kSceneFormatVersion (SceneSerializer.cpp)
    // porque os dois arquivos evoluem juntos NA PRATICA (mesma lista de
    // Components via ComponentRegistry) mas sao conceitos diferentes - um
    // bump aqui nao deveria forcar um bump la e vice-versa, mesmo que hoje
    // os dois numeros comecem alinhados por coincidencia (ambos refletem
    // o mesmo ComponentRegistry::GetAll() atual).
    //
    // MaterialComponent::LinkedAsset (ver Components.h e
    // EditorLayer::ReconcileLinkedMaterial) e gravado/lido pelo MESMO
    // callback de ComponentRegistry - nao ha campo extra a versionar
    // aqui so por causa dele. Efeito pratico: se a raiz (ou um filho) do
    // prefab tem um material VINCULADO a um .prismmat, cada instancia
    // criada por Instantiate() nasce com o MESMO vinculo - editar aquele
    // material afeta todas as instancias do prefab que ainda nao
    // desvincularam (ver botao "Desvincular" no painel Material), alem
    // de qualquer outra entidade fora do prefab que aponte para o mesmo
    // asset. Isto e intencional: o vinculo e do MATERIAL, nao do prefab.
    static constexpr uint32_t kPrefabFormatVersion = 1;
    static constexpr char kMagic[4] = { 'P', 'P', 'F', 'B' };

    // --- helpers de escrita/leitura binaria - mesmas rotinas
    // compartilhadas que SceneSerializer.cpp usa (ver ComponentRegistry.h).
    static void WriteString(std::ofstream& out, const std::string& str) {
        ComponentRegistry::WriteString(out, str);
    }
    static bool ReadString(std::ifstream& in, std::string& outStr) {
        return ComponentRegistry::ReadString(in, outStr);
    }
    template<typename T>
    static void WriteRaw(std::ofstream& out, const T& value) {
        ComponentRegistry::WriteRaw(out, value);
    }
    template<typename T>
    static bool ReadRaw(std::ifstream& in, T& value) {
        return ComponentRegistry::ReadRaw(in, value);
    }

    // Coleta 'root' + toda a subarvore em ordem de PRE-ORDEM (pai sempre
    // antes de seus filhos na lista final) - isso e o que permite ao
    // Serialize() abaixo calcular o indice-de-pai de cada entidade numa
    // UNICA passada (diferente de SceneSerializer::Serialize, que itera
    // TagComponent na ordem "natural" do registry e pode ver um filho
    // antes do pai - aqui, como SOMOS NOS que montamos a lista, garantimos
    // a ordem certa de proposito, simplificando o calculo de indices).
    static void CollectSubtree(Entity entity, Scene& scene, std::vector<Entity>& out) {
        out.push_back(entity);
        if (auto* rel = scene.GetRegistry().try_get<RelationshipComponent>(entity.GetHandle())) {
            for (entt::entity childHandle : rel->Children) {
                if (scene.GetRegistry().valid(childHandle))
                    CollectSubtree(Entity(childHandle, &scene), scene, out);
            }
        }
    }

    bool PrefabSerializer::Serialize(Entity root, const std::filesystem::path& filepath) {
        if (!root) {
            PRISM_CORE_ERROR("PrefabSerializer: entidade invalida - nada para salvar.");
            return false;
        }

        ComponentRegistry::RegisterAll(); // idempotente - ver comentario equivalente em SceneSerializer

        Scene* scene = root.GetScene();

        std::vector<Entity> entities;
        CollectSubtree(root, *scene, entities);

        // Mapa handle -> indice posicional dentro de 'entities' (0 = raiz,
        // por construcao de CollectSubtree) - usado para gravar o indice
        // do pai de cada entidade abaixo.
        std::unordered_map<entt::entity, int32_t> handleToIndex;
        for (size_t i = 0; i < entities.size(); i++)
            handleToIndex[entities[i].GetHandle()] = (int32_t)i;

        // Garante que a pasta de destino existe (ex: Assets/Prefabs) -
        // create_directories nao falha se ja existir, e cria toda a
        // hierarquia de pastas intermediarias necessaria de uma vez.
        std::error_code ec;
        std::filesystem::create_directories(filepath.parent_path(), ec);

        std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            PRISM_CORE_ERROR("PrefabSerializer: nao foi possivel criar o arquivo de prefab: ", filepath.string());
            return false;
        }

        out.write(kMagic, sizeof(kMagic));
        WriteRaw(out, kPrefabFormatVersion);
        WriteRaw(out, (uint32_t)entities.size());

        bool writeFailed = false;
        for (size_t i = 0; i < entities.size(); i++) {
            Entity entity = entities[i];

            WriteString(out, entity.GetComponent<TagComponent>().Tag);

            auto& transform = entity.GetComponent<TransformComponent>();
            WriteRaw(out, transform.Translation);
            WriteRaw(out, transform.Rotation);
            WriteRaw(out, transform.Scale);

            // Mesmo mecanismo de SceneSerializer::Serialize - um bloco
            // flag+campos por Component registrado, na mesma ordem que
            // ComponentRegistry::GetAll() sempre retorna.
            for (auto& info : ComponentRegistry::GetAll()) {
                bool has = info.Has(entity);
                WriteRaw(out, has);
                if (has)
                    info.Serialize(out, entity);
            }

            int32_t parentIndex = -1;
            if (auto* rel = scene->GetRegistry().try_get<RelationshipComponent>(entity.GetHandle())) {
                if (rel->Parent != entt::null) {
                    auto it = handleToIndex.find(rel->Parent);
                    // Um pai FORA da subarvore (so pode acontecer para a
                    // propria raiz, cujo pai na Scene original nao faz
                    // parte deste prefab) vira -1 de proposito - a raiz do
                    // prefab sempre nasce raiz ao instanciar (ver
                    // Instantiate), nunca carrega o pai que tinha na cena
                    // de origem.
                    if (it != handleToIndex.end())
                        parentIndex = it->second;
                }
            }
            WriteRaw(out, parentIndex);

            if (!out) writeFailed = true;
        }

        if (writeFailed) {
            PRISM_CORE_ERROR("PrefabSerializer: falha ao escrever dados do prefab em: ", filepath.string());
            return false;
        }

        PRISM_CORE_INFO("Prefab '", root.GetComponent<TagComponent>().Tag, "' salvo em: ", filepath.string(),
            " (", entities.size(), " entidade(s)).");
        return true;
    }

    Entity PrefabSerializer::Instantiate(Scene& targetScene, const std::filesystem::path& filepath, AssetID sourceAsset) {
        ComponentRegistry::RegisterAll();

        std::ifstream in(filepath, std::ios::binary);
        if (!in.is_open()) {
            PRISM_CORE_ERROR("PrefabSerializer: nao foi possivel abrir o arquivo de prefab: ", filepath.string());
            return Entity();
        }

        char magic[4];
        in.read(magic, sizeof(magic));
        if (!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
            PRISM_CORE_ERROR("PrefabSerializer: '", filepath.string(), "' nao e um arquivo de prefab Prism valido (magic incorreto).");
            return Entity();
        }

        uint32_t version = 0;
        if (!ReadRaw(in, version) || version != kPrefabFormatVersion) {
            PRISM_CORE_ERROR("PrefabSerializer: versao de formato incompativel em '", filepath.string(),
                "' (arquivo=v", version, ", esperado=v", kPrefabFormatVersion, ").");
            return Entity();
        }

        uint32_t entityCount = 0;
        if (!ReadRaw(in, entityCount)) {
            PRISM_CORE_ERROR("PrefabSerializer: arquivo de prefab corrompido (contagem de entidades): ", filepath.string());
            return Entity();
        }

        // Mesma protecao de sanidade que SceneSerializer::Deserialize usa
        // contra um arquivo corrompido/truncado com um valor absurdo aqui.
        constexpr uint32_t kMaxReasonableEntityCount = 100'000;
        if (entityCount == 0 || entityCount > kMaxReasonableEntityCount) {
            PRISM_CORE_ERROR("PrefabSerializer: contagem de entidades invalida (", entityCount,
                ") em '", filepath.string(), "' - arquivo provavelmente corrompido/truncado.");
            return Entity();
        }

        std::vector<int32_t> parentIndices;
        parentIndices.reserve(entityCount);
        std::vector<Entity> createdEntities;
        createdEntities.reserve(entityCount);

        for (uint32_t i = 0; i < entityCount; i++) {
            std::string tag;
            if (!ReadString(in, tag)) {
                PRISM_CORE_ERROR("PrefabSerializer: arquivo de prefab corrompido (tag da entidade ", i, "): ", filepath.string());
                return Entity();
            }

            Entity entity = targetScene.CreateEntity(tag);
            auto& transform = entity.GetComponent<TransformComponent>();

            bool ok = ReadRaw(in, transform.Translation)
                   && ReadRaw(in, transform.Rotation)
                   && ReadRaw(in, transform.Scale);
            if (!ok) {
                PRISM_CORE_ERROR("PrefabSerializer: arquivo de prefab corrompido (transform da entidade ", i, "): ", filepath.string());
                // Entidades ja criadas nesta tentativa ficam orfas na Scene -
                // aceitavel aqui (mesmo trade-off que SceneSerializer::Deserialize
                // ja aceita: uma falha no meio da leitura e rara/indica
                // arquivo corrompido, e o chamador normalmente vai avisar o
                // usuario e descartar o resultado de qualquer forma).
                return Entity();
            }

            for (auto& info : ComponentRegistry::GetAll()) {
                bool has = false;
                if (!ReadRaw(in, has)) {
                    PRISM_CORE_ERROR("PrefabSerializer: arquivo de prefab corrompido (flag de '", info.DisplayName, "' da entidade ", i, "): ", filepath.string());
                    return Entity();
                }
                if (has && !info.Deserialize(in, entity, i))
                    return Entity(); // mensagem especifica ja logada dentro do proprio Deserialize
            }

            int32_t parentIndex = -1;
            if (!ReadRaw(in, parentIndex)) {
                PRISM_CORE_ERROR("PrefabSerializer: arquivo de prefab corrompido (indice de pai da entidade ", i, "): ", filepath.string());
                return Entity();
            }
            parentIndices.push_back(parentIndex);
            createdEntities.push_back(entity);

            // Marca esta entidade como membro da instancia (indice 'i'
            // dentro DESTE arquivo) se um AssetID foi passado - ver
            // comentario grande no .h sobre 'sourceAsset'. Feito aqui, ja
            // dentro do loop de criacao, para toda entidade da subarvore
            // (nao so a raiz) ganhar o indice de uma vez, sem precisar de
            // outro loop separado so para isto.
            if (sourceAsset.IsValid())
                entity.AddComponent<PrefabInstanceMemberComponent>().IndexInPrefab = i;
        }

        // Segunda passada: resolve os indices de pai em chamadas reais de
        // SetParent - mesmo motivo/mesma protecao de indice fora do range
        // que SceneSerializer::Deserialize ja usa.
        for (uint32_t i = 0; i < entityCount; i++) {
            int32_t parentIndex = parentIndices[i];
            if (parentIndex < 0 || (uint32_t)parentIndex >= entityCount)
                continue;
            targetScene.SetParent(createdEntities[i], createdEntities[(uint32_t)parentIndex]);
        }

        // A RAIZ (indice 0, ver CollectSubtree em Serialize) tambem ganha
        // PrefabInstanceRootComponent - e ela quem representa a instancia
        // inteira para o resto do editor (ver PrefabSyncer.h e
        // EditorLayer::RenderHierarchyNode para o selo visual). So a raiz,
        // nao cada filho: um filho ja tem PrefabInstanceMemberComponent
        // (marcado no loop acima) para PrefabSyncer saber comparar seus
        // components, mas nao precisa saber sozinho qual e o AssetID de
        // origem - isso e responsabilidade da raiz da subarvore dele.
        if (sourceAsset.IsValid())
            createdEntities[0].AddComponent<PrefabInstanceRootComponent>().SourceAsset = sourceAsset;

        // So loga quando 'sourceAsset' e valido, ou seja, so na instancia
        // "de verdade" que o usuario ve na cena (drag-and-drop, etc). Com
        // AssetID invalido isto e so PrefabSyncer::LoadPrefabForComparison
        // recarregando o arquivo numa Scene temporaria - acontece TODO
        // FRAME enquanto o painel Prefab estiver aberto (ver
        // EditorLayer::RenderPrefabInstanceSection) e qualquer nivel de
        // log (o Log nao filtra TRACE) inundaria o console sem servir de
        // nada: nao e uma instancia nova, so uma releitura interna.
        if (sourceAsset.IsValid())
            PRISM_CORE_INFO("Prefab instanciado de: ", filepath.string(), " (", entityCount, " entidade(s)).");
        return createdEntities[0]; // indice 0 e sempre a raiz (ver CollectSubtree em Serialize)
    }

}
