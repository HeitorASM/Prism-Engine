#include "Scene.h"
#include "Entity.h"

namespace Prism {

    Scene::Scene(const std::string& name) : m_Name(name) {}

    Ref<Scene> Scene::Create(const std::string& name) {
        return CreateRef<Scene>(name);
    }

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity(m_Registry.create(), this);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<TagComponent>(name.empty() ? std::string("Entity") : name);
        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        m_Registry.destroy(entity.GetHandle());
    }

    void Scene::OnUpdate(float deltaTime) {
        // TODO(fase seguinte): scripts (Lua) e fisica (Box3D) vao atualizar
        // TransformComponent das entidades relevantes aqui. Por ora a Scene
        // e estatica - so o EditorLayer gira a entidade de teste manualmente
        // para fins de demonstracao visual (ver EditorLayer::RenderScene).
    }

}
