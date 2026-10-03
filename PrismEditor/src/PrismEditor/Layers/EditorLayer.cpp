#include "EditorLayer.h"
#include "../Core/EntityOps.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <cstdio>
#include <string>

namespace PrismEditor {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_Document(m_Ctx), m_Viewport(m_Ctx), m_Hierarchy(m_Ctx), m_Properties(m_Ctx) {}

    void EditorLayer::OnAttach() {
        PRISM_INFO("EditorLayer anexada. Projeto ativo: ",
            Prism::Project::GetActive()->GetConfig().Name);

        // Visual do projeto (exposicao, ambiente): o Renderer e estatico e
        // guarda o ultimo valor aplicado, entao SEMPRE reaplica ao abrir um
        // projeto - senao ele herdaria os ajustes do projeto anterior.
        Prism::Renderer::ApplyRenderSettings(Prism::Project::GetActive()->GetRenderSettings());

        // Liga os ponteiros do contexto compartilhado aos paineis que outros
        // paineis precisam chamar (ver EditorContext.h).
        m_Ctx.Play = &m_PlayWindow;
        m_Ctx.ContentBrowser = &m_ContentBrowser;
        m_Ctx.ScriptEditor = &m_ScriptEditor;

        m_Viewport.Init();

        m_ContentBrowser.ResetToProjectRoot();
        m_ContentBrowser.SetOnMapDoubleClicked([this](const std::filesystem::path& mapPath) {
            // Copia do path: o Content Browser pode reaproveitar o buffer
            // dele antes do popup "Salvar alteracoes?" ser respondido (a
            // acao roda depois, em outro frame).
            m_Document.RunAfterUnsavedCheck([this, mapPath]() { m_Document.LoadScene(mapPath); });
            });

        // Botao X da janela (ou Alt+F4): o gancho decide "deixa fechar?" e,
        // havendo alteracoes, pede o aviso - ver SceneDocument::OnCloseRequested.
        Prism::Application::Get().SetCloseRequestHandler([this]() -> bool {
            return m_Document.OnCloseRequested();
            });

        // O menu de contexto de entidade e ligado no construtor do HierarchyPanel
        // (ver HierarchyPanel::HierarchyPanel).

        m_Document.LoadOrCreateScene();
    }

    void EditorLayer::OnDetach() {
        // NAO chamar Prism::Application::Get() aqui para "desregistrar" o
        // gancho de fechar janela (SetCloseRequestHandler, ver OnAttach).
        // Este OnDetach roda DENTRO da destruicao do Application (o
        // LayerStack e um membro dele), DEPOIS de ~Application() ja ter
        // zerado s_Instance - Get() desreferenciaria nullptr e o editor
        // crasharia ao fechar. O gancho e um membro do proprio Application,
        // entao ele ja e limpo por ~Application() (ver Application.cpp)
        // antes de qualquer Layer ser destruida; nao ha como um clique no X
        // chamar o gancho de um EditorLayer que ja nao existe.
        //
        // (Excecao nao coberta: EditorLayer sendo removido com o Application
        // VIVO, por PopLayer. Hoje nada faz isso - o EditorLayer so sai no
        // shutdown. Se um dia o "Fechar Projeto" voltar ao
        // ProjectManagerLayer, o PopLayer(EditorLayer) precisa limpar o
        // gancho ANTES de enfileirar a remocao, a partir de um ponto onde o
        // Application e sabidamente valido - por exemplo o proprio
        // RenderMenuBar.)

        // Cursor: se o editor esta sendo destruido com o modo voar ativo
        // (RMB segurado), o cursor estaria escondido/lockado
        // (GLFW_CURSOR_DISABLED). Antes este metodo tentava restaura-lo via
        // Application::Get().GetWindow() - mas OnDetach roda DENTRO da
        // destruicao do Application (o LayerStack e membro dele), depois de
        // ~Application() ter zerado s_Instance E possivelmente depois da
        // janela GLFW ja ter sido destruida: Get() desreferenciaria nullptr
        // (crash ao fechar o editor) e mesmo com a instancia valida o
        // GLFWwindow* podia ja estar liberado (use-after-free).
        //
        // Por isso aqui so zeramos o estado. A janela do SO ao ser
        // destruida devolve o cursor ao normal sozinha - o unico custo e o
        // cursor continuar escondido por um instante ate a janela sumir, que
        // e exatamente o que o comentario original desta funcao ja aceitava.
        m_Ctx.Camera.LookActive = false;

        // Ajuste de renderizacao e edicao de material vinculado ainda pendentes
        // (a espera do debounce nao acabou): gravados agora, porque nao vem mais
        // nenhum frame. Nenhum dos dois usa Application::Get() - ver o comentario
        // grande no topo desta funcao e RenderSettingsPanel::FlushOnShutdown /
        // MaterialLinkSync::WritePendingOnShutdown.
        m_RenderSettings.FlushOnShutdown();
        m_Ctx.MaterialLinks.WritePendingOnShutdown();
    }

