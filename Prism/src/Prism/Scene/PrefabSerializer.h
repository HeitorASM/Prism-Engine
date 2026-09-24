#pragma once

// ============================================================================
// PrefabSerializer.h
// Um Prefab e uma entidade (e sua subarvore de filhos) salva em disco para
// ser REUTILIZADA em varias cenas - o mesmo conceito de "Prefab" da Unity
// ou "PackedScene" da Godot. Arquivos ficam em Project::GetAssetDirectory()
// / "Prefabs", extensao .prismprefab.
//
// POR QUE UM ARQUIVO SEPARADO EM VEZ DE SO SALVAR UM .prismmap DE 1
// ENTIDADE: um Prefab precisa poder ser INSTANCIADO dentro de uma Scene
// que ja tem outras entidades (ver PrefabSerializer::Instantiate) - a
// diferenca central para SceneSerializer e que este NUNCA cria uma Scene
// nova/substitui a Scene ativa, so adiciona uma subarvore nova dentro de
// uma Scene que ja existe. O FORMATO de arquivo em si e deliberadamente
// quase identico ao de SceneSerializer (mesmo cabecalho, mesmo uso de
// ComponentRegistry por entidade, mesmo esquema de indice-de-pai para
// RelationshipComponent) - ver comentario no .cpp para os detalhes campo a
// campo, praticamente o mesmo "por entidade" que SceneSerializer ja usa,
// so que para N < total-da-cena entidades (a raiz + seus descendentes) em
// vez de TODAS as entidades da Scene.
//
// VINCULO VIVO com instancias (ver Scene/PrefabSyncer.h para a logica
// completa): Instantiate() agora tambem marca a raiz criada com
// PrefabInstanceRootComponent (o AssetID deste .prismprefab) e TODA
// entidade da subarvore (raiz inclusive) com PrefabInstanceMemberComponent
// (o indice posicional dela dentro deste arquivo - ver Components.h).
// Esta classe (PrefabSerializer) continua sem saber NADA sobre
// sincronizacao - so gera/consome o arquivo em si; PrefabSyncer e quem
// usa esses dois components depois para decidir "o que mudou no arquivo
// desde a instanciacao, e o que a instancia overridou por conta propria".
// Um .prismprefab em si NUNCA contem PrefabInstanceRootComponent/
// PrefabInstanceMemberComponent (eles nao passam por ComponentRegistry,
// ver Components.h) - mesmo salvando um prefab a partir de uma entidade
// que ela mesma e instancia de outro prefab (aninhamento), o arquivo
// resultante fica limpo desses dois components.
// ============================================================================

#include "../Core/Base.h"
#include "Entity.h"
#include "../Assets/AssetID.h"
#include <filesystem>

namespace Prism {

    class Scene;

    class PrefabSerializer {
    public:
        // Salva 'root' (e toda a sua subarvore de filhos) em 'filepath'.
        // A posicao/rotacao/escala de 'root' E GRAVADA como veio (nao e
        // zerada) - ao instanciar de volta (ver Instantiate), quem chama
        // decide se reposiciona a raiz ou mantem o transform salvo (ver
        // comentario em Instantiate).
        static bool Serialize(Entity root, const std::filesystem::path& filepath);

        // Le 'filepath' e cria a subarvore inteira dentro de 'targetScene'
        // (SEM reparentar a raiz para nada por padrao - a raiz nasce
        // RAIZ da Scene, mesmo comportamento de arrastar um Prefab para
        // dentro da Hierarchy panel na Godot/Unity). Retorna a entidade
        // RAIZ recem-criada (Entity invalida em caso de falha, e nenhuma
        // entidade e criada nesse caso - falha e tudo-ou-nada, igual
        // SceneSerializer::Deserialize).
        //
        // 'sourceAsset': o AssetID (ver Assets/AssetID.h) deste MESMO
        // .prismprefab, resolvido pelo CHAMADOR via
        // Project::GetAssetRegistry() - PrefabSerializer nao conhece
        // Project/AssetRegistry (ficaria acoplado ao editor), entao nao
        // resolve isso sozinho. Se valido, a raiz criada ganha
        // PrefabInstanceRootComponent (ver Components.h e
        // Scene/PrefabSyncer.h) e toda a subarvore ganha
        // PrefabInstanceMemberComponent com o indice posicional dela
        // dentro deste arquivo - e isto que liga a instancia ao arquivo
        // para PrefabSyncer sincronizar depois. Invalido (o default) =
        // instancia SEM vinculo, exatamente o comportamento de antes do
        // vinculo vivo existir (uma subarvore de entidades comuns, sem
        // nenhum link de volta ao arquivo).
        static Entity Instantiate(Scene& targetScene, const std::filesystem::path& filepath, AssetID sourceAsset = {});
    };

}
