#include "PrefabSyncer.h"
#include "Scene.h"
#include "PrefabSerializer.h"
#include "ComponentRegistry.h"
#include "../Core/Log.h"

#include <fstream>
#include <cstring>

namespace Prism {

    // Nome "sintetico" usado no diff para o TransformComponent (que nao
    // passa por ComponentRegistry - ver LIMITACAO no PrefabSyncer.h). So
    // FILHOS da instancia (nunca a raiz) participam - ver SerializeAllComponents.
    static const char* const kTransformName = "Transform";

    static std::vector<char> TransformToBytes(const TransformComponent& t) {
        std::vector<char> data(sizeof(glm::vec3) * 3);
        std::memcpy(data.data(), &t.Translation, sizeof(glm::vec3));
        std::memcpy(data.data() + sizeof(glm::vec3), &t.Rotation, sizeof(glm::vec3));
        std::memcpy(data.data() + sizeof(glm::vec3) * 2, &t.Scale, sizeof(glm::vec3));
        return data;
    }

    int PrefabDiffResult::OverrideCount() const {
        int count = 0;
        for (const PrefabComponentDiff& diff : Components)
            if (diff.Overridden)
                count++;
        return count;
    }

    // --- helpers privados ---------------------------------------------

    bool PrefabSyncer::LoadPrefabForComparison(const std::filesystem::path& prefabFilePath, Ref<Scene>& outScene, std::vector<Entity>& outEntities) {
        outScene = Scene::Create("__prefab_comparison_temp__");
        outEntities.clear();

        // AssetID INVALIDO de proposito (ver comentario no .h) - esta e
        // so uma copia de comparacao, nunca deveria ela mesma ganhar
        // PrefabInstanceRootComponent/PrefabInstanceMemberComponent (ver
        // Components.h - Instantiate so marca esses components quando
        // 'sourceAsset' e valido).
        Entity root = PrefabSerializer::Instantiate(*outScene, prefabFilePath);
        if (!root)
            return false; // erro especifico ja logado dentro de Instantiate

        // Reconstroi a lista na MESMA ordem posicional que o arquivo usa
        // (indice i = PrefabInstanceMemberComponent::IndexInPrefab == i
        // do lado da INSTANCIA REAL) - PrefabSerializer::Instantiate ja
        // cria as entidades nessa ordem internamente, mas nao a expoe;
        // aqui reconstruimos ela varrendo a subarvore em PRE-ORDEM da
        // MESMA forma que PrefabSerializer::CollectSubtree/Serialize fez
        // ao gravar o arquivo (pai sempre antes dos filhos, filhos na
        // ordem de RelationshipComponent::Children, via GetChildAt - ver
        // Entity.h) - garantindo os MESMOS indices que o arquivo usa.
        CollectSubtreePreOrder(root, outEntities);
        return true;
    }

    void PrefabSyncer::CollectSubtreePreOrder(Entity entity, std::vector<Entity>& out) {
        out.push_back(entity);
        size_t childCount = entity.GetChildCount();
        for (size_t i = 0; i < childCount; i++) {
            Entity child = entity.GetChildAt(i);
            if (child)
                CollectSubtreePreOrder(child, out);
        }
    }