    void EditorLayer::OnUpdate(float deltaTime) {
        m_Viewport.ResizeIfNeeded();

        // EditorContext::ActiveScene->OnUpdate() aqui NAO roda scripts/fisica na pratica
        // (Scene::OnUpdate so simula quando IsRunning() e true - ver
        // Scene.cpp) - EditorContext::ActiveScene, a Scene de EDICAO, nunca chama
        // OnScriptsStart() mais (isso agora acontece so na copia clonada
        // dentro de m_PlayWindow, ver PlayWindow::Open). Mantido mesmo
        // assim por seguranca/futuro (caso algo alem de scripts/fisica
        // precise rodar por frame mesmo fora do Play).
        m_Ctx.ActiveScene->OnUpdate(deltaTime);

        m_Viewport.RenderScene(deltaTime);

        // Nao chamamos mais PropertiesPanel::RenderCameraPreview() aqui - agora ela e
        // chamada sob demanda dentro de PropertiesPanel::OnImGuiRender().

        // PlayWindow::OnUpdate cuida do proprio ciclo (simulacao + desenho
        // + eventos) da janela separada de Play, se estiver aberta -
        // idempotente/no-op quando fechada (ver PlayWindow::OnUpdate).
        // Precisa vir DEPOIS de ViewportPanel::RenderScene()/PropertiesPanel::RenderCameraPreview() acima:
        // aquelas duas funcoes assumem que o contexto OpenGL ativo e o da
        // janela do editor (nunca trocam de contexto) - chamando
        // PlayWindow::OnUpdate por ultimo, qualquer troca de contexto que
        // ela fizer internamente (ver PlayWindow.cpp) so acontece depois
        // que o editor ja terminou de desenhar tudo que precisava neste
        // frame.
        GLFWwindow* editorWindow = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();
        m_PlayWindow.OnUpdate(deltaTime, editorWindow);
    }

    void EditorLayer::OnEvent(Prism::Event& event) {
        // O controle de camera propriamente dito (arrastar com botao
        // direito) e tratado em ViewportPanel::OnImGuiRender() usando o estado de
        // mouse do proprio ImGui, porque so faz sentido reagir quando o
        // mouse esta sobre o painel Viewport - o que o ImGui ja sabe dizer
        // (IsWindowHovered) sem precisarmos de um sistema de Input por
        // polling na engine ainda (isso fica para quando o modo "jogar
        // dentro do editor" precisar de WASD de verdade).
    }

    void EditorLayer::OnImGuiRender() {
        // ImGuizmo::BeginFrame() PRECISA ser chamado uma vez por frame,
        // logo apos o ImGui::NewFrame() da engine (ver
        // Prism::ImGuiLayer::Begin) e ANTES de qualquer ImGuizmo::Manipulate
        // (chamado dentro de RenderTransformGizmo, la dentro de
        // RenderDockspace -> RenderViewportPanel). Sem isso, o proprio
        // header do ImGuizmo alerta que e obrigatorio: internamente ele
        // reseta o estado de hover/hotspot do frame anterior
        // (mbOverGizmoHotspot, mbUsingViewManipulate) - SEM chamar,
        // esse estado fica "preso" no valor de frames passados (ou
        // zerado/invalido logo na inicializacao), o que se manifesta
        // exatamente como "o gizmo aparece desenhado mas passar o mouse ou
        // clicar nas setas/aneis nao registra nada". Cria e
        // destroi uma janela ImGui interna própria e invisível
        // ("gizmo", full-screen, ver ImGuizmo::BeginFrame) - nao interfere
        // com o resto do editor, so precisa rodar antes de qualquer outro
        // ImGui::Begin do frame.
        ImGuizmo::BeginFrame();

        RenderDockspace();
    }

