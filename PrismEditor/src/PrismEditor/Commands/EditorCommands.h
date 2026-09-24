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
            // VINCULO VIVO de prefab (ver Scene/PrefabSyncer.h e
            // Components.h): resolve o AssetID de m_PrefabPath via
            // Project::GetAssetRegistry() (etapa 1) e passa para
            // Instantiate - mesmo padrao de
            // LoadMaterialAssetCommand::ResolveAssetID logo abaixo. Se
            // nao resolver (fora da pasta Assets, ou um Refresh ainda nao
            // rodou sobre um prefab recem-criado por fora do editor), a
            // instancia nasce SEM vinculo - equivalente ao comportamento
            // de antes do vinculo vivo de prefab existir.
            m_Instantiated = Prism::PrefabSerializer::Instantiate(*m_Scene, m_PrefabPath, ResolveAssetID());
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
        Prism::AssetID ResolveAssetID() const {
            auto project = Prism::Project::GetActive();
            if (!project)
                return {};
            auto& registry = project->GetAssetRegistry();
            std::string relative = registry.ToRelative(m_PrefabPath);
            if (relative.empty()) {
                PRISM_WARN("Prefab fora da pasta de Assets do projeto - a instancia sera criada SEM vinculo com o arquivo: ", m_PrefabPath.string());
                return {};
            }
            Prism::AssetID id = registry.IdForPath(relative);
            if (!id.IsValid()) {
                // Prefab criado/copiado depois do ultimo Refresh (ex: por
                // fora do editor, ou por qualquer caminho que nao chame
                // Refresh). Varre de novo antes de desistir - senao a
                // instancia nasce sem PrefabInstanceRootComponent e o
                // painel Prefab nunca aparece.
                registry.Refresh();
                id = registry.IdForPath(relative);
            }
            if (!id.IsValid())
                PRISM_WARN("Nao foi possivel obter o AssetID do prefab - instancia criada SEM vinculo: ", m_PrefabPath.string());
            return id;
        }

        Prism::Ref<Prism::Scene> m_Scene;
        std::filesystem::path m_PrefabPath;
        Prism::Entity m_Instantiated;
    };

    // Sincroniza uma INSTANCIA de prefab com o .prismprefab de origem
    // ATUAL - "Atualizar" no painel Prefab (ver EditorLayer.cpp e
    // Scene/PrefabSyncer.h para a logica de override por component).
    // Aplica Prism::PrefabSyncer::UpdateAll (que preserva Components
    // overridados - ver comentario la) e guarda, ANTES de aplicar, uma
    // copia de cada Component que sera tocado, para poder devolver
    // exatamente esses valores no Undo.
    //
    // A COPIA "antes" e feita entidade-a-entidade/component-a-component
    // usando o proprio ComponentRegistry::Copy contra uma Scene auxiliar
    // (m_Backup) criada so para isto - mesmo espirito de
    // PrefabSyncer::LoadPrefabForComparison, mas guardando o ESTADO DA
    // INSTANCIA antes da sincronizacao, nao o conteudo do arquivo.
    class SyncPrefabInstanceCommand : public Prism::Command {
    public:
        SyncPrefabInstanceCommand(Prism::Entity instanceRoot, std::filesystem::path prefabPath)
            : m_InstanceRoot(instanceRoot), m_PrefabPath(std::move(prefabPath)) {}

        void Execute() override {
            if (!m_InstanceRoot)
                return;

            // Descobre ANTES de aplicar quais (entidade, component) serao
            // tocados - reaproveita Diff() so para a lista de
            // nao-overridados, sem repetir a logica de comparacao aqui.
            Prism::PrefabDiffResult diff = Prism::PrefabSyncer::Diff(m_InstanceRoot, m_PrefabPath);
            if (!diff.Valid)
                return; // erro ja logado dentro de Diff via PRISM_CORE_ERROR (ver EditorLayer, quem chama isto ja avisou o usuario antes de criar o Command)

            m_Backup = Prism::Scene::Create("__sync_prefab_undo_backup__");
            // 'd' NAO e const, mesmo motivo documentado em
            // PrefabSyncer::UpdateAll/EditorLayer::RenderPrefabInstanceSection
            // (ver PrefabSyncer.cpp): Entity::HasComponent/GetComponent
            // nao sao const-qualificados, entao qualquer chamada futura
            // sobre d.InstanceEntity aqui dentro precisaria disto de
            // qualquer forma - mantido consistente mesmo onde hoje so
            // uso Copy()/push_back() (que aceitam Entity por valor e
            // funcionariam com const tambem).
            for (Prism::PrefabComponentDiff& d : diff.Components) {
                if (d.Overridden)
                    continue; // nunca tocado por UpdateAll (ver PrefabSyncer.h) - nada a desfazer

                if (d.ComponentName == "Transform") {
                    // Transform nao passa por ComponentRegistry (ver
                    // PrefabSyncer.h) - guarda o valor antigo direto.
                    m_TransformBackups.push_back({ d.InstanceEntity, d.InstanceEntity.GetComponent<Prism::TransformComponent>() });
                    continue;
                }

                if (d.PresentInInstance) {
                    // Caso comum: a instancia TEM o Component e vai ser
                    // SOBRESCRITO com o valor do prefab - guarda uma copia
                    // para devolver no Undo.
                    for (auto& info : Prism::ComponentRegistry::GetAll()) {
                        if (info.DisplayName != d.ComponentName)
                            continue;
                        Prism::Entity backupEntity = m_Backup->CreateEntity("backup");
                        info.Copy(d.InstanceEntity, backupEntity);
                        m_BackupEntries.push_back({ d.InstanceEntity, backupEntity, d.ComponentName, /*ComponentWillBeAdded*/ false });
                        break;
                    }
                }
                else if (d.PresentInPrefab) {
                    // A instancia NAO tem o Component, mas o PREFAB tem -
                    // UpdateAll/RevertComponent vai ADICIONAR o Component
                    // na instancia (ver PrefabSyncer::RevertComponent).
                    // Sem BackupEntity nenhum aqui: nao ha valor anterior
                    // para copiar de volta, so remover no Undo (ver
                    // ComponentWillBeAdded abaixo).
                    m_BackupEntries.push_back({ d.InstanceEntity, {}, d.ComponentName, /*ComponentWillBeAdded*/ true });
                }
                // (PresentInInstance == PresentInPrefab == false nunca
                // aparece em diff.Components - ver Diff(), que so inclui
                // nomes presentes em pelo menos um dos dois lados.)
            }

            m_UpdatedCount = Prism::PrefabSyncer::UpdateAll(m_InstanceRoot, m_PrefabPath);
        }

        void Undo() override {
            for (auto& tb : m_TransformBackups) {
                if (tb.InstanceEntity && tb.InstanceEntity.HasComponent<Prism::TransformComponent>())
                    tb.InstanceEntity.GetComponent<Prism::TransformComponent>() = tb.Before;
            }

            for (auto& entry : m_BackupEntries) {
                if (!entry.InstanceEntity)
                    continue;
                for (auto& info : Prism::ComponentRegistry::GetAll()) {
                    if (info.DisplayName != entry.ComponentName)
                        continue;

                    if (entry.ComponentWillBeAdded) {
                        // A instancia NAO tinha este Component antes de
                        // Execute() (ele foi ADICIONADO pelo sync, vindo
                        // do prefab) - Undo so remove, sem BackupEntity
                        // nenhum para copiar (ver Execute()).
                        if (info.Has(entry.InstanceEntity))
                            info.Remove(entry.InstanceEntity);
                    }
                    else {
                        if (info.Has(entry.InstanceEntity))
                            info.Remove(entry.InstanceEntity); // Copy() exige ausencia previa - ver comentario identico em PrefabSyncer
                        info.Copy(entry.BackupEntity, entry.InstanceEntity);
                    }
                    break;
                }
            }
        }

        std::string GetName() const override { return "Sincronizar Instancia de Prefab"; }

        int GetUpdatedCount() const { return m_UpdatedCount; }

    private:
        struct BackupEntry {
            Prism::Entity InstanceEntity;
            Prism::Entity BackupEntity;      // invalido se ComponentWillBeAdded == true (ver Execute())
            std::string ComponentName;
            bool ComponentWillBeAdded = false; // true = a instancia NAO tinha este Component antes do sync - Undo so remove, nao copia
        };

        struct TransformBackup {
            Prism::Entity InstanceEntity;
            Prism::TransformComponent Before;
        };

        Prism::Entity m_InstanceRoot;
        std::filesystem::path m_PrefabPath;
        Prism::Ref<Prism::Scene> m_Backup; // mantem viva a Scene auxiliar (RAII) enquanto este Command existir no historico de Undo/Redo
        std::vector<BackupEntry> m_BackupEntries;
        std::vector<TransformBackup> m_TransformBackups; // Transform nao passa por ComponentRegistry - ver PrefabSyncer.h
        int m_UpdatedCount = 0;
    };

    // Reverte UM Component especifico de UMA entidade de uma instancia de
    // prefab para o valor atual do arquivo - "Reverter" por-component no
    // painel Prefab (ver EditorLayer.cpp). Mais granular que
    // SyncPrefabInstanceCommand (que sincroniza TODOS os nao-overridados
    // de uma vez) - usado quando o usuario quer descartar o override de
    // UM component so, mantendo outros overrides intactos.
    class RevertPrefabComponentCommand : public Prism::Command {
    public:
        RevertPrefabComponentCommand(Prism::Entity instanceEntity, std::filesystem::path prefabPath, uint32_t indexInPrefab, std::string componentName)
            : m_InstanceEntity(instanceEntity), m_PrefabPath(std::move(prefabPath)), m_IndexInPrefab(indexInPrefab), m_ComponentName(std::move(componentName)) {}

        void Execute() override {
            if (!m_InstanceEntity)
                return;

            if (m_ComponentName == "Transform") {
                // Transform nao passa por ComponentRegistry (ver PrefabSyncer.h)
                m_TransformBefore = m_InstanceEntity.GetComponent<Prism::TransformComponent>();
                m_IsTransform = true;
                m_Reverted = Prism::PrefabSyncer::RevertComponent(m_InstanceEntity, m_PrefabPath, m_IndexInPrefab, m_ComponentName);
                return;
            }

            m_Backup = Prism::Scene::Create("__revert_prefab_component_undo_backup__");
            for (auto& info : Prism::ComponentRegistry::GetAll()) {
                if (info.DisplayName != m_ComponentName)
                    continue;
                m_HadComponent = info.Has(m_InstanceEntity);
                if (m_HadComponent) {
                    m_BackupEntity = m_Backup->CreateEntity("backup");
                    info.Copy(m_InstanceEntity, m_BackupEntity);
                }
                break;
            }

            m_Reverted = Prism::PrefabSyncer::RevertComponent(m_InstanceEntity, m_PrefabPath, m_IndexInPrefab, m_ComponentName);
        }

        void Undo() override {
            if (!m_Reverted || !m_InstanceEntity)
                return;

            if (m_IsTransform) {
                m_InstanceEntity.GetComponent<Prism::TransformComponent>() = m_TransformBefore;
                return;
            }

            for (auto& info : Prism::ComponentRegistry::GetAll()) {
                if (info.DisplayName != m_ComponentName)
                    continue;
                if (info.Has(m_InstanceEntity))
                    info.Remove(m_InstanceEntity);
                if (m_HadComponent)
                    info.Copy(m_BackupEntity, m_InstanceEntity);
                // se m_HadComponent for false, o Component nao existia
                // antes (foi ADICIONADO pelo Revert, ver PrefabSyncer::
                // RevertComponent) - Undo so precisa deixar removido, o
                // que o info.Remove acima ja fez.
                break;
            }
        }

        std::string GetName() const override { return "Reverter Component ao Prefab"; }

    private:
        Prism::Entity m_InstanceEntity;
        std::filesystem::path m_PrefabPath;
        uint32_t m_IndexInPrefab;
        std::string m_ComponentName;
        Prism::Ref<Prism::Scene> m_Backup;
        Prism::Entity m_BackupEntity;
        bool m_HadComponent = false;
        bool m_Reverted = false;
        bool m_IsTransform = false;                 // "Transform" e sintetico (nao esta no ComponentRegistry) - ver PrefabSyncer.h
        Prism::TransformComponent m_TransformBefore;
    };

    // Aplica o valor ATUAL de UM Component de UMA entidade da instancia
    // de VOLTA para o arquivo .prismprefab - "Aplicar ao Prefab" no
    // painel (ver Scene/PrefabSyncer.h, ApplyComponentToPrefab, sobre por
    // que isto NAO propaga para outras instancias automaticamente). Sem
    // Undo de verdade sobre o ARQUIVO (ver comentario em Undo()) - undo
    // de escrita em disco exigiria guardar o .prismprefab inteiro antes,
    // por uma acao que grava um unico Component; o command ainda entra no
    // historico (para GetName() aparecer no menu Editar) mas Undo() so
    // avisa que a alteracao no arquivo nao pode ser desfeita por aqui.
    class ApplyPrefabComponentCommand : public Prism::Command {
    public:
        ApplyPrefabComponentCommand(Prism::Entity instanceEntity, std::filesystem::path prefabPath, std::string componentName)
            : m_InstanceEntity(instanceEntity), m_PrefabPath(std::move(prefabPath)), m_ComponentName(std::move(componentName)) {}

        void Execute() override {
            m_Applied = Prism::PrefabSyncer::ApplyComponentToPrefab(m_InstanceEntity, m_PrefabPath, m_ComponentName);
        }

        void Undo() override {
            if (m_Applied)
                PRISM_ERROR("\"Aplicar ao Prefab\" grava direto no arquivo .prismprefab e nao pode ser desfeito por Ctrl+Z - restaure o arquivo manualmente (ou pelo controle de versao) se precisar reverter.");
        }

        std::string GetName() const override { return "Aplicar Component ao Prefab"; }

        bool WasApplied() const { return m_Applied; }

    private:
        Prism::Entity m_InstanceEntity;
        std::filesystem::path m_PrefabPath;
        std::string m_ComponentName;
        bool m_Applied = false;
    };

    // "Aplicar Estrutura ao Prefab": regrava o .prismprefab inteiro a partir
    // da instancia (filhos ADICIONADOS/REMOVIDOS na instancia passam a valer
    // no arquivo) - ver PrefabSyncer::ApplyStructureToPrefab. Mesmo padrao
    // de ApplyPrefabComponentCommand: grava direto no arquivo e NAO e
    // desfeito por Ctrl+Z (o Undo so avisa).
    class ApplyPrefabStructureCommand : public Prism::Command {
    public:
        ApplyPrefabStructureCommand(Prism::Entity instanceRoot, std::filesystem::path prefabPath)
            : m_InstanceRoot(instanceRoot), m_PrefabPath(std::move(prefabPath)) {}

        void Execute() override {
            m_Applied = Prism::PrefabSyncer::ApplyStructureToPrefab(m_InstanceRoot, m_PrefabPath);
            if (m_Applied)
                PRISM_INFO("Estrutura da instancia aplicada ao prefab: ", m_PrefabPath.filename().string());
            else
                PRISM_ERROR("Falha ao aplicar a estrutura da instancia ao prefab - ver console para detalhes.");
        }

        void Undo() override {
            if (m_Applied)
                PRISM_ERROR("\"Aplicar Estrutura ao Prefab\" grava direto no arquivo .prismprefab e nao pode ser desfeito por Ctrl+Z - restaure o arquivo manualmente (ou pelo controle de versao) se precisar reverter.");
        }

        std::string GetName() const override { return "Aplicar Estrutura ao Prefab"; }

        bool WasApplied() const { return m_Applied; }

    private:
        Prism::Entity m_InstanceRoot;
        std::filesystem::path m_PrefabPath;
        bool m_Applied = false;
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