    std::unordered_map<std::string, std::vector<char>> PrefabSyncer::SerializeAllComponents(Entity entity, bool includeTransform) {
        std::unordered_map<std::string, std::vector<char>> result;
        if (!entity)
            return result;

        // Transform de FILHOS de instancia entra na comparacao (senao
        // ajustar a posicao/rotacao/escala de uma peca dentro do prefab
        // nunca chegaria ao arquivo nem as outras cenas). A RAIZ fica
        // de fora de proposito: reposicionar a instancia na cena e o uso
        // mais basico de um prefab e nao pode contar como override.
        if (includeTransform && entity.HasComponent<TransformComponent>())
            result[kTransformName] = TransformToBytes(entity.GetComponent<TransformComponent>());

        // Mesmo truque de SceneSerializer::ComputeFingerprint (ver
        // comentario la sobre por que um arquivo temporario, nao um
        // std::ostringstream: ComponentTypeInfo::Serialize esta amarrado
        // a std::ofstream em toda a engine) - um arquivo por Component
        // aqui, reescrito a cada chamada.
        std::error_code ec;
        std::filesystem::path tempPath = std::filesystem::temp_directory_path(ec);
        if (ec)
            return result;
        tempPath /= "prism_prefab_syncer_compare.tmp";

        struct TempFileGuard {
            std::filesystem::path Path;
            ~TempFileGuard() { std::error_code e; std::filesystem::remove(Path, e); }
        } guard{ tempPath };

        for (auto& info : ComponentRegistry::GetAll()) {
            if (!info.Has(entity))
                continue;

            {
                std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                    continue;
                info.Serialize(out, entity);
                if (!out)
                    continue;
            } // fecha (flush) antes de reler

            std::ifstream in(tempPath, std::ios::binary | std::ios::ate);
            if (!in.is_open())
                continue;
            std::streamsize size = in.tellg();
            in.seekg(0, std::ios::beg);
            std::vector<char> data((size_t)size);
            if (size > 0 && !in.read(data.data(), size))
                continue;

            result[info.DisplayName] = std::move(data);
        }

        return result;
    }

    // --- Diff ------------------------------------------------------------

    PrefabDiffResult PrefabSyncer::Diff(Entity instanceRoot, const std::filesystem::path& prefabFilePath) {
        PrefabDiffResult result;

        if (!instanceRoot || !instanceRoot.HasComponent<PrefabInstanceRootComponent>()) {
            result.ErrorMessage = "Entidade nao e raiz de uma instancia de prefab.";
            return result;
        }

        Ref<Scene> comparisonScene;
        std::vector<Entity> prefabEntities;
        if (!LoadPrefabForComparison(prefabFilePath, comparisonScene, prefabEntities)) {
            result.ErrorMessage = "Nao foi possivel ler o prefab de origem: " + prefabFilePath.string();
            return result;
        }

        // Coleta a subarvore da INSTANCIA na Scene REAL com a MESMA
        // funcao usada do lado do prefab (ver CollectSubtreePreOrder no
        // .h) - garante que os indices batem exatamente, em vez de duas
        // implementacoes de travessia que precisariam ser mantidas
        // identicas manualmente. Entidades SEM PrefabInstanceMemberComponent
        // (adicionadas a mao depois da instanciacao original - ver
        // comentario no .h) sao coletadas aqui (para nao quebrar a
        // travessia dos descendentes delas, se houver), mas simplesmente
        // IGNORADAS na comparacao logo abaixo.
        std::vector<Entity> instanceEntities;
        CollectSubtreePreOrder(instanceRoot, instanceEntities);

        // Estrutura bate se: mesma contagem E toda entidade da instancia
        // (que TEM PrefabInstanceMemberComponent) aponta para um indice
        // que existe do lado do prefab. Nao exige correspondencia
        // 1-para-1 completa (uma entidade adicionada a mao na instancia,
        // sem o component, nao conta contra isto - ver comentario acima).
        if (instanceEntities.size() != prefabEntities.size())
            result.StructureMatches = false;

        for (Entity instanceEntity : instanceEntities) {
            if (!instanceEntity.HasComponent<PrefabInstanceMemberComponent>())
                continue; // adicionada a mao depois da instanciacao - nunca comparada (ver comentario no .h)

            uint32_t index = instanceEntity.GetComponent<PrefabInstanceMemberComponent>().IndexInPrefab;
            if (index >= prefabEntities.size()) {
                result.StructureMatches = false;
                continue; // indice que nao existe mais no prefab atual (entidades removidas no arquivo) - nada para comparar
            }

            Entity prefabEntity = prefabEntities[index];

            // Indice 0 e a RAIZ do prefab - nunca compara Transform nela.
            const bool includeTransform = (index != 0);
            auto instanceComponents = SerializeAllComponents(instanceEntity, includeTransform);
            auto prefabComponents = SerializeAllComponents(prefabEntity, includeTransform);

            // Uniao dos nomes de Component presentes de QUALQUER lado -
            // um Component presente so no prefab (adicionado la desde a
            // instanciacao) ou so na instancia (removido no prefab desde
            // entao, ou adicionado a mao na instancia) tambem precisa
            // aparecer no diff, nao so os que existem nos dois lados.
            std::vector<std::string> allNames;
            for (auto& [name, _] : instanceComponents) allNames.push_back(name);
            for (auto& [name, _] : prefabComponents)
                if (instanceComponents.find(name) == instanceComponents.end())
                    allNames.push_back(name);

            for (const std::string& name : allNames) {
                PrefabComponentDiff diff;
                diff.InstanceEntity = instanceEntity;
                diff.ComponentName = name;

                auto itInstance = instanceComponents.find(name);
                auto itPrefab = prefabComponents.find(name);
                diff.PresentInInstance = (itInstance != instanceComponents.end());
                diff.PresentInPrefab = (itPrefab != prefabComponents.end());

                if (diff.PresentInInstance != diff.PresentInPrefab)
                    diff.Overridden = true; // um dos dois lados nao tem o Component - sempre diverge
                else
                    diff.Overridden = (itInstance->second != itPrefab->second); // comparacao byte a byte (std::vector<char>::operator== ja faz isso)

                result.Components.push_back(std::move(diff));
            }
        }

        result.Valid = true;
        return result;
    }

