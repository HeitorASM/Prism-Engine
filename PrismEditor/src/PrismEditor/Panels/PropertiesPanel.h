#pragma once

// ============================================================================
// PropertiesPanel.h
// Painel Propriedades: uma secao por component (Transform, MeshRenderer,
// Material, Camera, Light, Collider, RigidBody, Script, Raycast), a secao de
// Prefab, "+ Add Component" e os popups "Salvar Material Como" e "Novo
// Script". Antes: ~1200 linhas e 10 membros de EditorLayer.
//
// A UI de cada component continua escrita a mao (ver docs/arquitetura.md,
// "Registro de components", passo 4): cada um tem logica propria demais para
// generalizar. Este painel so passou de lugar.
// ============================================================================

#include "../Core/EditorContext.h"
#include <imgui.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PrismEditor {

    class PropertiesPanel {
    public:
        explicit PropertiesPanel(EditorContext& context) : m_Ctx(context) {}

        void OnImGuiRender();

        // Desenha o popup modal "Salvar Material como Asset" (mesmo
        // padrao de RenderCreatePrefabPopup) - pede o nome do arquivo,
        // chama Prism::MaterialSerializer::Serialize com o
        // MaterialComponent ATUAL de EditorContext::SelectedEntity ao confirmar,
        // salvando dentro de Project::GetMaterialDirectory(). Aberto pelo
        // botao "Salvar como Asset..." do painel Material (ver
        // RenderPropertiesPanel).
        void RenderSaveMaterialPopup();

        // Desenha o popup modal "Novo Script" (mesmo padrao de
        // RenderSaveAsPopup) - pede o nome, chama CreateNewScript() ao
        // confirmar. Chamado a cada frame de RenderDockspace(), mesmo
        // fechado (ImGui::OpenPopup exige isso).
        void RenderNewScriptPopup();

        // IDs dos popups modais (o EditorLayer precisa deles para nao disparar
        // atalhos enquanto um popup esta aberto).
        static constexpr const char* kSaveMaterialPopupId = "Salvar Material Como";
        static constexpr const char* kNewScriptPopupId = "Novo Script";

    private:
        // --- UI por component ---------------------------------------------
        // Cada funcao desenha a secao (CollapsingHeader + campos + "X" de
        // remover, exceto Transform) do component correspondente em
        // EditorContext::SelectedEntity e vive em PropertiesPanel_<Nome>.cpp.
        // OnImGuiRender() so chama a funcao quando a entidade tem o
        // component. Um component novo = um .cpp novo + uma linha la.
        void DrawTransformUI();
        void DrawMeshRendererUI();
        void DrawMaterialUI();
        void DrawLightUI();
        void DrawColliderUI();
        void DrawRigidBodyUI();
        void DrawRaycastUI();
        void DrawCameraUI();
        void DrawScriptUI();

        // --- Vinculo vivo de PREFAB (PrefabInstanceRootComponent/
        // PrefabInstanceMemberComponent, ver Components.h e
        // Scene/PrefabSyncer.h) ------------------------------------------
        //
        // Ao contrario do vinculo de Material (que grava sozinho, com
        // debounce, a cada edicao - ver FlushMaterialLinkSave acima),
        // aqui NADA e automatico: sincronizar/aplicar/reverter sao acoes
        // EXPLICITAS do usuario (via os tres Commands em EditorCommands.h
        // - SyncPrefabInstanceCommand, RevertPrefabComponentCommand,
        // ApplyPrefabComponentCommand), porque mexem em varios Components
        // de uma vez e precisam de Undo/Redo de verdade. O que ESTA
        // funcao faz automaticamente e so DETECTAR e AVISAR: compara a
        // instancia contra o arquivo (Prism::PrefabSyncer::Diff, barato
        // o bastante para chamar todo frame - ver comentario no .cpp) e
        // desenha o selo/contagem de divergencias no painel Prefab
        // (RenderPrefabInstanceSection) - a decisao de agir e sempre do
        // usuario, atraves dos botoes daquela secao.
        void RenderPrefabInstanceSection();

        // ^ OnImGuiRender() desenha uma secao por component que a
        //   entidade selecionada ja tem (com um "X" para remover, exceto
        //   Transform); o botao "+ Add Component" no final fica em
        //   RenderAddComponentButton() por ser um bloco grande o bastante
        //   (o popup com a lista de components disponiveis) para nao
        //   poluir o corpo principal da funcao.
        void RenderAddComponentButton();

        // Ver comentario grande na implementacao (EditorLayer.cpp) sobre
        // por que este mapeamento String -> Command<T> vive aqui e nao
        // dentro de Prism::ComponentRegistry.
        void AddComponentByRegistryName(const std::string& displayName);

        // Desenha a cena a partir do ponto de vista da entidade passada
        // (espera-se que tenha CameraComponent) dentro de
        // m_CameraPreviewFramebuffer. O framebuffer e redimensionado para
        // caber exatamente em (width, height) (em pixels) - usado para
        // exibir a pre-visualizacao da camera selecionada dentro do painel
        // de Propriedades. Retorna o ID da textura de cor para ser exibido
        // via ImGui::Image. Se a entidade nao tiver CameraComponent, ou se
        // m_CameraPreviewFramebuffer nao estiver inicializado, retorna 0.
        uint32_t RenderCameraPreview(Prism::Entity cameraEntity, float width, float height);

        // Lista (nomes relativos, ex: "player_controller.lua") todo arquivo
        // .lua diretamente dentro de Project::GetScriptDirectory() - NAO
        // recursivo por simplicidade (mesmo escopo do ContentBrowserPanel
        // hoje: scripts organizados em subpastas e um caso futuro). Chamado
        // toda vez que o combo de selecao do ScriptComponent e desenhado
        // (barato o bastante para nao precisar de cache - ver
        // OnImGuiRender()).
        std::vector<std::string> ListProjectScripts() const;

        // ^ mesmo padrao do Console/Content Browser: delega para
        //   m_ScriptEditor.OnImGuiRender() - ver Panels/ScriptEditorPanel.h.

        // Cria um arquivo .lua novo dentro de Project::GetScriptDirectory(),
        // com um template minimo (OnCreate/OnUpdate/OnDestroy comentados -
        // mesmo formato dos exemplos em PrismEditor/assets/ScriptExamples/).
        // 'name' e o nome SEM extensao (ex: "player_controller") - ".lua" e
        // adicionado aqui. Retorna o caminho RELATIVO (ex:
        // "player_controller.lua", o formato que ScriptComponent::ScriptPath
        // espera - ver Components.h) em caso de sucesso, ou um path vazio se
        // o nome for invalido ou ja existir um arquivo com esse nome. Chamado
        // pelo botao "Novo..." da UI do ScriptComponent (ver
        // OnImGuiRender()).
        std::filesystem::path CreateNewScript(const std::string& name);

        EditorContext& m_Ctx;

        // Estado "antes" capturado no momento em que o usuario COMECA a
        // arrastar um DragFloat3/ColorEdit3 na Properties panel (via
        // ImGui::IsItemActivated()) - usado para montar um unico
        // TransformCommand/MeshColorCommand quando o arraste termina, em
        // vez de um comando por frame de movimento do mouse. Valido apenas
        // enquanto um drag esta em andamento.
        Prism::TransformComponent m_TransformBeforeEdit;

        glm::vec3 m_ColorBeforeEdit{ 0.0f };

        // Estado do popup modal "Novo Script" (mesmo padrao de
        // m_ShowSaveAsPopup/m_SaveAsNameBuffer acima) - aberto pelo botao
        // "Novo..." da UI do ScriptComponent, pede so o nome (sem extensao)
        // do arquivo a criar em Project::GetScriptDirectory() via
        // CreateNewScript().
        bool m_ShowNewScriptPopup = false;

        char m_NewScriptNameBuffer[128] = "";

        // Estado do popup modal "Salvar Material como Asset" (mesmo
        // padrao de m_ShowCreatePrefabPopup acima) - aberto pelo botao
        // "Salvar como Asset..." do painel Material (ver
        // RenderPropertiesPanel). Ao contrario do prefab, nao precisa de
        // um "m_MaterialToSaveFrom" separado - sempre opera sobre
        // EditorContext::SelectedEntity no momento da confirmacao (o material fica
        // visivel/editavel so quando ha selecao, entao nao ha o mesmo
        // risco de "selecao mudou enquanto o popup estava aberto" que
        // justificou capturar a entidade separadamente para prefabs).
        bool m_ShowSaveMaterialPopup = false;

        char m_SaveMaterialNameBuffer[128] = "";

        // Framebuffer offscreen SEPARADO, usado exclusivamente para a
        // pre-visualizacao da camera dentro do painel de Propriedades.
        // Criado sob demanda (fica nulo ate a primeira vez que for
        // necessario exibir o preview) para nao gastar uma textura de GPU
        // extra em sessoes que nunca abrem o painel Camera.
        Prism::Scope<Prism::Framebuffer> m_CameraPreviewFramebuffer;

        // Cache do ultimo PrefabSyncer::Diff calculado pelo painel Prefab
        // (ver RenderPrefabInstanceSection) - evita reler o .prismprefab
        // inteiro numa Scene temporaria TODO FRAME enquanto o painel fica
        // aberto (Diff() e mais caro que o simples stat que o cache de
        // Material usa, ja que compara byte a byte cada Component - ver
        // PrefabSyncer.h). Valido enquanto (a) a raiz selecionada for a
        // MESMA de quando calculamos e (b) o mtime do arquivo nao mudou
        // desde entao - qualquer uma das duas invalida o cache e forca
        // um novo Diff (ver RenderPrefabInstanceSection).
        Prism::Entity m_PrefabDiffCachedRoot;

        std::filesystem::file_time_type m_PrefabDiffCachedFileTime{};

        Prism::PrefabDiffResult m_PrefabDiffCache;
    };

}
