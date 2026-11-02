#include "EntityClipboard.h"

namespace PrismEditor::EntityClipboard {

    namespace {
        Prism::Ref<Prism::Scene> s_Scene;
        Prism::Entity s_Root;
    }

    void Copy(Prism::Entity entity) {
        if (!entity.IsValid())
            return;
        // Cria a copia ANTES de largar a anterior: copiar uma entidade que veio da propria
        // area de transferencia (improvavel, mas possivel) nao pode apontar para uma Scene morta.
        Prism::Ref<Prism::Scene> scene = Prism::Scene::Create("__clipboard__");
        Prism::Entity root = scene->CopyEntityTreeFrom(entity, /*exact*/ true);
        if (!root.IsValid())
            return;
        s_Scene = std::move(scene);
        s_Root = root;
    }

    bool HasContent() {
        return s_Scene && s_Root.IsValid();
    }

    Prism::Entity Root() {
        return HasContent() ? s_Root : Prism::Entity();
    }

    std::string Label() {
        if (!HasContent() || !s_Root.HasComponent<Prism::TagComponent>())
            return {};
        return s_Root.GetComponent<Prism::TagComponent>().Tag;
    }

    void Clear() {
        s_Root = {};
        s_Scene.reset();
    }

}