    // --- RevertComponent ---------------------------------------------------

    bool PrefabSyncer::RevertComponent(Entity instanceEntity, const std::filesystem::path& prefabFilePath, uint32_t indexInPrefab, const std::string& componentName) {
        if (!instanceEntity)
            return false;

        Ref<Scene> comparisonScene;
        std::vector<Entity> prefabEntities;
        if (!LoadPrefabForComparison(prefabFilePath, comparisonScene, prefabEntities)) {
            PRISM_CORE_ERROR("PrefabSyncer: nao foi possivel ler o prefab para reverter: ", prefabFilePath.string());
            return false;
        }
        if (indexInPrefab >= prefabEntities.size()) {
            PRISM_CORE_ERROR("PrefabSyncer: indice de prefab (", indexInPrefab, ") fora do range - o prefab foi editado e esta entidade nao existe mais nele.");
            return false;
        }
        Entity prefabEntity = prefabEntities[indexInPrefab];

        if (componentName == kTransformName) {
            if (indexInPrefab == 0)
                return true; // raiz: transform e da instancia, nunca sincronizado
            instanceEntity.GetComponent<TransformComponent>() = prefabEntity.GetComponent<TransformComponent>();
            return true;
        }

        for (auto& info : ComponentRegistry::GetAll()) {
            if (info.DisplayName != componentName)
                continue;

            bool prefabHas = info.Has(prefabEntity);
            bool instanceHas = info.Has(instanceEntity);

            if (!prefabHas) {
                // O prefab nao tem mais este Component - reverter
                // significa REMOVER da instancia tambem, se presente.
                if (instanceHas)
                    info.Remove(instanceEntity);
                return true;
            }

            // Copy() exige que o destino AINDA NAO tenha o Component (ver
            // ComponentRegistry.h) - remove primeiro se ja tiver, para
            // sempre poder usar o mesmo Copy() dos dois casos (instancia
            // tinha o Component OU nao tinha ainda).
            if (instanceHas)
                info.Remove(instanceEntity);
            info.Copy(prefabEntity, instanceEntity);
            return true;
        }

        PRISM_CORE_ERROR("PrefabSyncer: Component '", componentName, "' nao encontrado no ComponentRegistry - nao e possivel reverter.");
        return false;
    }

    // --- UpdateAll -----------------------------------------------------

