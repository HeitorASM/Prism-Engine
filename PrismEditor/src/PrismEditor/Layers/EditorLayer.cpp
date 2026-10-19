#include "EditorLayer.h"
#include "ProjectManagerLayer.h"
#include <PrismVersion.h>
#include "../Core/EntityOps.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>
#include <imgui_internal.h> // DockBuilder (layout padrao dos paineis)
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <cstdio>
#include <string>

namespace PrismEditor {

    EditorLayer::EditorLayer(bool buildNativeOnOpen)
        : Layer("EditorLayer"), m_Document(m_Ctx), m_Viewport(m_Ctx), m_Hierarchy(m_Ctx), m_Properties(m_Ctx),
          m_BuildNativeOnOpen(buildNativeOnOpen) {}

    void EditorLayer::OnAttach() {
        PRISM_INFO("EditorLayer anexada. Projeto ativo: ",
            Prism::Project::GetActive()->GetConfig().Name);

        // Liga os ponteiros do contexto compartilhado aos paineis que outros
        // paineis precisam chamar (ver EditorContext.h).
        m_Ctx.Play = &m_PlayWindow;
        m_Ctx.ContentBrowser = &m_ContentBrowser;
        m_Ctx.ScriptEditor = &m_ScriptEditor;

        m_Viewport.Init();

        m_ContentBrowser.ResetToProjectRoot();
        m_ContentBrowser.SetOnTextFileDoubleClicked([this](const std::filesystem::path& filePath) {
            if (m_ScriptEditor.Open(filePath))
                ImGui::SetWindowFocus("###ScriptEditorPanel"); // traz a aba do Editor de Codigo para frente
            });
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

        // Scripts C++ ja compilados em uma sessao anterior (Binaries/NativeScripts.*).
        Prism::NativeScripting::LoadProjectModule(*Prism::Project::GetActive());

        // Clicar num erro/aviso da Saida de Build abre o arquivo no Editor de Codigo.
        m_BuildOutput.SetBaseDirectory(Prism::NativeScripting::GetSourceDirectory(*Prism::Project::GetActive()));
        m_BuildOutput.SetOnOpenFile([this](const std::filesystem::path& filePath) {
            if (m_ScriptEditor.Open(filePath))
                ImGui::SetWindowFocus("###ScriptEditorPanel");
            });

        // Projeto criado por outra versao da engine: so avisa (nao ha migracao automatica).
        const std::string& projectVersion = Prism::Project::GetActive()->GetConfig().EngineVersion;
        if (!projectVersion.empty() && projectVersion != Prism::Version::Number)
            PRISM_WARN("Este projeto foi criado com a Prism ", projectVersion, " e a engine atual e a ", Prism::Version::Number,
                ". Mapas e prefabs de outra versao podem nao abrir (ver docs/formatos-de-arquivo.md).");

        m_Document.LoadOrCreateScene();

        if (m_BuildNativeOnOpen)
            StartNativeBuild();
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

        // Edicao de material vinculado ainda pendente (a espera do debounce
        // nao acabou): gravada agora, porque nao vem mais nenhum frame. Nao
        // usa Application::Get() - ver o comentario grande no topo desta
        // funcao e MaterialLinkSync::WritePendingOnShutdown.
        m_Ctx.MaterialLinks.WritePendingOnShutdown();
    }

    void EditorLayer::OnUpdate(float deltaTime) {
        m_Viewport.ResizeIfNeeded();
        PollNativeBuild();

        // EditorContext::ActiveScene->OnUpdate() aqui NAO roda scripts/fisica na pratica
        // (Scene::OnUpdate so simula quando IsRunning() e true - ver
        // Scene.cpp) - EditorContext::ActiveScene, a Scene de EDICAO, nunca chama
        // OnScriptsStart() mais (isso agora acontece so na copia clonada
        // dentro de m_PlayWindow, ver PlayWindow::Open). Mantido mesmo
        // assim por seguranca/futuro (caso algo alem de scripts/fisica
        // precise rodar por frame mesmo fora do Play).
        m_Ctx.ActiveScene->OnUpdate(deltaTime);

        // Preview do emissor de particulas selecionado (a cena de edicao nao roda o Play).
        Prism::ParticleSystem::UpdatePreview(*m_Ctx.ActiveScene, deltaTime, m_Ctx.SelectedEntity);

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
            RenderToolbar();

            ImGuiID dockspaceId = ImGui::GetID("PrismDockspace");
            // Layout padrao: na primeira abertura (sem imgui.ini com este dockspace,
            // ou com um dockspace vazio de uma sessao antiga quebrada) e ao pedir reset.
            ImGuiDockNode* existingNode = ImGui::DockBuilderGetNode(dockspaceId);
            bool emptyDockspace = existingNode == nullptr || (existingNode->IsLeafNode() && existingNode->Windows.Size == 0);
            if (m_ResetLayoutRequested || emptyDockspace) {
                m_ResetLayoutRequested = false;
                BuildDefaultLayout(dockspaceId, viewport->WorkSize.x, viewport->WorkSize.y);
            }
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
        }

