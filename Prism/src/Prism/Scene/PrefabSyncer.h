#pragma once

// ============================================================================
// PrefabSyncer.h
// Vinculo vivo entre uma instancia de prefab (subarvore marcada com
// PrefabInstanceRootComponent/PrefabInstanceMemberComponent - ver
// Components.h) e o .prismprefab de origem. Mesmo espirito de
// MaterialComponent::LinkedAsset (vinculo vivo de MATERIAL, ver
// MaterialSerializer.h), mas para uma ARVORE de entidades em vez de um
// struct unico - por isso precisa de uma logica dedicada em vez de so
// reler/regravar um arquivo.
//
// CONCEITO CENTRAL - "override por component inteiro": ao contrario de
// Godot/Unity (que rastreiam override CAMPO A CAMPO), aqui o que existe e
// TIPO DE COMPONENT inteiro. "Este TransformComponent da instancia diverge
// do prefab" e uma pergunta que da para responder com a infraestrutura
// ATUAL do ComponentRegistry (Has/Serialize/Copy, ver ComponentRegistry.h)
// sem adicionar nada nela. "So o campo Translation.x diverge" exigiria
// reflection campo a campo que o C++ nao da de graca (e o
// ComponentRegistry nao tem hoje) - fica para uma fase futura, se/quando
// fizer falta na pratica. Efeito pratico: mudar QUALQUER campo de um
// Component na instancia (ex: so a cor do MaterialComponent) marca O
// COMPONENT INTEIRO como overridado - sincronizar o prefab depois NAO
// mexe em nenhum campo daquele component na instancia, nem os que o
// usuario nao tocou.
//
// TRANSFORM: TransformComponent (como TagComponent) nao passa por
// ComponentRegistry::GetAll() - toda entidade tem os dois por definicao
// (ver Scene::CreateEntity e ComponentRegistration.cpp). Por isso o
// PrefabSyncer o trata como um component "sintetico" chamado "Transform",
// comparado/revertido/aplicado com codigo proprio. REGRA: so FILHOS da
// instancia (IndexInPrefab != 0) participam. A RAIZ nunca entra na
// comparacao - reposicionar a instancia na cena e o uso mais basico de um
// prefab e nao pode virar override permanente. Efeito: ajustar o
// transform de uma peca (filho) dentro do prefab e clicar em "Aplicar ao
// Prefab" grava no arquivo, e as outras instancias/cenas passam a receber
// esse transform ao Sincronizar ou ao instanciar de novo.
//
// COMO A COMPARACAO FUNCIONA (sem inflar o formato de arquivo): em vez de
// gravar uma "baseline" (copia dos valores no momento da instanciacao) no
// .prismmap - o que dobraria o tamanho de toda entidade de instancia -,
// PrefabSyncer RELE o .prismprefab de origem sob demanda (via
// PrefabSerializer, numa Scene TEMPORARIA em memoria, nunca a Scene real)
// toda vez que precisa comparar. A entidade de indice N nessa Scene
// temporaria e a "verdade atual do prefab" para o component com indice N
// (PrefabInstanceMemberComponent::IndexInPrefab, ver Components.h) na
// instancia. Cada Component e serializado (ComponentTypeInfo::Serialize,
// o MESMO callback que grava no .prismmap) para um buffer em memoria dos
// dois lados e comparado BYTE A BYTE - identico = nao overridado,
// diferente = overridado. Mais caro que comparar uma baseline em cache,
// mas simples, sempre correto (nunca dessincroniza de um cache), e so
// acontece quando o usuario pede Sync/Apply/Revert (nao a cada frame).
// ============================================================================

