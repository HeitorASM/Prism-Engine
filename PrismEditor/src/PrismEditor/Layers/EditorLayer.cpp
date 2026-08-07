#include "EditorLayer.h"
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace PrismEditor {

    // ID do popup modal de "Salvar Como" - compartilhado entre
    // RenderSaveAsPopup() (que o abre/desenha) e o atalho Ctrl+S/Ctrl+Shift+S
    // em RenderDockspace() (que precisa saber se ja esta aberto, para nao
    // tentar abrir de novo por cima de si mesmo).
    static constexpr const char* kSaveAsPopupId = "Salvar Mapa Como";

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    void EditorLayer::OnAttach() {
        PRISM_INFO("EditorLayer anexada. Projeto ativo: ",
            Prism::Project::GetActive()->GetConfig().Name);

        Prism::FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_ViewportFramebuffer = Prism::Framebuffer::Create(fbSpec);

        m_ContentBrowser.ResetToProjectRoot();
        m_ContentBrowser.SetOnMapDoubleClicked([this](const std::filesystem::path& mapPath) {
            LoadScene(mapPath);
        });

        LoadOrCreateScene();
    }

    bool EditorLayer::LoadScene(const std::filesystem::path& mapPath) {
        Prism::SceneSerializer serializer(Prism::Scene::Create());
        if (!serializer.Deserialize(mapPath)) {
            PRISM_ERROR("Falha ao carregar o mapa: ", mapPath.string());
            return false;
        }

        m_ActiveScene = serializer.GetScene();
        m_CurrentMapPath = mapPath;
        m_SelectedEntity = {};
        m_CommandHistory.Clear();
        return true;
    }

    void EditorLayer::NewMap() {
        // TODO: quando existir rastreamento de "alteracoes nao salvas"
        // (dirty flag), perguntar aqui antes de descartar a cena atual -
        // por ora, Novo Mapa descarta sem aviso, igual acontecia ao
        // carregar outro mapa pelo Content Browser (ver nota no README).
        m_ActiveScene = Prism::Scene::Create("Nova Cena");
        m_CurrentMapPath.clear(); // sem arquivo associado ainda - "Salvar Mapa" vai se comportar como "Salvar Como"
        m_SelectedEntity = {};
        m_CommandHistory.Clear();

        PRISM_INFO("Novo mapa criado (ainda nao salvo).");
    }

    void EditorLayer::LoadOrCreateScene() {
        auto project = Prism::Project::GetActive();
        const auto& startMap = project->GetConfig().StartMap;

        // Historico de undo/redo e por definicao amarrado a UMA Scene em
        // memoria - trocar a Scene (carregando um mapa do disco) sem
        // limpar o historico deixaria Undo() tentando desfazer acoes sobre
        // entidades que nao existem mais na nova Scene. LoadScene() ja faz
        // isso; aqui so garantimos o mesmo comportamento no caminho de
        // "criar cena de exemplo" abaixo.
        m_CommandHistory.Clear();

        if (!startMap.empty()) {
            std::filesystem::path mapPath = project->GetMapDirectory() / startMap;
            if (std::filesystem::exists(mapPath)) {
                if (LoadScene(mapPath))
                    return;
                PRISM_WARN("Falha ao carregar '", mapPath.string(), "' - criando cena de exemplo em memoria.");
            }
        }

        // Projeto novo (ou StartMap ainda nao definido/nao encontrado):
        // cena de exemplo em memoria, igual antes de existir persistencia.
        // "Salvar Mapa"/"Salvar Como" (ver abaixo) e o que grava isso no
        // disco - m_CurrentMapPath fica vazio ate la.
        m_ActiveScene = Prism::Scene::Create("Cena de exemplo");
        m_CurrentMapPath.clear();

        Prism::Entity cube = m_ActiveScene->CreateEntity("Cubo");
        cube.GetComponent<Prism::TransformComponent>().Translation = { -1.2f, 0.0f, 0.0f };
        cube.AddComponent<Prism::MeshRendererComponent>();

        Prism::Entity cube2 = m_ActiveScene->CreateEntity("Cubo (filho conceitual)");
        auto& t2 = cube2.GetComponent<Prism::TransformComponent>();
        t2.Translation = { 1.4f, 0.3f, 0.0f };
        t2.Scale = { 0.6f, 0.6f, 0.6f };
        auto& mesh2 = cube2.AddComponent<Prism::MeshRendererComponent>();
        mesh2.Color = { 0.3f, 0.6f, 0.9f };

        m_SelectedEntity = cube;
    }

    // Helper interno (nao declarado no .h) - escreve m_ActiveScene em
    // 'mapPath' de fato, sem se importar com "e primeiro save?" ou popups.
    // SaveActiveScene()/SaveActiveSceneAs() decidem QUAL caminho usar;
    // este helper so faz a escrita e retorna se deu certo.
    static bool WriteSceneFile(Prism::Ref<Prism::Scene> scene, const std::filesystem::path& mapPath) {
        std::error_code ec;
        std::filesystem::create_directories(mapPath.parent_path(), ec);

        Prism::SceneSerializer serializer(scene);
        if (!serializer.Serialize(mapPath)) {
            PRISM_ERROR("Falha ao salvar o mapa em: ", mapPath.string());
            return false;
        }

        PRISM_INFO("Mapa salvo: ", mapPath.string());
        return true;
    }

    void EditorLayer::SaveActiveScene() {
        if (m_CurrentMapPath.empty()) {
            // Cena sem arquivo associado ainda (nova, ou criada por
            // NewMap()) - nao ha "onde" sobrescrever, entao pedimos um
            // nome, exatamente como Salvar Como faria.
            SaveActiveSceneAs();
            return;
        }

        WriteSceneFile(m_ActiveScene, m_CurrentMapPath);
    }

    void EditorLayer::SaveActiveSceneAs() {
        // So abre o popup - a escrita de fato acontece em
        // RenderSaveAsPopup() quando o usuario confirma o nome, porque
        // ImGui::OpenPopup precisa ser chamado durante o ciclo normal de
        // render (ver RenderDockspace(), que chama RenderSaveAsPopup() a
        // cada frame independente do popup estar aberto ou nao).
        std::string suggested = m_ActiveScene->GetName();
        std::snprintf(m_SaveAsNameBuffer, sizeof(m_SaveAsNameBuffer), "%s", suggested.c_str());
        m_ShowSaveAsPopup = true;
    }

    void EditorLayer::RenderSaveAsPopup() {
        if (m_ShowSaveAsPopup) {
            ImGui::OpenPopup(kSaveAsPopupId);
            m_ShowSaveAsPopup = false; // OpenPopup so precisa ser chamado uma vez, no frame em que o popup deve abrir
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kSaveAsPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Nome do mapa:");
            ImGui::SetNextItemWidth(-1);

            bool confirmedByEnter = ImGui::InputText("##SaveAsName", m_SaveAsNameBuffer, sizeof(m_SaveAsNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            auto project = Prism::Project::GetActive();
            std::string name = m_SaveAsNameBuffer;

            // Sanitizacao minima: caracteres proibidos em nomes de arquivo
            // no Windows (e problematicos em qualquer SO) viram underscore.
            // Sem isso, um nome como "Meu Mapa: Final?" geraria um
            // std::filesystem::path invalido e Serialize() falharia com um
            // erro criptico em vez de simplesmente funcionar com um nome
            // sensato.
            static const std::string kForbiddenChars = "/\\:*?\"<>|";
            for (auto& c : name)
                if (kForbiddenChars.find(c) != std::string::npos)
                    c = '_';

            bool nameEmpty = name.empty();

            // Preview do caminho final - ajuda o usuario a perceber ANTES
            // de confirmar se vai sobrescrever um mapa existente (mesmo
            // nome de um arquivo .prismmap ja presente na pasta Maps/).
            std::filesystem::path previewPath = project->GetMapDirectory() / (name + ".prismmap");
            bool wouldOverwrite = !nameEmpty && std::filesystem::exists(previewPath);

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o mapa.");
            } else if (wouldOverwrite) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Ja existe um mapa com este nome - sera sobrescrito.");
            } else {
                ImGui::TextDisabled("%s", previewPath.filename().string().c_str());
            }

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Salvar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty;

            if (confirmed) {
                if (WriteSceneFile(m_ActiveScene, previewPath)) {
                    m_CurrentMapPath = previewPath;
                    m_ActiveScene->SetName(name);

                    // Novo mapa salvo vira o StartMap do projeto - assim a
                    // proxima vez que o editor abrir, reabre este mapa
                    // (mesmo comportamento que ja existia para o primeiro
                    // save de um projeto, agora tambem valido para
                    // qualquer Salvar Como subsequente).
                    std::filesystem::path relativeToMapDir = std::filesystem::relative(previewPath, project->GetMapDirectory());
                    Prism::Project::SetStartMap(relativeToMapDir);
                }
                ImGui::CloseCurrentPopup();
            } else if (cancelled) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void EditorLayer::OnDetach() {}

    void EditorLayer::OnUpdate(float deltaTime) {
        // Redimensiona o framebuffer se o painel Viewport mudou de tamanho
        // desde o ultimo frame (arrastar a janela, dockar/desdockar, etc).
        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        if (m_ViewportSize[0] > 0.0f && m_ViewportSize[1] > 0.0f &&
            (spec.Width != (uint32_t)m_ViewportSize[0] || spec.Height != (uint32_t)m_ViewportSize[1])) {
            m_ViewportFramebuffer->Resize((uint32_t)m_ViewportSize[0], (uint32_t)m_ViewportSize[1]);
        }

        m_ActiveScene->OnUpdate(deltaTime);

        RenderScene(deltaTime);
    }

    void EditorLayer::RenderScene(float deltaTime) {
        m_ViewportFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        float aspect = spec.Height > 0 ? (float)spec.Width / (float)spec.Height : 1.0f;

        // Camera de orbita: posicao calculada a partir de yaw/pitch/distancia
        // ao redor da origem, olhando sempre para o centro da cena.
        float yawRad = glm::radians(m_CameraYaw);
        float pitchRad = glm::radians(m_CameraPitch);
        glm::vec3 cameraPos;
        cameraPos.x = m_CameraDistance * cosf(pitchRad) * cosf(yawRad);
        cameraPos.y = m_CameraDistance * sinf(pitchRad);
        cameraPos.z = m_CameraDistance * cosf(pitchRad) * sinf(yawRad);

        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        glm::mat4 viewProjection = projection * view;

        // Desenha TODA entidade da cena que tenha Transform + MeshRenderer -
        // isto e o "sistema de renderizacao" no sentido ECS: uma funcao que
        // itera sobre o conjunto de components relevantes, sem saber nada
        // sobre quantas entidades existem ou o que cada uma "e" alem disso.
        auto view_ = m_ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::MeshRendererComponent>();
        for (auto entityHandle : view_) {
            auto [transform, meshRenderer] = view_.get<Prism::TransformComponent, Prism::MeshRendererComponent>(entityHandle);

            glm::mat4 model = transform.GetTransform();
            glm::vec3 color = meshRenderer.Color;

            switch (meshRenderer.Mesh) {
                case Prism::PrimitiveMesh::Cube:
                    Prism::Renderer::DrawTestCube(glm::value_ptr(viewProjection), glm::value_ptr(model), glm::value_ptr(color));
                    break;
            }
        }

        m_ViewportFramebuffer->Unbind();
    }

    void EditorLayer::OnEvent(Prism::Event& event) {
        // O controle de camera propriamente dito (arrastar com botao
        // direito) e tratado em RenderViewportPanel() usando o estado de
        // mouse do proprio ImGui, porque so faz sentido reagir quando o
        // mouse esta sobre o painel Viewport - o que o ImGui ja sabe dizer
        // (IsWindowHovered) sem precisarmos de um sistema de Input por
        // polling na engine ainda (isso fica para quando o modo "jogar
        // dentro do editor" precisar de WASD de verdade).
    }

    void EditorLayer::OnImGuiRender() {
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
        if (!io.WantTextInput && !ImGui::IsPopupOpen(kSaveAsPopupId)) {
            bool ctrl = io.KeyCtrl;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_CommandHistory.Undo();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                m_CommandHistory.Redo();
            else if (ctrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveActiveSceneAs();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveActiveScene();
        }

        RenderMenuBar();
        RenderSaveAsPopup(); // popup modal - precisa ser chamado todo frame, mesmo fechado (ver comentario no metodo)

        ImGui::End();

        // Paineis - cada um e sua propria janela ImGui, e o dockspace acima
        // permite que o usuario os arraste/organize livremente.
        RenderViewportPanel();
        RenderHierarchyPanel();
        RenderPropertiesPanel();
        RenderConsolePanel();
        RenderContentBrowserPanel();
    }

    void EditorLayer::RenderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Arquivo")) {
                if (ImGui::MenuItem("Novo Mapa")) {
                    NewMap();
                }
                if (ImGui::MenuItem("Salvar Mapa", "Ctrl+S")) {
                    SaveActiveScene();
                }
                if (ImGui::MenuItem("Salvar Como...", "Ctrl+Shift+S")) {
                    SaveActiveSceneAs();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Fechar Projeto")) {
                    Prism::Application::Get().Close(); // TODO: voltar ao ProjectManagerLayer em vez de fechar
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Editar")) {
                std::string undoLabel = m_CommandHistory.CanUndo() ? ("Desfazer '" + m_CommandHistory.PeekUndoName() + "'") : "Desfazer";
                std::string redoLabel = m_CommandHistory.CanRedo() ? ("Refazer '" + m_CommandHistory.PeekRedoName() + "'") : "Refazer";

                if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, m_CommandHistory.CanUndo()))
                    m_CommandHistory.Undo();
                if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, m_CommandHistory.CanRedo()))
                    m_CommandHistory.Redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Entidade")) {
                if (ImGui::MenuItem("Criar Cubo")) {
                    auto command = Prism::CreateScope<CreateEntityCommand>(m_ActiveScene, "Cubo", glm::vec3(0.85f, 0.55f, 0.2f));
                    CreateEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Luz")) {
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_ActiveScene, "Luz", "Luz",
                        [](Prism::Entity entity) { entity.AddComponent<Prism::LightComponent>(); });
                    CreatePresetEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Character")) {
                    // Preset de conveniencia: combo comum para um NPC/player
                    // controlavel por script - mesh visivel + collider +
                    // rigidbody cinematico (nao cai por gravidade sozinho,
                    // controle fica com o script/codigo - ver
                    // BodyType::Kinematic em Components.h) + slot de script
                    // vazio pronto para preencher.
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_ActiveScene, "Character", "Character",
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
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Entidade Vazia")) {
                    // So Transform (todo Scene::CreateEntity ja da isso) -
                    // ponto de partida para montar qualquer combinacao de
                    // components manualmente via "+ Add Component".
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_ActiveScene, "Entidade Vazia", "Entidade Vazia",
                        CreatePresetEntityCommand::SetupFn{});
                    CreatePresetEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Excluir selecionada", nullptr, false, (bool)m_SelectedEntity)) {
                    m_CommandHistory.Execute(Prism::CreateScope<DeleteEntityCommand>(m_ActiveScene, m_SelectedEntity));
                    m_SelectedEntity = {};
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Janela")) {
                ImGui::MenuItem("Viewport", nullptr, true, false);
                ImGui::MenuItem("Hierarquia", nullptr, true, false);
                ImGui::MenuItem("Propriedades", nullptr, true, false);
                ImGui::MenuItem("Console", nullptr, true, false);
                ImGui::MenuItem("Conteudo do Projeto", nullptr, true, false);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

    void EditorLayer::RenderViewportPanel() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        ImVec2 size = ImGui::GetContentRegionAvail();
        // Nunca deixamos o tamanho chegar a zero - um framebuffer 0x0 e
        // invalido em OpenGL (ver Framebuffer::Resize, que ja ignora isso
        // tambem por seguranca).
        m_ViewportSize[0] = std::max(size.x, 1.0f);
        m_ViewportSize[1] = std::max(size.y, 1.0f);

        // A cena ja foi desenhada no framebuffer em OnUpdate() deste mesmo
        // frame - aqui so pegamos o color attachment (uma textura OpenGL
        // comum) e desenhamos como uma imagem dentro do painel ImGui. E
        // exatamente assim que Unity/Unreal/Godot/Hazel mostram a viewport
        // 3D dentro de uma janela de UI dockavel.
        uint32_t textureID = m_ViewportFramebuffer->GetColorAttachmentID();
        ImGui::Image((ImTextureID)(uintptr_t)textureID, ImVec2(m_ViewportSize[0], m_ViewportSize[1]),
                     ImVec2(0, 1), ImVec2(1, 0)); // UV invertido no Y: origem do framebuffer OpenGL e embaixo a esquerda.

        // Controle de camera minimo: segurar botao direito do mouse sobre a
        // viewport e arrastar orbita a camera; scroll (com o mouse sobre a
        // viewport) aproxima/afasta. E deliberadamente simples - vira a
        // camera de editor "de verdade" (com pan, foco em objeto, etc)
        // quando o resto do editor (gizmos de manipulacao, picking por
        // raycast) existir.
        if (m_ViewportHovered) {
            ImGuiIO& io = ImGui::GetIO();

            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                ImVec2 delta = io.MouseDelta;
                m_CameraYaw += delta.x * 0.4f;
                m_CameraPitch = std::clamp(m_CameraPitch - delta.y * 0.4f, -89.0f, 89.0f);
            }

            if (io.MouseWheel != 0.0f) {
                m_CameraDistance = std::clamp(m_CameraDistance - io.MouseWheel * 0.5f, 1.0f, 25.0f);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::RenderHierarchyPanel() {
        ImGui::Begin("Hierarquia");

        // Lista toda entidade da cena ativa (qualquer entidade com
        // TagComponent, ou seja, todas - ver Scene::CreateEntity). Clicar
        // seleciona; a selecao e o que a Properties panel usa para saber o
        // que mostrar/editar.
        m_ActiveScene->ForEachEntity([&](entt::entity handle, Prism::TagComponent& tag) {
            Prism::Entity entity(handle, m_ActiveScene.get());
            bool isSelected = (m_SelectedEntity == entity);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
                | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isSelected)
                flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)handle, flags, "%s", tag.Tag.c_str());
            if (ImGui::IsItemClicked())
                m_SelectedEntity = entity;
        });

        // Clicar em area vazia do painel desseleciona - convencao comum em
        // editores (Unity/Godot fazem o mesmo).
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
            m_SelectedEntity = {};

        ImGui::End();
    }

    void EditorLayer::RenderPropertiesPanel() {
        ImGui::Begin("Propriedades");

        if (!m_SelectedEntity) {
            ImGui::TextDisabled("Nada selecionado.");
            ImGui::End();
            return;
        }

        auto& tag = m_SelectedEntity.GetComponent<Prism::TagComponent>();
        char nameBuffer[256];
        strncpy(nameBuffer, tag.Tag.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        if (ImGui::InputText("Nome", nameBuffer, sizeof(nameBuffer)))
            tag.Tag = nameBuffer;

        ImGui::Separator();

        if (m_SelectedEntity.HasComponent<Prism::TransformComponent>()) {
            auto& transform = m_SelectedEntity.GetComponent<Prism::TransformComponent>();
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Cada DragFloat3 e verificado individualmente logo apos ser
                // desenhado - IsItemActivated()/IsItemDeactivatedAfterEdit()
                // sempre se referem ao ULTIMO item desenhado, entao nao da
                // para checar os tres so no final (so pegaria o de Escala).
                ImGui::DragFloat3("Posicao", glm::value_ptr(transform.Translation), 0.05f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<TransformCommand>(m_SelectedEntity, m_TransformBeforeEdit, transform));

                ImGui::DragFloat3("Rotacao", glm::value_ptr(transform.Rotation), 0.5f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<TransformCommand>(m_SelectedEntity, m_TransformBeforeEdit, transform));

                ImGui::DragFloat3("Escala", glm::value_ptr(transform.Scale), 0.05f, 0.01f, 100.0f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<TransformCommand>(m_SelectedEntity, m_TransformBeforeEdit, transform));
            }
            // Transform nao tem botao de remover - toda entidade tem um por
            // definicao (ver Scene::CreateEntity) e o resto da engine
            // assume isso (ex: RenderScene le GetTransform() sem checar
            // HasComponent primeiro).
        }

        if (m_SelectedEntity.HasComponent<Prism::MeshRendererComponent>()) {
            auto& meshRenderer = m_SelectedEntity.GetComponent<Prism::MeshRendererComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Mesh Renderer", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Mesh: Cubo (primitiva embutida)");

                ImGui::ColorEdit3("Cor", glm::value_ptr(meshRenderer.Color));
                if (ImGui::IsItemActivated())
                    m_ColorBeforeEdit = meshRenderer.Color;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<MeshColorCommand>(m_SelectedEntity, m_ColorBeforeEdit, meshRenderer.Color));
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::MeshRendererComponent>>(m_SelectedEntity, "Mesh Renderer"));
        }

        if (m_SelectedEntity.HasComponent<Prism::LightComponent>()) {
            auto& light = m_SelectedEntity.GetComponent<Prism::LightComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Light", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* typeNames[] = { "Point (Omni)", "Spot", "Directional" };
                int typeIndex = (int)light.Type;
                if (ImGui::Combo("Tipo", &typeIndex, typeNames, IM_ARRAYSIZE(typeNames)))
                    light.Type = (Prism::LightType)typeIndex;

                ImGui::ColorEdit3("Cor##Light", glm::value_ptr(light.Color));
                ImGui::DragFloat("Intensidade", &light.Intensity, 0.05f, 0.0f, 100.0f);

                if (light.Type != Prism::LightType::Directional)
                    ImGui::DragFloat("Alcance", &light.Range, 0.1f, 0.0f, 1000.0f);
                if (light.Type == Prism::LightType::Spot)
                    ImGui::DragFloat("Angulo do Cone", &light.SpotAngle, 0.5f, 1.0f, 90.0f);

                ImGui::TextDisabled("Ainda nao afeta a renderizacao (ver README).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::LightComponent>>(m_SelectedEntity, "Light"));
        }

        if (m_SelectedEntity.HasComponent<Prism::ColliderComponent>()) {
            auto& collider = m_SelectedEntity.GetComponent<Prism::ColliderComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Collider", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* shapeNames[] = { "Caixa", "Esfera", "Capsula" };
                int shapeIndex = (int)collider.Shape;
                if (ImGui::Combo("Forma", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames)))
                    collider.Shape = (Prism::ColliderShape)shapeIndex;

                switch (collider.Shape) {
                    case Prism::ColliderShape::Box:
                        ImGui::DragFloat3("Half-Extents", glm::value_ptr(collider.Size), 0.05f, 0.01f, 100.0f);
                        break;
                    case Prism::ColliderShape::Sphere:
                        ImGui::DragFloat("Raio", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                        break;
                    case Prism::ColliderShape::Capsule:
                        ImGui::DragFloat("Raio##Capsule", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                        ImGui::DragFloat("Altura##Capsule", &collider.Size.y, 0.05f, 0.01f, 100.0f);
                        break;
                }

                ImGui::Checkbox("E um Trigger", &collider.IsTrigger);
                ImGui::TextDisabled("Ainda nao alimenta simulacao de fisica (ver README).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ColliderComponent>>(m_SelectedEntity, "Collider"));
        }

        if (m_SelectedEntity.HasComponent<Prism::RigidBodyComponent>()) {
            auto& rigidBody = m_SelectedEntity.GetComponent<Prism::RigidBodyComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Rigid Body", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                if (!m_SelectedEntity.HasComponent<Prism::ColliderComponent>())
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Sem Collider - adicione um para a fisica funcionar.");

                const char* bodyTypeNames[] = { "Static", "Kinematic", "Dynamic" };
                int bodyTypeIndex = (int)rigidBody.Type;
                if (ImGui::Combo("Tipo##RigidBody", &bodyTypeIndex, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames)))
                    rigidBody.Type = (Prism::BodyType)bodyTypeIndex;

                bool dynamicOnly = (rigidBody.Type == Prism::BodyType::Dynamic);
                ImGui::BeginDisabled(!dynamicOnly);
                ImGui::DragFloat("Massa (kg)", &rigidBody.Mass, 0.1f, 0.01f, 10000.0f);
                ImGui::Checkbox("Usa Gravidade", &rigidBody.UseGravity);
                ImGui::EndDisabled();

                ImGui::Checkbox("Colisao Continua (CCD)", &rigidBody.ContinuousCollisionDetection);
                ImGui::TextDisabled("Ainda nao alimenta simulacao de fisica (ver README).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::RigidBodyComponent>>(m_SelectedEntity, "Rigid Body"));
        }

        if (m_SelectedEntity.HasComponent<Prism::ScriptComponent>()) {
            auto& script = m_SelectedEntity.GetComponent<Prism::ScriptComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Script", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                char scriptPathBuffer[256];
                strncpy(scriptPathBuffer, script.ScriptPath.c_str(), sizeof(scriptPathBuffer) - 1);
                scriptPathBuffer[sizeof(scriptPathBuffer) - 1] = '\0';
                ImGui::InputTextWithHint("Arquivo", "ex: player_controller.lua", scriptPathBuffer, sizeof(scriptPathBuffer));
                if (ImGui::IsItemDeactivatedAfterEdit())
                    script.ScriptPath = scriptPathBuffer;

                ImGui::TextDisabled("Relativo a Scripts/. Ainda nao executa (Lua nao esta embutido ainda).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ScriptComponent>>(m_SelectedEntity, "Script"));
        }

        ImGui::Dummy(ImVec2(0, 8));
        RenderAddComponentButton();

        ImGui::Separator();
        ImGui::TextDisabled("Camera do editor");
        ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_CameraYaw, m_CameraPitch);
        ImGui::Text("Distancia: %.2f", m_CameraDistance);

        ImGui::End();
    }

    void EditorLayer::RenderAddComponentButton() {
        // So mostra o botao se sobrar pelo menos um component que a
        // entidade ainda nao tem - evita um popup vazio (a entidade ja
        // tem TransformComponent sempre, entao esse nunca entra na lista).
        bool hasAnyMissing = !m_SelectedEntity.HasComponent<Prism::MeshRendererComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::LightComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::ColliderComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::RigidBodyComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::ScriptComponent>();

        if (!hasAnyMissing) {
            ImGui::TextDisabled("(todos os components ja adicionados)");
            return;
        }

        if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!m_SelectedEntity.HasComponent<Prism::MeshRendererComponent>() && ImGui::MenuItem("Mesh Renderer")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::MeshRendererComponent>>(m_SelectedEntity, "Mesh Renderer"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::LightComponent>() && ImGui::MenuItem("Light")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::LightComponent>>(m_SelectedEntity, "Light"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::ColliderComponent>() && ImGui::MenuItem("Collider")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::ColliderComponent>>(m_SelectedEntity, "Collider"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::RigidBodyComponent>() && ImGui::MenuItem("Rigid Body")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::RigidBodyComponent>>(m_SelectedEntity, "Rigid Body"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::ScriptComponent>() && ImGui::MenuItem("Script")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::ScriptComponent>>(m_SelectedEntity, "Script"));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorLayer::RenderConsolePanel() {
        m_ConsolePanel.OnImGuiRender();
    }

    void EditorLayer::RenderContentBrowserPanel() {
        m_ContentBrowser.OnImGuiRender();
    }

}
