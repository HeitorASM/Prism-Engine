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
// O QUE ISTO NAO E (ainda): nao ha "prefab instance" vinculada ao arquivo
// original - instanciar um Prefab cria entidades NORMAIS na Scene, sem
// nenhum link de volta ao .prismprefab. Editar o Prefab depois NAO
// atualiza instancias ja colocadas em cenas (equivalente a "unpacked
// scene instance" da Godot, ou a quebrar o vinculo com o Prefab na Unity).
// Um sistema de instancias vinculadas/atualizacao em cascata e uma
// funcionalidade grande o bastante para ficar para uma iteracao futura -
// ver README/roadmap.
// ============================================================================

#include "../Core/Base.h"
#include "Entity.h"
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
        static Entity Instantiate(Scene& targetScene, const std::filesystem::path& filepath);
    };

}