#include "Entity.h"
#include "../Assets/AssetID.h"
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace Prism {

    class Scene;

    // Um Component (por nome de exibicao - ver ComponentTypeInfo::
    // DisplayName) de uma entidade da instancia, e se ele diverge do
    // prefab atual. Preenchido por PrefabSyncer::Diff - a UI (ver
    // EditorLayer, secao Prefab do painel de propriedades) usa isto para
    // desenhar "3 components divergem, 5 batem" sem duplicar a logica de
    // comparacao.
    struct PrefabComponentDiff {
        Entity InstanceEntity;      // a entidade DENTRO da instancia (na Scene real)
        std::string ComponentName;  // ComponentTypeInfo::DisplayName
        bool Overridden = false;    // true = a instancia diverge do prefab neste component
        bool PresentInInstance = false; // false = a instancia NAO tem este Component (removido depois de instanciar)
        bool PresentInPrefab = false;   // false = o PREFAB nao tem mais este Component (removido no arquivo desde a instanciacao/ultimo sync)
    };

    // Resultado de comparar UMA instancia inteira (raiz + subarvore)
    // contra o .prismprefab de origem atual. Ver PrefabSyncer::Diff.
    struct PrefabDiffResult {
        bool Valid = false;         // false = a comparacao nao pode ser feita (ver ErrorMessage) - todos os outros campos ficam vazios/0 nesse caso
        std::string ErrorMessage;   // preenchido so quando Valid == false

        // Contagem estrutural: a subarvore da instancia e a do prefab tem
        // o MESMO NUMERO de entidades E na MESMA ordem/parentesco? Se
        // false, Diff() ainda preenche 'Components' para as entidades que
        // CONSEGUIU casar (por indice), mas UpdateAll()/quem chama deve
        // tratar isto como um aviso forte - o prefab foi editado de um
        // jeito que criou/removeu entidades na subarvore (ex: um novo
        // filho adicionado ao prefab), e sincronizar so os components
        // batidos por indice NAO traz esse filho novo para a instancia
        // (ver comentario em UpdateAll sobre este caso).
        bool StructureMatches = true;

        std::vector<PrefabComponentDiff> Components;

        // Atalhos calculados sobre 'Components' - convenientes para a UI
        // mostrar "X divergencia(s)" sem que o chamador precise iterar.
        int OverrideCount() const;
    };

    class PrefabSyncer {
    public:
        // Compara TODA a instancia cuja raiz e 'instanceRoot' (deve ter
        // PrefabInstanceRootComponent - ver Components.h) contra o
        // .prismprefab apontado por SourceAsset, resolvido via
        // 'assetRegistryRoot' + o caminho relativo do asset (ver
        // Assets/AssetRegistry.h - PrefabSyncer nao depende de Project
        // diretamente, para nao acoplar Scene/ a Project/; quem chama
        // resolve o caminho absoluto e passa pronto).
        //
        // 'instanceRoot' PRECISA ter PrefabInstanceRootComponent, senao
        // Valid=false (ErrorMessage explica). Entidades da subarvore sem
        // PrefabInstanceMemberComponent (ex: o usuario adicionou um filho
        // novo diretamente na instancia, que nunca existiu no prefab) sao
        // IGNORADAS na comparacao - nao entram em 'Components' nem
        // marcam StructureMatches=false, porque isto e uma adicao legitima
        // da instancia, nao uma divergencia estrutural do prefab (ver
        // UpdateAll: uma entidade assim nunca e tocada por
        // Update/UpdateAll, exatamente como uma entidade nova deveria se
        // comportar).
        static PrefabDiffResult Diff(Entity instanceRoot, const std::filesystem::path& prefabFilePath);

        // Aplica o valor ATUAL do prefab em UM Component especifico de UMA
        // entidade da instancia - equivalente a "Reverter para o prefab"
        // (descarta o override, mesmo que a instancia tivesse um valor
        // diferente). Sobrescreve com AddComponent+Copy se a instancia nao
        // tinha o Component; REMOVE o Component da instancia se o prefab
        // atual tambem nao tem mais (Component removido no prefab desde a
        // instanciacao). Nao mexe em NENHUM outro Component - chamador
        // decide quais reverter (ver UpdateAll para reverter todos os
        // NAO-overridados de uma vez).
        //
        // Retorna false se a leitura do prefab falhar ou 'componentName'
        // nao existir no ComponentRegistry - a instancia fica intocada
        // nesses casos.
        static bool RevertComponent(Entity instanceEntity, const std::filesystem::path& prefabFilePath, uint32_t indexInPrefab, const std::string& componentName);

        // Sincroniza a instancia inteira com o prefab ATUAL: para cada
        // Component NAO overridado (ver Diff), aplica o valor do prefab -
        // Components ja overridados na instancia NUNCA sao tocados (isto
        // e o que torna isto seguro de chamar livremente, ex: toda vez
        // que o editor detecta que o .prismprefab mudou no disco - mesmo
        // padrao de EditorLayer::ReconcileLinkedMaterial, mas aqui e
        // preciso reler/comparar em vez de so reler, por causa do override
        // por-component). NAO cria nem remove entidades - se
        // StructureMatches==false no Diff (o prefab ganhou/perdeu uma
        // entidade na subarvore desde a instanciacao), esta funcao ainda
        // sincroniza os components das entidades que CONSEGUEM ser
        // casadas por indice, mas a estrutura da arvore da instancia (que
        // entidades existem, quem e filho de quem) fica como estava -
        // trazer entidades novas/remover as que sumiram do prefab exige
        // recriar a instancia (ver EditorLayer::RenderPropertiesPanel,
        // secao Prefab, opcao "Recriar instancia") em vez de um sync
        // incremental.
        //
        // Retorna quantos Components foram efetivamente atualizados (0 se
        // nada precisava mudar, ou se a leitura falhou - ver PRISM_CORE_ERROR
        // ja logado nesse caso).
        static int UpdateAll(Entity instanceRoot, const std::filesystem::path& prefabFilePath);

        // Inverso de RevertComponent: pega o valor ATUAL deste Component
        // NA INSTANCIA e regrava no ARQUIVO do prefab (equivalente a
        // "Aplicar ao Prefab" da Unity) - top-down vira bottom-up. So
        // regrava o arquivo (nao mexe em nenhuma outra instancia deste
        // prefab que possa existir em outras cenas abertas - isto e
        // deliberado, ver comentario grande no .cpp sobre por que
        // propagar automaticamente para OUTRAS instancias, como
        // ReconcileLinkedMaterial faz para material, nao e seguro aqui).
        // Depois de chamar isto, o Component deixa de estar overridado
        // (um proximo Diff() vai compara-lo como igual, ja que agora o
        // arquivo TEM o valor que a instancia ja tinha).
        //
        // Retorna false se a leitura/escrita do prefab falhar.
        static bool ApplyComponentToPrefab(Entity instanceEntity, const std::filesystem::path& prefabFilePath, const std::string& componentName);

        // "Aplicar Estrutura ao Prefab": regrava o .prismprefab INTEIRO a
        // partir da instancia atual - inclui entidades ADICIONADAS a
        // instancia (que o Diff ignora por nao terem
        // PrefabInstanceMemberComponent) e omite as REMOVIDAS. Depois de
        // gravar, remarca PrefabInstanceMemberComponent::IndexInPrefab de
        // TODA a subarvore da instancia na mesma pre-ordem do arquivo novo,
        // para que a instancia volte a casar 1:1 com o prefab
        // (StructureMatches == true) e o filho novo passe a ser rastreado.
        //
        // O Transform da RAIZ gravado no arquivo e o que o prefab JA tinha
        // (a posicao da instancia na cena nunca vaza para o prefab - mesma
        // regra de "raiz nunca entra na comparacao"). Se o arquivo antigo
        // nao puder ser lido, usa Transform padrao (origem/sem rotacao/
        // escala 1).
        //
        // Nao mexe em OUTRAS instancias deste prefab (outras cenas):
        // elas continuam com a estrutura antiga ate serem recriadas -
        // mesma limitacao ja documentada em UpdateAll.
        //
        // Retorna false se a escrita falhar (a instancia fica intocada).
        static bool ApplyStructureToPrefab(Entity instanceRoot, const std::filesystem::path& prefabFilePath);

    private:
        // Preenche 'out' com 'entity' e toda a sua subarvore em
        // PRE-ORDEM (a propria entidade primeiro, depois cada filho e a
        // subarvore dele, na ordem de GetChildAt - ver Entity.h), que e
        // EXATAMENTE a mesma ordem que PrefabSerializer::CollectSubtree
        // usa ao gravar um .prismprefab (ver PrefabSerializer.cpp) - por
        // isso o indice i nesta lista sempre bate com
        // PrefabInstanceMemberComponent::IndexInPrefab == i, seja do lado
        // do prefab (via LoadPrefabForComparison) ou do lado da instancia
        // REAL na Scene do editor (Diff() chama isto para os dois lados
        // com a MESMA funcao, garantindo que a ordem nunca diverge entre
        // eles por acidente).
        static void CollectSubtreePreOrder(Entity entity, std::vector<Entity>& out);

        // Le 'prefabFilePath' inteiro para uma Scene temporaria em
        // memoria (nunca a Scene real do editor - ver comentario grande
        // no topo do arquivo) via PrefabSerializer::Instantiate, mas
        // SEM AssetID (sourceAsset invalido - ver PrefabSerializer.h) -
        // esta copia e so material de comparacao, nunca deveria ela
        // mesma parecer uma instancia vinculada. 'outScene' segura a
        // Scene temporaria viva (RAII - ela e destruida quando o
        // Ref<Scene> sai de escopo no chamador); 'outEntities' e a lista
        // de entidades na MESMA ordem posicional do arquivo (indice i =
        // PrefabInstanceMemberComponent::IndexInPrefab == i).
        static bool LoadPrefabForComparison(const std::filesystem::path& prefabFilePath, Ref<Scene>& outScene, std::vector<Entity>& outEntities);

        // Serializa TODOS os Components presentes em 'entity' que passam
        // por ComponentRegistry (ver ComponentRegistry.h) para um buffer
        // em memoria POR NOME DE COMPONENT - usado por Diff() para
        // comparar byte a byte. Um Component ausente simplesmente nao
        // aparece no mapa resultante (equivalente a Has()==false).
        // TransformComponent/TagComponent NUNCA aparecem aqui - ver
        // "LIMITACAO CONHECIDA" no topo deste arquivo.
        static std::unordered_map<std::string, std::vector<char>> SerializeAllComponents(Entity entity, bool includeTransform);
    };

}