    void EditorLayer::RenderDockspace() {
        static bool dockspaceOpen = true;
        static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("PrismEditorDockspace", &dockspaceOpen, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspaceId = ImGui::GetID("PrismDockspace");
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
        }

        // Atalhos globais de Undo/Redo/Salvar. Ignorados enquanto o ImGui
        // esta capturando texto (ex: editando o campo "Nome" na Properties
        // panel, ou o proprio campo de nome do popup Salvar Como) para nao
        // brigar com o undo nativo de InputText - Ctrl+Z ali deve desfazer
        // a digitacao, nao uma acao do CommandHistory.
        if (!io.WantTextInput && !ImGui::IsPopupOpen(SceneDocument::kSaveAsPopupId) && !ImGui::IsPopupOpen(HierarchyPanel::kCreatePrefabPopupId) && !ImGui::IsPopupOpen(PropertiesPanel::kSaveMaterialPopupId) && !ImGui::IsPopupOpen(SceneDocument::kUnsavedChangesPopupId)) {
            bool ctrl = io.KeyCtrl;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_Ctx.History.Undo();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                m_Ctx.History.Redo();
            else if (ctrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
                m_Document.SaveActiveSceneAs();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                m_Document.SaveActiveScene();
            // Ctrl+D duplica a entidade selecionada - mesma tecla que
            // Unity/Blender usam para "Duplicate". Delete/Backspace exclui -
            // convencao de Unity (Delete) e Blender (X/Delete); cobrimos os
            // dois para nao depender de um teclado especifico (alguns
            // notebooks nao tem uma tecla Delete dedicada facil de achar).
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && m_Ctx.SelectedEntity)
                EntityOps::DuplicateEntity(m_Ctx, m_Ctx.SelectedEntity);
            else if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && m_Ctx.SelectedEntity)
                EntityOps::DeleteEntity(m_Ctx, m_Ctx.SelectedEntity);
        }

        RenderMenuBar();
        m_Document.RenderSaveAsPopup(); // popup modal - precisa ser chamado todo frame, mesmo fechado (ver comentario no metodo)
        m_Document.RenderUnsavedChangesPopup(); // idem - "Salvar alteracoes?" ao fechar/trocar de mapa com alteracoes nao salvas
        m_Properties.RenderNewScriptPopup(); // idem - popup modal do botao "Novo..." do ScriptComponent
        m_Hierarchy.RenderCreatePrefabPopup(); // idem - popup modal do item "Criar Prefab..." do menu de contexto
        m_Properties.RenderSaveMaterialPopup(); // idem - popup modal do botao "Salvar como Asset..." do painel Material

        ImGui::End();

