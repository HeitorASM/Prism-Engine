#include "EntityOps.h"
#include "../Commands/EditorCommands.h"
#include <algorithm>

namespace PrismEditor::EntityOps {

    void SyncAllPrefabInstances(EditorContext& ctx) {
        // Propaga mudancas feitas no(s) .prismprefab desde a ultima vez
        // que este mapa foi salvo: toda instancia (PrefabInstanceRootComponent)
        // na Scene recem-carregada e sincronizada com o arquivo atual, na
        // hora do carregamento (nao a cada frame como ReconcileLinkedMaterial
        // faz para material - Diff/UpdateAll releem o arquivo inteiro numa
        // Scene temporaria, mais caro que o simples stat de mtime que o
        // material usa, entao rodar isto continuamente pesaria para
        // prefabs grandes/muitas instancias). PrefabSyncer::UpdateAll
        // preserva os overrides (Components ja divergentes na instancia
        // NUNCA sao tocados - ver PrefabSyncer.h), entao isto e seguro de
        // chamar mesmo que a instancia tenha edicoes locais nao aplicadas.
        //
        // NAO usa CommandHistory aqui - mesmo raciocinio de
        // ReconcileLinkedMaterial: isto e sincronizacao de baixo nivel no
        // MOMENTO DE ABRIR o mapa, nao uma acao do usuario para desfazer
        // com Ctrl+Z (que neste ponto ainda nem tem historico - ver
        // EditorContext::History.Clear() logo acima).
        auto project = Prism::Project::GetActive();
        if (!project)
            return;

        auto view = ctx.ActiveScene->GetRegistry().view<Prism::PrefabInstanceRootComponent>();
        int updatedInstances = 0;
        for (auto handle : view) {
            Prism::Entity instanceRoot(handle, ctx.ActiveScene.get());
            auto& root = view.get<Prism::PrefabInstanceRootComponent>(handle);
            std::filesystem::path prefabPath = project->GetAssetRegistry().AbsolutePath(root.SourceAsset);
            if (prefabPath.empty())
                continue; // vinculo quebrado (arquivo sumiu) - painel Prefab ja avisa isto na UI, nada a fazer aqui

            int updatedComponents = Prism::PrefabSyncer::UpdateAll(instanceRoot, prefabPath);
            if (updatedComponents > 0)
                updatedInstances++;
        }

        if (updatedInstances > 0)
            PRISM_INFO(updatedInstances, " instancia(s) de prefab atualizada(s) ao carregar o mapa (mudancas nao-overridadas do(s) prefab(s) de origem).");
    }

    void InstantiatePrefab(EditorContext& ctx, const std::filesystem::path& prefabPath, Prism::Entity parent) {
        auto command = Prism::CreateScope<InstantiatePrefabCommand>(ctx.ActiveScene, prefabPath);
        InstantiatePrefabCommand* raw = command.get();
        ctx.History.Execute(std::move(command));
        Prism::Entity instantiated = raw->GetInstantiatedEntity();

        if (instantiated && parent)
            ctx.History.Execute(Prism::CreateScope<SetParentCommand>(ctx.ActiveScene, instantiated, Prism::Entity{}, parent));

        ctx.SelectedEntity = instantiated;
    }

    void DuplicateEntity(EditorContext& ctx, Prism::Entity entity) {
        if (!entity)
            return;
        auto command = Prism::CreateScope<DuplicateEntityCommand>(ctx.ActiveScene, entity);
        DuplicateEntityCommand* raw = command.get();
        ctx.History.Execute(std::move(command));
        ctx.SelectedEntity = raw->GetDuplicatedEntity();
    }

    void DeleteEntity(EditorContext& ctx, Prism::Entity entity) {
        if (!entity)
            return;
        ctx.History.Execute(Prism::CreateScope<DeleteEntityCommand>(ctx.ActiveScene, entity));
        if (ctx.SelectedEntity == entity)
            ctx.SelectedEntity = {};
    }

    void SetPrimaryCamera(EditorContext& ctx, Prism::Entity newPrimary) {
        auto view = ctx.ActiveScene->GetRegistry().view<Prism::CameraComponent>();
        for (auto entityHandle : view) {
            Prism::Entity entity(entityHandle, ctx.ActiveScene.get());
            entity.GetComponent<Prism::CameraComponent>().Primary = (entity == newPrimary);
        }
    }

}
