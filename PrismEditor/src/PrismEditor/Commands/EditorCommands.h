#pragma once

// ============================================================================
// EditorCommands.h
// Commands concretos (ver Prism::Command / Prism::CommandHistory) usados
// pelo EditorLayer. Ficam do lado do Editor (nao da engine) porque
// conhecem o fluxo de edicao especifico: qual Entity, qual Scene, etc.
//
// Quatro comandos concretos + dois templates genericos (Add/RemoveComponent):
//   - TransformCommand: mudanca de posicao/rotacao/escala de UMA entidade
//     (usado pelos DragFloat3 da Properties panel - ver EditorLayer.cpp)
//   - MeshColorCommand: mudanca de cor do MeshRendererComponent
//   - CreateEntityCommand: criar uma entidade nova (usado por "Criar Cubo")
//   - CreatePresetEntityCommand: versao generica para os outros presets do
//     menu Entidade (Criar Luz, Criar Character, Criar Entidade Vazia) -
//     recebe uma funcao de setup em vez de ter uma classe por preset
//   - DeleteEntityCommand: excluir uma entidade existente
//   - AddComponentCommand<T>/RemoveComponentCommand<T>: adicionar/remover
//     qualquer component "opcional" (Light, Collider, RigidBody, Script) -
//     ver EditorLayer::RenderPropertiesPanel para o botao "Add Component".
//
// Novos tipos de edicao seguem o mesmo padrao: guardar o estado "antes" no
// construtor, aplicar em Execute(), reverter em Undo().
// ============================================================================

#include <Prism.h>
#include <string>
#include <utility>
#include <functional>
#include <filesystem>

namespace PrismEditor {

    // Comando generico de "editar o TransformComponent de uma entidade".
    // Guarda o transform ANTES e o transform DEPOIS da edicao - construido
    // uma unica vez, quando o usuario TERMINA de arrastar um DragFloat3 no
    // ImGui (nao a cada frame do arraste - ver EditorLayer::RenderPropertiesPanel,
    // que usa IsItemActivated()/IsItemDeactivatedAfterEdit() para capturar
    // exatamente o inicio e o fim do gesto).
    class TransformCommand : public Prism::Command {
    public:
        TransformCommand(Prism::Entity entity, const Prism::TransformComponent& before, const Prism::TransformComponent& after)
            : m_Entity(entity), m_Before(before), m_After(after) {}

        void Execute() override {
            if (m_Entity.HasComponent<Prism::TransformComponent>())
                m_Entity.GetComponent<Prism::TransformComponent>() = m_After;
        }

        void Undo() override {
            if (m_Entity.HasComponent<Prism::TransformComponent>())
                m_Entity.GetComponent<Prism::TransformComponent>() = m_Before;
        }

        std::string GetName() const override { return "Transformar Entidade"; }

    private:
        Prism::Entity m_Entity;
        Prism::TransformComponent m_Before;
        Prism::TransformComponent m_After;
    };

    // Comando generico de "editar a cor do MeshRendererComponent" - mesmo
    // padrao do TransformCommand acima, so que para o ColorEdit3 da
    // Properties panel.
    class MeshColorCommand : public Prism::Command {
    public:
        MeshColorCommand(Prism::Entity entity, const glm::vec3& before, const glm::vec3& after)
            : m_Entity(entity), m_Before(before), m_After(after) {}

        void Execute() override {
            if (m_Entity.HasComponent<Prism::MeshRendererComponent>())
                m_Entity.GetComponent<Prism::MeshRendererComponent>().Color = m_After;
        }

        void Undo() override {
            if (m_Entity.HasComponent<Prism::MeshRendererComponent>())
                m_Entity.GetComponent<Prism::MeshRendererComponent>().Color = m_Before;
        }

        std::string GetName() const override { return "Mudar Cor"; }

    private:
        Prism::Entity m_Entity;
        glm::vec3 m_Before;
        glm::vec3 m_After;
    };