    int PrefabSyncer::UpdateAll(Entity instanceRoot, const std::filesystem::path& prefabFilePath) {
        PrefabDiffResult diff = Diff(instanceRoot, prefabFilePath);
        if (!diff.Valid) {
            PRISM_CORE_ERROR("PrefabSyncer: nao foi possivel sincronizar - ", diff.ErrorMessage);
            return 0;
        }

        int updated = 0;
        // 'd' NAO e const: Entity::HasComponent/GetComponent nao sao
        // const-qualificados (ver Entity.h) - um "const PrefabComponentDiff&"
        // tornaria "d.InstanceEntity" implicitamente const, e chamar um
        // metodo nao-const num Entity const nao compila (MSVC C2662).
        // So LEIO os campos de 'd' aqui (nunca escrevo em diff.Components),
        // entao tirar o const e seguro - nao muda a semantica do laco.
        for (PrefabComponentDiff& d : diff.Components) {
            if (d.Overridden)
                continue; // preserva overrides da instancia - ver comentario grande no .h

            if (!d.InstanceEntity.HasComponent<PrefabInstanceMemberComponent>())
                continue; // nunca deveria acontecer (Diff so preenche 'Components' para entidades com o component), mas confere por seguranca antes de indexar

            uint32_t index = d.InstanceEntity.GetComponent<PrefabInstanceMemberComponent>().IndexInPrefab;
            if (RevertComponent(d.InstanceEntity, prefabFilePath, index, d.ComponentName))
                updated++;
        }

        if (!diff.StructureMatches) {
            PRISM_CORE_WARN("PrefabSyncer: o prefab '", prefabFilePath.string(),
                "' tem uma estrutura diferente da instancia (entidades adicionadas/removidas na subarvore) - so os components das entidades correspondentes foram sincronizados. Recrie a instancia para refletir a nova estrutura.");
        }

        return updated;
    }

    // --- ApplyComponentToPrefab -----------------------------------------

    bool PrefabSyncer::ApplyComponentToPrefab(Entity instanceEntity, const std::filesystem::path& prefabFilePath, const std::string& componentName) {
        // NAO propaga para outras instancias deste mesmo prefab
        // automaticamente (ao contrario de EditorLayer::
        // ReconcileLinkedMaterial, que atualiza TODA entidade vinculada a
        // um material assim que o arquivo muda). Motivo: "Aplicar ao
        // Prefab" e uma acao EXPLICITA do usuario sobre UM Component de
        // UMA instancia especifica - propagar imediatamente para outras
        // instancias (que podem estar em OUTRAS cenas, nem abertas agora)
        // sem confirmacao seria uma acao de grande alcance escondida
        // atras de um botao que parece local. O CAMINHO normal para
        // outras instancias enxergarem esta mudanca e o mesmo de sempre:
        // o editor (ou o usuario) chama UpdateAll() sobre elas depois -
        // exatamente como abrir aquela cena e ver um sinal de "prefab
        // desatualizado" ja convida a fazer.
        if (!instanceEntity)
            return false;

        Ref<Scene> comparisonScene;
        std::vector<Entity> prefabEntities;
        if (!LoadPrefabForComparison(prefabFilePath, comparisonScene, prefabEntities)) {
            PRISM_CORE_ERROR("PrefabSyncer: nao foi possivel ler o prefab para aplicar a mudanca: ", prefabFilePath.string());
            return false;
        }

        if (!instanceEntity.HasComponent<PrefabInstanceMemberComponent>()) {
            PRISM_CORE_ERROR("PrefabSyncer: a entidade nao pertence a uma instancia de prefab (sem PrefabInstanceMemberComponent).");
            return false;
        }
        uint32_t index = instanceEntity.GetComponent<PrefabInstanceMemberComponent>().IndexInPrefab;
        if (index >= prefabEntities.size()) {
            PRISM_CORE_ERROR("PrefabSyncer: indice de prefab (", index, ") fora do range - o prefab foi editado e esta entidade nao existe mais nele.");
            return false;
        }
        Entity prefabEntity = prefabEntities[index];
        Entity prefabRoot = prefabEntities[0];

        if (componentName == kTransformName) {
            if (index == 0)
                return true; // raiz: nada a aplicar (transform da raiz nunca e comparado)
            prefabEntity.GetComponent<TransformComponent>() = instanceEntity.GetComponent<TransformComponent>();
            if (!PrefabSerializer::Serialize(prefabRoot, prefabFilePath)) {
                PRISM_CORE_ERROR("PrefabSyncer: falha ao gravar o prefab atualizado em: ", prefabFilePath.string());
                return false;
            }
            return true;
        }

        for (auto& info : ComponentRegistry::GetAll()) {
            if (info.DisplayName != componentName)
                continue;

            bool instanceHas = info.Has(instanceEntity);
            bool prefabHas = info.Has(prefabEntity);

            if (!instanceHas) {
                // A instancia nao tem mais este Component (removido pelo
                // usuario) - "aplicar" significa remover do prefab
                // tambem, se ele ainda o tiver.
                if (prefabHas)
                    info.Remove(prefabEntity);
            }
            else {
                if (prefabHas)
                    info.Remove(prefabEntity); // Copy() exige ausencia previa - ver comentario identico em RevertComponent
                info.Copy(instanceEntity, prefabEntity);
            }

            // Regrava o .prismprefab INTEIRO com o estado atualizado da
            // Scene de comparacao - PrefabSerializer::Serialize nao sabe
            // "atualizar so um Component de uma entidade", entao
            // regravamos o arquivo inteiro a partir da raiz (que ja tem
            // exatamente o mesmo conteudo de antes, exceto o Component
            // que acabamos de mudar em 'prefabEntity').
            if (!PrefabSerializer::Serialize(prefabRoot, prefabFilePath)) {
                PRISM_CORE_ERROR("PrefabSyncer: falha ao gravar o prefab atualizado em: ", prefabFilePath.string());
                return false;
            }
            return true;
        }

        PRISM_CORE_ERROR("PrefabSyncer: Component '", componentName, "' nao encontrado no ComponentRegistry - nao e possivel aplicar.");
        return false;
    }