        // Atalhos globais de Undo/Redo/Salvar. Ignorados enquanto o ImGui
        // esta capturando texto (ex: editando o campo "Nome" na Properties
        // panel, ou o proprio campo de nome do popup Salvar Como) para nao
        // brigar com o undo nativo de InputText - Ctrl+Z ali deve desfazer
        // a digitacao, nao uma acao do CommandHistory.
        if (!io.WantTextInput && !m_ScriptEditor.IsFocused() && !ImGui::IsPopupOpen(SceneDocument::kSaveAsPopupId) && !ImGui::IsPopupOpen(HierarchyPanel::kCreatePrefabPopupId) && !ImGui::IsPopupOpen(PropertiesPanel::kSaveMaterialPopupId) && !ImGui::IsPopupOpen(SceneDocument::kUnsavedChangesPopupId)) {
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
            else if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
                m_Docs.Open();
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
        m_BuildOutput.OnImGuiRender();
        RenderContentBrowserPanel();
        RenderScriptEditorPanel();
        m_Docs.OnImGuiRender();
        // REMOVIDO: RenderCameraPreviewPanel() - agora integrado ao painel de Propriedades
    }

    void EditorLayer::BuildDefaultLayout(unsigned int dockspaceId, float width, float height) {
        ImGuiID root = dockspaceId;
        ImGui::DockBuilderRemoveNode(root);
        ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(root, ImVec2(width, height));

        ImGuiID center = root;
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);