    // Cria uma entidade com MeshRendererComponent (o unico tipo de entidade
    // que o menu "Entidade > Criar Cubo" produz hoje). Execute() cria de
    // novo com os MESMOS dados sempre que for chamado (inclusive em um
    // Redo(), depois de um Undo() ter destruido a entidade) - por isso
    // guardamos os dados iniciais em vez da Entity em si, que deixa de ser
    // valida apos Undo().
    class CreateEntityCommand : public Prism::Command {
    public:
        CreateEntityCommand(Prism::Ref<Prism::Scene> scene, const std::string& name, const glm::vec3& color)
            : m_Scene(scene), m_Name(name), m_Color(color) {}

        void Execute() override {
            Prism::Entity entity = m_Scene->CreateEntity(m_Name);
            auto& mesh = entity.AddComponent<Prism::MeshRendererComponent>();
            mesh.Color = m_Color;
            m_CreatedEntity = entity;
        }

        void Undo() override {
            if (m_CreatedEntity)
                m_Scene->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = {};
        }

        std::string GetName() const override { return "Criar Entidade"; }

        // Valido apos Execute(), ate o proximo Undo() - usado pelo chamador
        // (EditorLayer) para selecionar a entidade recem-criada.
        Prism::Entity GetCreatedEntity() const { return m_CreatedEntity; }

    private:
        Prism::Ref<Prism::Scene> m_Scene;
        std::string m_Name;
        glm::vec3 m_Color;
        Prism::Entity m_CreatedEntity;
    };

    // Versao generica de CreateEntityCommand para os PRESETS do menu
    // Entidade (Criar Luz, Criar Character, Criar Entidade Vazia, etc) -
    // em vez de uma classe de comando por preset, este recebe uma funcao
    // de "setup" que adiciona os components que aquele preset representa.
    // Isso e so uma conveniencia de criacao - o resultado e uma entidade
    // com components normais, editavel/removivel como qualquer outra pela
    // Properties panel depois (ver EditorLayer::RenderAddComponentButton).
    // Undo() destroi a entidade inteira (nao desfaz component por
    // component) - do ponto de vista do historico, "criar um preset" e
    // uma unica acao atomica.
    class CreatePresetEntityCommand : public Prism::Command {
    public:
        using SetupFn = std::function<void(Prism::Entity)>;

        CreatePresetEntityCommand(Prism::Ref<Prism::Scene> scene, std::string name, std::string presetLabel, SetupFn setup)
            : m_Scene(scene), m_Name(std::move(name)), m_PresetLabel(std::move(presetLabel)), m_Setup(std::move(setup)) {}

        void Execute() override {
            Prism::Entity entity = m_Scene->CreateEntity(m_Name);
            if (m_Setup)
                m_Setup(entity);
            m_CreatedEntity = entity;
        }

        void Undo() override {
            if (m_CreatedEntity)
                m_Scene->DestroyEntity(m_CreatedEntity);
            m_CreatedEntity = {};
        }

        std::string GetName() const override { return "Criar " + m_PresetLabel; }

        Prism::Entity GetCreatedEntity() const { return m_CreatedEntity; }

    private:
        Prism::Ref<Prism::Scene> m_Scene;
        std::string m_Name;
        std::string m_PresetLabel;
        SetupFn m_Setup;
        Prism::Entity m_CreatedEntity;
    };

    // Exclui uma entidade existente. Guarda uma copia de TODOS os
    // components relevantes ANTES de destruir, para que Undo() recrie a
    // entidade identica. Se novos tipos de component forem adicionados a
    // Entity (fisica, scripts), este e o lugar a atualizar junto.
    class DeleteEntityCommand : public Prism::Command {
    public:
        DeleteEntityCommand(Prism::Ref<Prism::Scene> scene, Prism::Entity entity)
            : m_Scene(scene) {
            m_Tag = entity.GetComponent<Prism::TagComponent>().Tag;
            m_Transform = entity.GetComponent<Prism::TransformComponent>();

            m_HadMesh = entity.HasComponent<Prism::MeshRendererComponent>();
            if (m_HadMesh)
                m_Mesh = entity.GetComponent<Prism::MeshRendererComponent>();

            m_HadLight = entity.HasComponent<Prism::LightComponent>();
            if (m_HadLight)
                m_Light = entity.GetComponent<Prism::LightComponent>();

            m_HadCollider = entity.HasComponent<Prism::ColliderComponent>();
            if (m_HadCollider)
                m_Collider = entity.GetComponent<Prism::ColliderComponent>();

            m_HadRigidBody = entity.HasComponent<Prism::RigidBodyComponent>();
            if (m_HadRigidBody)
                m_RigidBody = entity.GetComponent<Prism::RigidBodyComponent>();

            m_HadScript = entity.HasComponent<Prism::ScriptComponent>();
            if (m_HadScript)
                m_Script = entity.GetComponent<Prism::ScriptComponent>();

            m_HadCamera = entity.HasComponent<Prism::CameraComponent>();
            if (m_HadCamera)
                m_Camera = entity.GetComponent<Prism::CameraComponent>();

            m_EntityToDelete = entity;
        }