    bool PrefabSyncer::ApplyStructureToPrefab(Entity instanceRoot, const std::filesystem::path& prefabFilePath) {
        if (!instanceRoot || !instanceRoot.HasComponent<PrefabInstanceRootComponent>()) {
            PRISM_CORE_ERROR("PrefabSyncer: ApplyStructureToPrefab exige a RAIZ de uma instancia de prefab.");
            return false;
        }

        // Guarda o Transform da instancia para restaurar depois: o
        // Serialize grava o Transform ATUAL da raiz, e a posicao da
        // instancia na cena nao deve virar a posicao "padrao" do prefab.
        TransformComponent& rootTransform = instanceRoot.GetComponent<TransformComponent>();
        const TransformComponent instanceTransform = rootTransform;

        // Transform que o prefab JA tinha na raiz (indice 0). Se o
        // arquivo nao puder ser lido, cai no padrao.
        TransformComponent prefabRootTransform;
        {
            Ref<Scene> tempScene;
            std::vector<Entity> tempEntities;
            if (LoadPrefabForComparison(prefabFilePath, tempScene, tempEntities) && !tempEntities.empty())
                prefabRootTransform = tempEntities[0].GetComponent<TransformComponent>();
        }

        // Grava com o Transform do PREFAB na raiz, depois devolve o da instancia.
        rootTransform = prefabRootTransform;
        const bool saved = PrefabSerializer::Serialize(instanceRoot, prefabFilePath);
        instanceRoot.GetComponent<TransformComponent>() = instanceTransform;

        if (!saved) {
            PRISM_CORE_ERROR("PrefabSyncer: falha ao gravar a estrutura do prefab em: ", prefabFilePath.string());
            return false;
        }

        // Remarca os indices da instancia na MESMA pre-ordem do arquivo
        // recem-gravado. Toda entidade da subarvore vira membro (inclusive
        // as recem-adicionadas, que ate agora nao tinham vinculo).
        std::vector<Entity> subtree;
        CollectSubtreePreOrder(instanceRoot, subtree);
        for (size_t i = 0; i < subtree.size(); i++) {
            Entity e = subtree[i];
            if (!e.HasComponent<PrefabInstanceMemberComponent>())
                e.AddComponent<PrefabInstanceMemberComponent>();
            e.GetComponent<PrefabInstanceMemberComponent>().IndexInPrefab = (uint32_t)i;
        }

        return true;
    }

}