        // Paineis - cada um e sua propria janela ImGui, e o dockspace acima
        // permite que o usuario os arraste/organize livremente.
        m_Viewport.OnImGuiRender();
        m_Hierarchy.OnImGuiRender();
        m_Properties.OnImGuiRender();
        RenderConsolePanel();
        RenderContentBrowserPanel();
        RenderScriptEditorPanel();
        // REMOVIDO: RenderCameraPreviewPanel() - agora integrado ao painel de Propriedades
    }

    void EditorLayer::RenderMenuBar() {
        m_RenderSettings.FlushSave();

        // Vinculo vivo de Material (ver comentario grande em
        // MaterialSerializer.h e EditorLayer.h): grava a edicao pendente
        // desta entidade (se o debounce ja passou) e depois releva
        // qualquer .prismmat vinculado que tenha mudado no disco desde a
        // ultima vez - NESTA ORDEM, para que uma gravacao feita agora
        // mesmo ja conte como "processada" (MaterialLinkSync::m_FileTimes
        // atualizado) antes da releitura rodar no mesmo frame, evitando
        // reler desnecessariamente o arquivo que acabamos de escrever.
        m_Ctx.MaterialLinks.FlushSave();
        m_Ctx.MaterialLinks.Reconcile(m_Ctx.ActiveScene);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Arquivo")) {
                if (ImGui::MenuItem("Novo Mapa")) {
                    m_Document.RunAfterUnsavedCheck([this]() { m_Document.NewMap(); });
                }
                if (ImGui::MenuItem("Salvar Mapa", "Ctrl+S")) {
                    m_Document.SaveActiveScene();
                }
                if (ImGui::MenuItem("Salvar Como...", "Ctrl+Shift+S")) {
                    m_Document.SaveActiveSceneAs();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Fechar Projeto")) {
                    // TODO: voltar ao ProjectManagerLayer em vez de fechar
                    m_Document.RunAfterUnsavedCheck([this]() {
                        m_Document.SetAllowWindowClose(true);
                        Prism::Application::Get().Close();
                        });
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Editar")) {
                std::string undoLabel = m_Ctx.History.CanUndo() ? ("Desfazer '" + m_Ctx.History.PeekUndoName() + "'") : "Desfazer";
                std::string redoLabel = m_Ctx.History.CanRedo() ? ("Refazer '" + m_Ctx.History.PeekRedoName() + "'") : "Refazer";

                if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, m_Ctx.History.CanUndo()))
                    m_Ctx.History.Undo();
                if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, m_Ctx.History.CanRedo()))
                    m_Ctx.History.Redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Entidade")) {
                if (ImGui::MenuItem("Criar Cubo")) {
                    auto command = Prism::CreateScope<CreateEntityCommand>(m_Ctx.ActiveScene, "Cubo", glm::vec3(0.85f, 0.55f, 0.2f));
                    CreateEntityCommand* raw = command.get();
                    m_Ctx.History.Execute(std::move(command));
                    m_Ctx.SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Luz")) {
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_Ctx.ActiveScene, "Luz", "Luz",
                        [](Prism::Entity entity) { entity.AddComponent<Prism::LightComponent>(); });
                    CreatePresetEntityCommand* raw = command.get();
                    m_Ctx.History.Execute(std::move(command));
                    m_Ctx.SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Character")) {
                    // Preset de conveniencia: combo comum para um NPC/player
                    // controlavel por script - mesh visivel + collider +
                    // rigidbody cinematico (nao cai por gravidade sozinho,
                    // controle fica com o script/codigo - ver
                    // BodyType::Kinematic em Components.h) + slot de script
                    // vazio pronto para preencher.
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_Ctx.ActiveScene, "Character", "Character",
                        [](Prism::Entity entity) {
                            auto& mesh = entity.AddComponent<Prism::MeshRendererComponent>();
                            mesh.Color = glm::vec3(0.3f, 0.8f, 0.4f);

                            auto& collider = entity.AddComponent<Prism::ColliderComponent>();
                            collider.Shape = Prism::ColliderShape::Capsule;
                            collider.Size = { 0.4f, 1.8f, 0.0f };

                            auto& rigidBody = entity.AddComponent<Prism::RigidBodyComponent>();
                            rigidBody.Type = Prism::BodyType::Kinematic;

                            entity.AddComponent<Prism::ScriptComponent>();
                        });
                    CreatePresetEntityCommand* raw = command.get();
                    m_Ctx.History.Execute(std::move(command));
                    m_Ctx.SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Camera")) {
                    // Preset de camera de jogo: so Transform + CameraComponent
                    // (nasce Primary=true - ver Components.h). Se ja existir
                    // outra Primary na cena, desmarcamos ela para manter a
                    // regra de "no maximo uma Primary" (mesmo ajuste feito
                    // em PropertiesPanel::RenderAddComponentButton() para o botao Add Component).
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_Ctx.ActiveScene, "Camera", "Camera",
                        [](Prism::Entity entity) { entity.AddComponent<Prism::CameraComponent>(); });
                    CreatePresetEntityCommand* raw = command.get();
                    m_Ctx.History.Execute(std::move(command));
                    m_Ctx.SelectedEntity = raw->GetCreatedEntity();
                    if (m_Ctx.SelectedEntity)
                        EntityOps::SetPrimaryCamera(m_Ctx, m_Ctx.SelectedEntity); // garante unicidade - desmarca qualquer Primary anterior
                }
                if (ImGui::MenuItem("Criar Entidade Vazia")) {
                    // So Transform (todo Scene::CreateEntity ja da isso) -
                    // ponto de partida para montar qualquer combinacao de
                    // components manualmente via "+ Add Component".
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_Ctx.ActiveScene, "Entidade Vazia", "Entidade Vazia",
                        CreatePresetEntityCommand::SetupFn{});
                    CreatePresetEntityCommand* raw = command.get();
                    m_Ctx.History.Execute(std::move(command));
                    m_Ctx.SelectedEntity = raw->GetCreatedEntity();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicar selecionada", "Ctrl+D", false, (bool)m_Ctx.SelectedEntity))
                    EntityOps::DuplicateEntity(m_Ctx, m_Ctx.SelectedEntity);
                if (ImGui::MenuItem("Excluir selecionada", "Delete", false, (bool)m_Ctx.SelectedEntity))
                    EntityOps::DeleteEntity(m_Ctx, m_Ctx.SelectedEntity);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Janela")) {
                ImGui::MenuItem("Viewport", nullptr, true, false);
                ImGui::MenuItem("Hierarquia", nullptr, true, false);
                ImGui::MenuItem("Propriedades", nullptr, true, false);
                ImGui::MenuItem("Console", nullptr, true, false);
                ImGui::MenuItem("Conteudo do Projeto", nullptr, true, false);
                ImGui::MenuItem("Editor de Script", nullptr, true, false);
                // REMOVIDO: entrada para o painel "Camera" que agora está integrado
                ImGui::EndMenu();
            }

            m_RenderSettings.OnImGuiRenderMenu();

            // Botao Play/Parar: abre/fecha a PlayWindow (janela SEPARADA
            // do SO, ver Play/PlayWindow.h) rodando uma copia clonada da
            // Scene ativa - scripts/fisica nunca tocam a Scene de edicao
            // (EditorContext::ActiveScene) diretamente. Alinhado a direita da menu bar
            // via um espacador.
            float playButtonWidth = 90.0f;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - playButtonWidth - 16.0f);
            bool isRunning = m_PlayWindow.IsOpen();
            if (isRunning) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.2f, 1.0f));
                if (ImGui::Button("Parar", ImVec2(playButtonWidth, 0)))
                    OnStopButtonClicked();
                ImGui::PopStyleColor();
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
                if (ImGui::Button("Play", ImVec2(playButtonWidth, 0)))
                    OnPlayButtonClicked();
                ImGui::PopStyleColor();
            }

            ImGui::EndMenuBar();
        }
    }

    // Abre a PlayWindow (janela separada do SO, ver Play/PlayWindow.h) com
    // uma copia clonada de EditorContext::ActiveScene. GetNativeWindow() da janela do
    // editor (Application::GetWindow()) e passado como contexto a
    // compartilhar - ver comentario extenso sobre isso no topo de
    // PlayWindow.h.
    void EditorLayer::OnPlayButtonClicked() {
        if (m_PlayWindow.IsOpen())
            return; // ja aberta - o botao vira "Parar" nesse caso, ver RenderDockspace, entao isto nao deveria ser alcancavel na pratica

        GLFWwindow* editorWindow = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();
        m_PlayWindow.Open(m_Ctx.ActiveScene, editorWindow);
    }

    void EditorLayer::OnStopButtonClicked() {
        m_PlayWindow.Close();
    }

    void EditorLayer::RenderConsolePanel() {
        m_ConsolePanel.OnImGuiRender();
    }

    void EditorLayer::RenderContentBrowserPanel() {
        m_ContentBrowser.OnImGuiRender();
    }

    void EditorLayer::RenderScriptEditorPanel() {
        m_ScriptEditor.OnImGuiRender();
    }

}