        void Execute() override {
            if (m_EntityToDelete)
                m_Scene->DestroyEntity(m_EntityToDelete);
            m_EntityToDelete = {};
        }

        void Undo() override {
            Prism::Entity entity = m_Scene->CreateEntity(m_Tag);
            entity.GetComponent<Prism::TransformComponent>() = m_Transform;
            if (m_HadMesh)
                entity.AddComponent<Prism::MeshRendererComponent>() = m_Mesh;
            if (m_HadLight)
                entity.AddComponent<Prism::LightComponent>() = m_Light;
            if (m_HadCollider)
                entity.AddComponent<Prism::ColliderComponent>() = m_Collider;
            if (m_HadRigidBody)
                entity.AddComponent<Prism::RigidBodyComponent>() = m_RigidBody;
            if (m_HadScript)
                entity.AddComponent<Prism::ScriptComponent>() = m_Script;
            if (m_HadCamera)
                entity.AddComponent<Prism::CameraComponent>() = m_Camera;
            m_EntityToDelete = entity;
        }

        std::string GetName() const override { return "Excluir Entidade"; }

    private:
        Prism::Ref<Prism::Scene> m_Scene;
        Prism::Entity m_EntityToDelete;

        std::string m_Tag;
        Prism::TransformComponent m_Transform;

        bool m_HadMesh = false;
        Prism::MeshRendererComponent m_Mesh;
        bool m_HadLight = false;
        Prism::LightComponent m_Light;
        bool m_HadCollider = false;
        Prism::ColliderComponent m_Collider;
        bool m_HadRigidBody = false;
        Prism::RigidBodyComponent m_RigidBody;
        bool m_HadScript = false;
        Prism::ScriptComponent m_Script;
        bool m_HadCamera = false;
        Prism::CameraComponent m_Camera;
    };

    // Duplica uma entidade (e toda a sua subarvore de filhos) via
    // Scene::DuplicateEntity - ver Scene.h/.cpp para o motivo desta logica
    // viver la (reaproveita ComponentRegistry::Copy, mesmo mecanismo que
    // Scene::Clone ja usa, entao cobre QUALQUER Component registrado sem
    // precisar de uma lista fixa como o antigo DeleteEntityCommand tinha).
    // Guardamos so o handle 'original' (nao os dados dela) porque
    // Execute()/Redo() sempre duplicam de novo a partir do estado ATUAL
    // do original - se o original for editado entre um Undo() e um Redo()
    // deste comando, o Redo() reflete a edicao mais recente, igual o
    // comportamento esperado de "refazer" em qualquer editor.
    class DuplicateEntityCommand : public Prism::Command {
    public:
        DuplicateEntityCommand(Prism::Ref<Prism::Scene> scene, Prism::Entity original)
            : m_Scene(scene), m_Original(original) {}

        void Execute() override {
            if (m_Original)
                m_Duplicate = m_Scene->DuplicateEntity(m_Original);
        }

        void Undo() override {
            if (m_Duplicate)
                m_Scene->DestroyEntity(m_Duplicate);
            m_Duplicate = {};
        }

        std::string GetName() const override { return "Duplicar Entidade"; }

        // Valido apos Execute(), ate o proximo Undo() - usado pelo
        // chamador (EditorLayer) para selecionar a copia recem-criada.
        Prism::Entity GetDuplicatedEntity() const { return m_Duplicate; }

    private:
        Prism::Ref<Prism::Scene> m_Scene;
        Prism::Entity m_Original;
        Prism::Entity m_Duplicate;
    };

