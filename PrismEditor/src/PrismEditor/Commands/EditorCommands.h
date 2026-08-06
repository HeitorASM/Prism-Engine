#pragma once

// ============================================================================
// EditorCommands.h
// Commands concretos (ver Prism::Command / Prism::CommandHistory) usados
// pelo EditorLayer. Ficam do lado do Editor (nao da engine) porque
// conhecem o fluxo de edicao especifico: qual Entity, qual Scene, etc.
//
// Quatro comandos por enquanto - o minimo para o editor ja ser util com
// undo/redo:
//   - TransformCommand: mudanca de posicao/rotacao/escala de UMA entidade
//     (usado pelos DragFloat3 da Properties panel - ver EditorLayer.cpp)
//   - MeshColorCommand: mudanca de cor do MeshRendererComponent
//   - CreateEntityCommand: criar uma entidade nova
//   - DeleteEntityCommand: excluir uma entidade existente
//
// Novos tipos de edicao (renomear, criar/excluir components) seguem o
// mesmo padrao: guardar o estado "antes" no construtor, aplicar em
// Execute(), reverter em Undo().
// ============================================================================

#include <Prism.h>

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
    };

}