        ImGui::DockBuilderDockWindow("Hierarquia", left);
        ImGui::DockBuilderDockWindow("Propriedades", right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("###ScriptEditorPanel", center); // aba ao lado do Viewport (ID fixo, ver ScriptEditorPanel)
        ImGui::DockBuilderDockWindow("###DocsPanel", center);         // painel de documentacao (aparece como aba quando aberto)
        ImGui::DockBuilderDockWindow("Conteudo do Projeto", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("###BuildOutputPanel", bottom); // saida da compilacao dos scripts C++ (aba ao lado do Console)
        ImGui::DockBuilderFinish(root);

        // Viewport em foco (a aba do editor de script fica atras).
        ImGui::SetWindowFocus("Viewport");
    }

    void EditorLayer::RenderToolbar() {
        const float height = 38.0f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 0.0f);
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
            ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x, origin.y + height - 1.0f), ImVec2(origin.x + width, origin.y + height - 1.0f),
            ImGui::GetColorU32(ImGuiCol_Border));

        float startY = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(10.0f, startY + 6.0f));

        if (ImGui::Button("Salvar"))
            m_Document.SaveActiveScene();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Ctx.History.CanUndo());
        if (ImGui::Button("Desfazer"))
            m_Ctx.History.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Ctx.History.CanRedo());
        if (ImGui::Button("Refazer"))
            m_Ctx.History.Redo();
        ImGui::EndDisabled();

        ImGui::SameLine();
        RenderBuildToolbarGroup();

        // Play/Parar centralizado.
        const float playButtonWidth = 90.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX((width - playButtonWidth) * 0.5f);
        ImGui::SetCursorPosY(startY + 6.0f);
        if (m_PlayWindow.IsOpen()) {
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

        // Direita: Documentacao (F1) e Restaurar layout.
        const char* docsLabel = "Documentacao (F1)";
        const char* resetLabel = "Restaurar layout";
        const float framePadding = ImGui::GetStyle().FramePadding.x * 2.0f;
        float resetWidth = ImGui::CalcTextSize(resetLabel).x + framePadding;
        float docsWidth = ImGui::CalcTextSize(docsLabel).x + framePadding;
        ImGui::SameLine();
        ImGui::SetCursorPosX(width - resetWidth - docsWidth - ImGui::GetStyle().ItemSpacing.x - 10.0f);
        ImGui::SetCursorPosY(startY + 6.0f);
        if (ImGui::Button(docsLabel))
            m_Docs.Open();
        ImGui::SameLine();
        if (ImGui::Button(resetLabel))
            m_ResetLayoutRequested = true;

        // Reserva a altura da barra: o dockspace comeca abaixo dela.
        ImGui::SetCursorPos(ImVec2(0.0f, startY + height));
    }

    void EditorLayer::RenderMenuBar() {
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
                if (ImGui::MenuItem("Fechar Projeto"))
                    m_Document.RunAfterUnsavedCheck([this]() { CloseProject(); });
                if (ImGui::MenuItem("Sair do Editor")) {
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
                {
                    // Ambiente da cena (luz ambiente + skybox): so um por cena.
                    auto environmentView = m_Ctx.ActiveScene->GetRegistry().view<Prism::EnvironmentComponent>();
                    bool hasEnvironment = environmentView.begin() != environmentView.end();
                    if (ImGui::MenuItem("Criar Ambiente", nullptr, false, !hasEnvironment)) {
                        auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_Ctx.ActiveScene, "Ambiente", "Ambiente",
                            [](Prism::Entity entity) { entity.AddComponent<Prism::EnvironmentComponent>(); });
                        CreatePresetEntityCommand* raw = command.get();
                        m_Ctx.History.Execute(std::move(command));
                        m_Ctx.SelectedEntity = raw->GetCreatedEntity();
                    }
                    if (hasEnvironment && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("A cena ja tem um Ambiente.");
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
                if (ImGui::BeginMenu("Criar UI")) {
                    // Elementos de UI in-game (ver UI/UISystem.h). Nascem como raizes;
                    // arraste na hierarquia para aninhar dentro de um Panel.
                    auto createUI = [this](const char* name, std::function<void(Prism::UIElementComponent&)> setup) {
                        auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_Ctx.ActiveScene, name, name,
                            [setup](Prism::Entity entity) { setup(entity.AddComponent<Prism::UIElementComponent>()); });
                        CreatePresetEntityCommand* raw = command.get();
                        m_Ctx.History.Execute(std::move(command));
                        m_Ctx.SelectedEntity = raw->GetCreatedEntity();
                    };
                    if (ImGui::MenuItem("Panel"))
                        createUI("UI Panel", [](Prism::UIElementComponent& ui) {
                            ui.Type = Prism::UIElementType::Panel;
                            ui.Size = { 300.0f, 200.0f };
                            ui.Color = { 0.1f, 0.1f, 0.12f, 0.8f };
                        });
                    if (ImGui::MenuItem("Texto"))
                        createUI("UI Texto", [](Prism::UIElementComponent& ui) {
                            ui.Type = Prism::UIElementType::Text;
                            ui.Size = { 300.0f, 40.0f };
                            ui.BlocksPointer = false;
                        });
                    if (ImGui::MenuItem("Botao"))
                        createUI("UI Botao", [](Prism::UIElementComponent& ui) {
                            ui.Type = Prism::UIElementType::Button;
                            ui.Size = { 200.0f, 50.0f };
                            ui.Color = { 0.25f, 0.45f, 0.85f, 1.0f };
                            ui.Text = "Botao";
                        });
                    if (ImGui::MenuItem("Imagem"))
                        createUI("UI Imagem", [](Prism::UIElementComponent& ui) {
                            ui.Type = Prism::UIElementType::Image;
                            ui.Size = { 128.0f, 128.0f };
                        });
                    ImGui::EndMenu();
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
                if (ImGui::MenuItem("Restaurar layout padrao"))
                    m_ResetLayoutRequested = true;
                ImGui::TextDisabled("Arraste as abas para reorganizar os paineis.");
                // REMOVIDO: entrada para o painel "Camera" que agora está integrado
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Ajuda")) {
                if (ImGui::MenuItem("Documentacao da engine", "F1"))
                    m_Docs.Open();
                ImGui::Separator();
                if (ImGui::MenuItem("Visao geral"))
                    m_Docs.Open("00-visao-geral.md");
                if (ImGui::MenuItem("Primeiros passos"))
                    m_Docs.Open("01-primeiros-passos.md");
                if (ImGui::MenuItem("Scripting em Lua"))
                    m_Docs.Open("04-scripting-lua.md");
                if (ImGui::MenuItem("Scripting em C++"))
                    m_Docs.Open("05-scripting-cpp.md");
                if (ImGui::MenuItem("API de scripting (Lua e C++)"))
                    m_Docs.Open("06-api-de-scripting.md");
                if (ImGui::MenuItem("UI in-game"))
                    m_Docs.Open("10-ui-in-game.md");
                if (ImGui::MenuItem("Como a engine funciona"))
                    m_Docs.Open("11-como-a-engine-funciona.md");
                ImGui::EndMenu();
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

        // Modulo compilado por fora do editor (ou antes de abrir): garante carregado.
        if (!Prism::NativeScripting::IsModuleLoaded())
            Prism::NativeScripting::LoadProjectModule(*Prism::Project::GetActive());

        GLFWwindow* editorWindow = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();
        m_PlayWindow.Open(m_Ctx.ActiveScene, editorWindow);
    }

    void EditorLayer::StartNativeBuild() {
        auto project = Prism::Project::GetActive();
        if (!project)
            return;

        // Projeto sem Source/ (ex: aberto de uma versao antiga): gera so se faltar.
        // Nao regrava um CMakeLists.txt existente, para nunca apagar edicoes do usuario.
        std::error_code ec;
        if (!std::filesystem::exists(Prism::NativeScripting::GetSourceDirectory(*project) / "CMakeLists.txt", ec)) {
            std::string message;
            if (Prism::NativeScripting::GenerateProjectFiles(*project, message))
                PRISM_INFO(message);
            else {
                PRISM_ERROR(message);
                return;
            }
        }

        m_BuildOutput.BeginBuild();
        std::string error;
        if (m_NativeBuilder.Start(*project, error)) {
            m_LastNativeBuildFailed = false;
            PRISM_INFO("Compilando scripts C++...");
        }
        else {
            m_BuildOutput.AddLine(error);
            m_BuildOutput.EndBuild(false);
            m_LastNativeBuildFailed = true;
            PRISM_ERROR(error);
        }
    }

    // Botao "Compilar" + estado dos scripts C++, na barra de ferramentas.
    void EditorLayer::RenderBuildToolbarGroup() {
        const bool building = m_NativeBuilder.IsRunning();
        const bool playing = m_PlayWindow.IsOpen();
        const bool hasProject = Prism::Project::GetActive() != nullptr;

        ImGui::BeginDisabled(!hasProject || building || playing);
        if (ImGui::Button(building ? "Compilando..." : "Compilar"))
            StartNativeBuild();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", playing ? "Pare o Play para compilar os scripts."
                : "Compila os scripts C++ (pasta Source/) e recarrega o modulo.");

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (building) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Compilando scripts C++...");
        }
        else if (m_LastNativeBuildFailed) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Erro de build (ver Saida de Build)");
        }
        else if (Prism::NativeScripting::IsModuleLoaded()) {
            ImGui::TextDisabled("Scripts C++: %d classe(s)", (int)Prism::NativeScripting::GetClassNames().size());
        }
        else {
            ImGui::TextDisabled("Scripts C++: nao compilados");
        }
    }

    void EditorLayer::PollNativeBuild() {
        std::vector<NativeBuilder::Line> lines;
        bool justFinished = false;
        bool success = false;
        m_NativeBuilder.Poll(lines, justFinished, success);

        // A saida detalhada vai para a aba "Saida de Build"; o Console recebe so o resumo.
        for (const auto& line : lines)
            m_BuildOutput.AddLine(line.Text);

        if (justFinished) {
            m_BuildOutput.EndBuild(success);
            if (success) {
                m_LastNativeBuildFailed = false;
                PRISM_INFO("Scripts C++ compilados com sucesso.");
                m_NativeReloadPending = true;
            }
            else {
                m_LastNativeBuildFailed = true;
                PRISM_ERROR("Falha ao compilar os scripts C++ (veja a aba Saida de Build).");
            }
        }

        // Recarrega quando nao ha Play (instancias vivas usam o codigo do modulo).
        if (m_NativeReloadPending && !m_PlayWindow.IsOpen()) {
            m_NativeReloadPending = false;
            if (auto project = Prism::Project::GetActive())
                Prism::NativeScripting::LoadProjectModule(*project);
        }
    }

    void EditorLayer::CloseProject() {
        if (m_PlayWindow.IsOpen())
            m_PlayWindow.Close();

        // O modulo C++ e as instancias de script pertencem a este projeto.
        Prism::NativeScripting::UnloadModule();

        // O gancho de fechar janela captura 'this' (ver OnAttach): precisa
        // sumir antes desta Layer ser destruida. NAO fazer isso no OnDetach -
        // la Application::Get() pode ja ser nulo (shutdown do app).
        Prism::Application& app = Prism::Application::Get();
        app.SetCloseRequestHandler(nullptr);

        // Troca de tela: ver o comentario em ProjectManagerLayer::EnterEditor.
        app.PushLayer(new ProjectManagerLayer());
        app.PopLayer(this);
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