    // Instancia um prefab (ver Prism::PrefabSerializer, Prism/Scene/
    // PrefabSerializer.h) dentro da Scene ativa - usado pelo drag-and-drop
    // de um .prismprefab do Content Browser para a Hierarchy/Viewport (ver
    // EditorLayer::InstantiatePrefab). Guardamos so o CAMINHO do arquivo
    // (nao os dados), pelo mesmo motivo de DuplicateEntityCommand: um
    // Redo() sempre le o arquivo de novo do disco, entao reflete qualquer
    // edicao feita no prefab entre o Undo() e o Redo() deste comando -
    // mesmo comportamento que se espera de "refazer" em qualquer editor.
    class InstantiatePrefabCommand : public Prism::Command {
    public:
        InstantiatePrefabCommand(Prism::Ref<Prism::Scene> scene, std::filesystem::path prefabPath)
            : m_Scene(scene), m_PrefabPath(std::move(prefabPath)) {}

        void Execute() override {
            m_Instantiated = Prism::PrefabSerializer::Instantiate(*m_Scene, m_PrefabPath);
        }

        void Undo() override {
            if (m_Instantiated)
                m_Scene->DestroyEntity(m_Instantiated);
            m_Instantiated = {};
        }

        std::string GetName() const override { return "Instanciar Prefab"; }

        // Valido apos Execute(), ate o proximo Undo() - usado pelo
        // chamador (EditorLayer) para selecionar a raiz recem-instanciada.
        Prism::Entity GetInstantiatedEntity() const { return m_Instantiated; }

    private:
        Prism::Ref<Prism::Scene> m_Scene;
        std::filesystem::path m_PrefabPath;
        Prism::Entity m_Instantiated;
    };

    // Carrega um Material Asset (.prismmat - ver Prism::MaterialSerializer,
    // Prism/Scene/MaterialSerializer.h) sobre o MaterialComponent de uma
    // entidade - usado pelo botao "Carregar de Asset" e pelo drag-and-drop
    // de um .prismmat sobre o painel Material (ver EditorLayer.cpp). Ao
    // contrario de DuplicateEntityCommand/InstantiatePrefabCommand (que
    // recriam do zero no Redo), aqui guardamos os campos ANTIGOS do
    // material explicitamente: MaterialSerializer::Deserialize so
    // preenche campos de textura/fatores, nao ha uma forma natural de
    // "desfazer" um Load senao devolvendo o valor de antes.
    //
    // VINCULO VIVO (ver MaterialComponent::LinkedAsset, Components.h):
    // este comando tambem LIGA o vinculo, resolvendo 'assetPath' para um
    // AssetID via Project::GetAssetRegistry() (etapa 1 - ver
    // Assets/AssetRegistry.h). Se o caminho nao resolver para nenhum
    // asset conhecido (fora da pasta de Assets do projeto, ou um Refresh
    // ainda nao rodou sobre um arquivo recem-criado por fora do editor),
    // os campos ainda sao carregados normalmente, so que SEM vinculo -
    // equivalente ao comportamento de antes desta funcionalidade existir.
    class LoadMaterialAssetCommand : public Prism::Command {
    public:
        LoadMaterialAssetCommand(Prism::Entity entity, std::filesystem::path assetPath)
            : m_Entity(entity), m_AssetPath(std::move(assetPath)) {}

        void Execute() override {
            if (!m_Entity || !m_Entity.HasComponent<Prism::MaterialComponent>())
                return;
            m_Before = m_Entity.GetComponent<Prism::MaterialComponent>();
            Prism::MaterialComponent loaded;
            if (Prism::MaterialSerializer::Deserialize(m_AssetPath, loaded)) {
                loaded.LinkedAsset = ResolveAssetID();
                m_Entity.GetComponent<Prism::MaterialComponent>() = loaded;
                m_Loaded = true;
            }
        }

        void Undo() override {
            if (m_Loaded && m_Entity && m_Entity.HasComponent<Prism::MaterialComponent>())
                m_Entity.GetComponent<Prism::MaterialComponent>() = m_Before;
        }

        std::string GetName() const override { return "Carregar Material de Asset"; }

    private:
        // AssetID{} (invalido) se nao houver Project ativo ou o caminho
        // nao resolver para um asset conhecido - Execute() ja trata isso
        // como "sem vinculo", entao esta funcao nunca precisa falhar,
        // so devolver invalido nesses casos.
        Prism::AssetID ResolveAssetID() const {
            auto project = Prism::Project::GetActive();
            if (!project)
                return {};
            std::string relative = project->GetAssetRegistry().ToRelative(m_AssetPath);
            if (relative.empty())
                return {};
            return project->GetAssetRegistry().IdForPath(relative);
        }

        Prism::Entity m_Entity;
        std::filesystem::path m_AssetPath;
        Prism::MaterialComponent m_Before;
        bool m_Loaded = false; // false se Deserialize falhou (arquivo corrompido/inexistente) - Undo() nao faz nada nesse caso, pois Execute() tambem nao mudou nada
    };

    // Reparenta uma entidade (drag-and-drop na Hierarchy panel - ver
    // EditorLayer::RenderHierarchyNode). Guarda o pai ANTIGO no construtor
    // (antes de qualquer mudanca) para Undo() devolver exatamente para o
    // mesmo lugar na arvore, mesmo que a entidade tenha sido movida varias
    // vezes depois - cada movimento e seu proprio comando no historico.
    class SetParentCommand : public Prism::Command {
    public:
        SetParentCommand(Prism::Ref<Prism::Scene> scene, Prism::Entity child, Prism::Entity oldParent, Prism::Entity newParent)
            : m_Scene(scene), m_Child(child), m_OldParent(oldParent), m_NewParent(newParent) {}

        void Execute() override {
            m_Scene->SetParent(m_Child, m_NewParent);
        }

        void Undo() override {
            m_Scene->SetParent(m_Child, m_OldParent);
        }

        std::string GetName() const override { return "Reparentar Entidade"; }

    private:
        Prism::Ref<Prism::Scene> m_Scene;
        Prism::Entity m_Child;
        Prism::Entity m_OldParent;
        Prism::Entity m_NewParent;
    };

    // --- Add/Remove Component genericos -------------------------------
    // Um unico par de templates cobre os quatro components "opcionais"
    // novos (Light, Collider, RigidBody, Script) em vez de escrever a
    // mesma logica AddComponent/RemoveComponent quatro vezes. Funciona
    // para qualquer T que seja copiavel e tenha um construtor default -
    // exatamente o contrato que todo Component em Components.h ja segue.

    // Adiciona T() (valores default) a entidade. Undo() remove de volta.
    // Usado pelo botao "Add Component" da Properties panel - ver
    // EditorLayer::RenderPropertiesPanel.
    template<typename T>
    class AddComponentCommand : public Prism::Command {
    public:
        AddComponentCommand(Prism::Entity entity, std::string displayName)
            : m_Entity(entity), m_DisplayName(std::move(displayName)) {}

        void Execute() override {
            if (!m_Entity.HasComponent<T>())
                m_Entity.AddComponent<T>();
        }

        void Undo() override {
            if (m_Entity.HasComponent<T>())
                m_Entity.RemoveComponent<T>();
        }

        std::string GetName() const override { return "Adicionar " + m_DisplayName; }

    private:
        Prism::Entity m_Entity;
        std::string m_DisplayName;
    };

    // Remove T da entidade, guardando uma copia ANTES de remover para que
    // Undo() restaure os valores exatos que o usuario tinha configurado
    // (nao um T() generico) - diferente de AddComponentCommand, que nao
    // precisa disso porque "desfazer um Add" e so tirar o component de
    // novo, sem valores para preservar.
    template<typename T>
    class RemoveComponentCommand : public Prism::Command {
    public:
        RemoveComponentCommand(Prism::Entity entity, std::string displayName)
            : m_Entity(entity), m_DisplayName(std::move(displayName)) {
            m_Backup = entity.GetComponent<T>();
        }

        void Execute() override {
            if (m_Entity.HasComponent<T>())
                m_Entity.RemoveComponent<T>();
        }

        void Undo() override {
            if (!m_Entity.HasComponent<T>())
                m_Entity.AddComponent<T>(m_Backup);
        }

        std::string GetName() const override { return "Remover " + m_DisplayName; }

    private:
        Prism::Entity m_Entity;
        std::string m_DisplayName;
        T m_Backup;
    };

}
