#include "EditorLayer.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <GLFW/glfw3.h> 

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp> // glm::decompose() - GLM_ENABLE_EXPERIMENTAL ja definido globalmente em Components.h
#include <glm/gtc/quaternion.hpp> // glm::eulerAngles(quat) - usado em RenderTransformGizmo para converter o resultado de glm::decompose() de volta para os graus euler que TransformComponent guarda
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <fstream>
#include <filesystem>

namespace PrismEditor {

    // ID do popup modal de "Salvar Como" - compartilhado entre
    // RenderSaveAsPopup() (que o abre/desenha) e o atalho Ctrl+S/Ctrl+Shift+S
    // em RenderDockspace() (que precisa saber se ja esta aberto, para nao
    // tentar abrir de novo por cima de si mesmo).
    static constexpr const char* kSaveAsPopupId = "Salvar Mapa Como";
    static constexpr const char* kUnsavedChangesPopupId = "Alteracoes nao salvas";

    // Quanto tempo (s) sem editar o menu "Renderizacao" antes de gravar o
    // .prismproj - cobre arrastar slider, edicao pelo teclado (repeticao de
    // tecla muda o valor varios frames seguidos) e o picker de cor.
    static constexpr double kRenderSettingsSaveDelay = 0.35;
    static constexpr const char* kCreatePrefabPopupId = "Criar Prefab";
    static constexpr const char* kSaveMaterialPopupId = "Salvar Material Como";
    static constexpr const char* kNewScriptPopupId = "Novo Script";

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    void EditorLayer::OnAttach() {
        PRISM_INFO("EditorLayer anexada. Projeto ativo: ",
            Prism::Project::GetActive()->GetConfig().Name);

        // Visual do projeto (exposicao, ambiente): o Renderer e estatico e
        // guarda o ultimo valor aplicado, entao SEMPRE reaplica ao abrir um
        // projeto - senao ele herdaria os ajustes do projeto anterior.
        Prism::Renderer::ApplyRenderSettings(Prism::Project::GetActive()->GetRenderSettings());

        Prism::FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_ViewportFramebuffer = Prism::Framebuffer::Create(fbSpec);

        m_ContentBrowser.ResetToProjectRoot();
        m_ContentBrowser.SetOnMapDoubleClicked([this](const std::filesystem::path& mapPath) {
            // Copia do path: o Content Browser pode reaproveitar o buffer
            // dele antes do popup "Salvar alteracoes?" ser respondido (a
            // acao roda depois, em outro frame).
            RunAfterUnsavedCheck([this, mapPath]() { LoadScene(mapPath); });
            });

        // Botao X da janela (ou Alt+F4). O callback do GLFW roda no meio de
        // glfwPollEvents, cedo demais para abrir um popup ImGui - entao o
        // gancho so decide "deixa fechar?" e, se ha alteracoes, REGISTRA o
        // pedido (m_CloseWindowRequested) para OnImGuiRender abrir o popup
        // no frame seguinte, e recusa o fechamento por ora.
        Prism::Application::Get().SetCloseRequestHandler([this]() -> bool {
            if (m_AllowWindowClose)
                return true;
            if (!HasUnsavedChanges())
                return true;
            m_CloseWindowRequested = true;
            return false;
            });

        // Liga o menu de contexto de entidade (ver Panels/EntityContextMenuPanel.h)
        // as funcoes deste EditorLayer que de fato tem acesso a
        // CommandHistory/Scene/popups - o painel em si nao conhece nada
        // disso, so avisa QUAL acao foi escolhida e sobre QUAL entidade.
        m_EntityContextMenu.SetOnDuplicate([this](Prism::Entity entity) {
            DuplicateEntity(entity);
            });
        m_EntityContextMenu.SetOnDelete([this](Prism::Entity entity) {
            DeleteEntity(entity);
            });
        m_EntityContextMenu.SetOnCreatePrefabRequested([this](Prism::Entity entity) {
            m_PrefabToCreateFrom = entity;
            m_ShowCreatePrefabPopup = true;
            // Sugere o nome do arquivo a partir do Tag da entidade -
            // usuario pode trocar no popup antes de confirmar (ver
            // RenderCreatePrefabPopup).
            std::string suggested = entity.HasComponent<Prism::TagComponent>()
                ? entity.GetComponent<Prism::TagComponent>().Tag : std::string("Prefab");
            strncpy(m_CreatePrefabNameBuffer, suggested.c_str(), sizeof(m_CreatePrefabNameBuffer) - 1);
            m_CreatePrefabNameBuffer[sizeof(m_CreatePrefabNameBuffer) - 1] = '\0';
            });

        LoadOrCreateScene();
    }

    bool EditorLayer::LoadScene(const std::filesystem::path& mapPath) {
        // Se a PlayWindow estiver aberta, fecha ela ANTES de trocar de
        // mapa - ela roda uma COPIA clonada de m_ActiveScene (ver
        // Play/PlayWindow.h); nao faz sentido continuar simulando essa
        // copia depois que o mapa que a originou deixou de ser o ativo no
        // editor. m_ActiveScene em si NUNCA roda scripts/fisica, entao
        // trocar de mapa e uma operacao simples, sem nada para "desfazer".
        if (m_PlayWindow.IsOpen())
            OnStopButtonClicked();

        Prism::SceneSerializer serializer(Prism::Scene::Create());
        if (!serializer.Deserialize(mapPath)) {
            PRISM_ERROR("Falha ao carregar o mapa: ", mapPath.string());
            return false;
        }

        m_ActiveScene = serializer.GetScene();
        m_CurrentMapPath = mapPath;
        m_SelectedEntity = {};
        m_CommandHistory.Clear();
        MarkSceneClean();
        return true;
    }

    void EditorLayer::NewMap() {
        // NewMap() em si descarta a cena sem perguntar - quem pergunta e
        // o CHAMADOR, via RunAfterUnsavedCheck (ver RenderMenuBar). Assim
        // chamadas internas/programaticas nao ficam presas num popup.
        if (m_PlayWindow.IsOpen())
            OnStopButtonClicked(); // ver comentario identico em LoadScene()

        m_ActiveScene = Prism::Scene::Create("Nova Cena");
        m_CurrentMapPath.clear(); // sem arquivo associado ainda - "Salvar Mapa" vai se comportar como "Salvar Como"
        m_SelectedEntity = {};
        m_CommandHistory.Clear();

        MarkSceneClean();

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
        MarkSceneClean();
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
        TrySaveActiveScene();
    }

    bool EditorLayer::TrySaveActiveScene() {
        if (m_CurrentMapPath.empty()) {
            // Cena sem arquivo associado ainda (nova, ou criada por
            // NewMap()) - nao ha "onde" sobrescrever, entao pedimos um
            // nome, exatamente como Salvar Como faria. O save de verdade
            // so acontece num frame futuro (RenderSaveAsPopup), entao
            // aqui a resposta e "ainda nao salvou".
            SaveActiveSceneAs();
            return false;
        }

        if (!WriteSceneFile(m_ActiveScene, m_CurrentMapPath))
            return false;

        MarkSceneClean();
        return true;
    }

    bool EditorLayer::HasUnsavedChanges() const {
        if (!m_ActiveScene)
            return false;

        Prism::SceneSerializer serializer(m_ActiveScene);
        uint64_t current = serializer.ComputeFingerprint();

        // 0 = nao foi possivel calcular (ex: sem permissao na pasta
        // temporaria). Na duvida, assume que HA alteracoes: um aviso a mais
        // e inofensivo, ja um aviso a menos perde o trabalho do usuario.
        if (current == 0 || m_SavedSceneFingerprint == 0)
            return true;

        return current != m_SavedSceneFingerprint;
    }

    void EditorLayer::MarkSceneClean() {
        if (!m_ActiveScene) {
            m_SavedSceneFingerprint = 0;
            return;
        }
        Prism::SceneSerializer serializer(m_ActiveScene);
        m_SavedSceneFingerprint = serializer.ComputeFingerprint();
    }

    void EditorLayer::RunAfterUnsavedCheck(std::function<void()> action) {
        if (!HasUnsavedChanges()) {
            action();
            return;
        }

        // Ja ha um popup aberto esperando resposta: nao empilha uma segunda
        // acao por cima (ex: duplo clique em outro mapa com o popup
        // aberto). A primeira decisao do usuario ainda esta pendente.
        if (m_ShowUnsavedChangesPopup || ImGui::IsPopupOpen(kUnsavedChangesPopupId))
            return;

        m_PendingDiscardAction = std::move(action);
        m_ShowUnsavedChangesPopup = true;
    }

    void EditorLayer::RenderUnsavedChangesPopup() {
        // Pedido de fechar a janela chegado pelo gancho do Application.
        if (m_CloseWindowRequested) {
            m_CloseWindowRequested = false;
            RunAfterUnsavedCheck([this]() {
                // Libera o gancho para o proximo pedido passar direto, e
                // fecha pelo Close() (incondicional) em vez de depender do
                // GLFW: o X ja foi consumido/recusado uma vez.
                m_AllowWindowClose = true;
                Prism::Application::Get().Close();
                });
        }

        if (m_ShowUnsavedChangesPopup) {
            ImGui::OpenPopup(kUnsavedChangesPopupId);
            m_ShowUnsavedChangesPopup = false; // OpenPopup so precisa ser chamado uma vez
        }

        // A acao escolhida roda so depois de EndPopup(): ela pode trocar a
        // cena ativa ou pedir o fechamento do editor, e fazer isso no meio
        // do BeginPopupModal deixaria o popup manipulando estado que a acao
        // acabou de invalidar.
        std::function<void()> actionToRun;

        // Centraliza na janela principal: um aviso de "voce vai perder
        // seu trabalho" nao pode aparecer num canto onde passe batido.
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal(kUnsavedChangesPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            std::string mapName = m_CurrentMapPath.empty()
                ? m_ActiveScene->GetName() + " (ainda nao salvo)"
                : m_CurrentMapPath.filename().string();

            ImGui::TextWrapped("O mapa '%s' tem alteracoes nao salvas.", mapName.c_str());
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::TextDisabled("Se voce continuar sem salvar, elas serao perdidas.");
            ImGui::Dummy(ImVec2(0, 10));

            bool save = ImGui::Button("Salvar", ImVec2(110, 0));
            ImGui::SameLine();
            bool discard = ImGui::Button("Nao salvar", ImVec2(110, 0));
            ImGui::SameLine();
            bool cancel = ImGui::Button("Cancelar", ImVec2(110, 0));

            // Esc cancela: e a saida "segura" universal de um dialogo, e
            // impede que o usuario fique preso sem clicar num botao.
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                cancel = true;

            if (save) {
                if (TrySaveActiveScene()) {
                    // Salvou (cena ja tinha arquivo): segue com o que o
                    // usuario queria fazer.
                    ImGui::CloseCurrentPopup();
                    actionToRun = std::move(m_PendingDiscardAction);
                    m_PendingDiscardAction = nullptr;
                }
                else if (m_ShowSaveAsPopup) {
                    // Cena sem arquivo: TrySaveActiveScene() abriu o popup
                    // Salvar Como. A acao pendente fica guardada e so roda
                    // se o usuario CONFIRMAR o nome (ver RenderSaveAsPopup).
                    m_RunPendingActionAfterSaveAs = true;
                    ImGui::CloseCurrentPopup();
                }
                else {
                    // Save falhou (disco cheio, sem permissao...). NAO
                    // segue: continuar aqui perderia o trabalho justamente
                    // no cenario em que este aviso existe para proteger.
                    // O erro ja foi logado no Console por WriteSceneFile.
                    m_PendingDiscardAction = nullptr;
                    ImGui::CloseCurrentPopup();
                }
            }
            else if (discard) {
                ImGui::CloseCurrentPopup();
                actionToRun = std::move(m_PendingDiscardAction);
                m_PendingDiscardAction = nullptr;
            }
            else if (cancel) {
                m_PendingDiscardAction = nullptr;
                m_AllowWindowClose = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (actionToRun)
            actionToRun();
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
        // Acao pendente (fechar/novo mapa/abrir mapa) liberada por um save
        // bem sucedido. Roda so DEPOIS de EndPopup(): ela pode trocar a
        // cena ativa ou pedir o fechamento do editor, e fazer isso no meio
        // do BeginPopupModal deixaria o popup manipulando estado que a acao
        // acabou de invalidar.
        std::function<void()> actionAfterSaveAs;

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
            }
            else if (wouldOverwrite) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Ja existe um mapa com este nome - sera sobrescrito.");
            }
            else {
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

                    // O nome da cena mudou (SetName acima) e ele faz parte
                    // do que e comparado - o fingerprint tem que ser
                    // calculado DEPOIS dele, senao a cena recem-salva
                    // pareceria ter alteracoes.
                    MarkSceneClean();

                    // Veio de "Salvar" no popup de alteracoes nao salvas:
                    // agora que o arquivo existe de fato, segue com a acao
                    // que o usuario tinha pedido (fechar, novo mapa...).
                    if (m_RunPendingActionAfterSaveAs) {
                        m_RunPendingActionAfterSaveAs = false;
                        actionAfterSaveAs = std::move(m_PendingDiscardAction);
                        m_PendingDiscardAction = nullptr;
                    }
                }
                else if (m_RunPendingActionAfterSaveAs) {
                    // Escrita falhou: nao segue com a acao pendente.
                    m_RunPendingActionAfterSaveAs = false;
                    m_PendingDiscardAction = nullptr;
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled) {
                // Cancelou o nome: o usuario desistiu de salvar, entao a
                // acao que dependia disso (ex: fechar o editor) tambem e
                // cancelada - nunca fecha "sem salvar" sem ele ter pedido.
                m_RunPendingActionAfterSaveAs = false;
                m_PendingDiscardAction = nullptr;
                m_AllowWindowClose = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (actionAfterSaveAs)
            actionAfterSaveAs();
    }

    void EditorLayer::RenderCreatePrefabPopup() {
        if (m_ShowCreatePrefabPopup) {
            ImGui::OpenPopup(kCreatePrefabPopupId);
            m_ShowCreatePrefabPopup = false; // OpenPopup so precisa ser chamado uma vez, no frame em que o popup deve abrir
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kCreatePrefabPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            // A entidade pode ter sido excluida (por qualquer caminho -
            // undo, outro popup, etc) entre o clique no menu de contexto e
            // este frame - checagem defensiva antes de mexer nela.
            if (!m_PrefabToCreateFrom) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "A entidade de origem nao existe mais.");
                ImGui::Dummy(ImVec2(0, 8));
                if (ImGui::Button("Fechar", ImVec2(120, 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            ImGui::TextWrapped("Nome do prefab (a partir de '%s'):",
                m_PrefabToCreateFrom.GetComponent<Prism::TagComponent>().Tag.c_str());
            ImGui::SetNextItemWidth(-1);

            bool confirmedByEnter = ImGui::InputText("##CreatePrefabName", m_CreatePrefabNameBuffer, sizeof(m_CreatePrefabNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            auto project = Prism::Project::GetActive();
            std::string name = m_CreatePrefabNameBuffer;

            // Mesma sanitizacao minima que RenderSaveAsPopup ja usa (ver
            // comentario la) - caracteres proibidos em nomes de arquivo
            // viram underscore.
            static const std::string kForbiddenChars = "/\\:*?\"<>|";
            for (auto& c : name)
                if (kForbiddenChars.find(c) != std::string::npos)
                    c = '_';

            bool nameEmpty = name.empty();

            std::filesystem::path previewPath = project->GetPrefabDirectory() / (name + ".prismprefab");
            bool wouldOverwrite = !nameEmpty && std::filesystem::exists(previewPath);

            // Conta quantas entidades vao para o arquivo (raiz + toda a
            // subarvore) - so uma informacao a mais para o usuario
            // perceber, ANTES de confirmar, que um prefab com filhos leva
            // TUDO junto (nao so a entidade clicada) - mesmo espirito do
            // preview de "sera sobrescrito" em RenderSaveAsPopup.
            size_t childCount = m_PrefabToCreateFrom.GetChildCount();

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o prefab.");
            }
            else if (wouldOverwrite) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Ja existe um prefab com este nome - sera sobrescrito.");
            }
            else {
                ImGui::TextDisabled("%s", previewPath.filename().string().c_str());
            }
            if (childCount > 0)
                ImGui::TextDisabled("Inclui %zu entidade(s) filha(s).", childCount);

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Criar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty;

            if (confirmed) {
                if (!Prism::PrefabSerializer::Serialize(m_PrefabToCreateFrom, previewPath))
                    PRISM_ERROR("Falha ao criar prefab '", name, "' - ver console para detalhes.");
                m_PrefabToCreateFrom = {};
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled) {
                m_PrefabToCreateFrom = {};
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void EditorLayer::InstantiatePrefab(const std::filesystem::path& prefabPath, Prism::Entity parent) {
        auto command = Prism::CreateScope<InstantiatePrefabCommand>(m_ActiveScene, prefabPath);
        InstantiatePrefabCommand* raw = command.get();
        m_CommandHistory.Execute(std::move(command));
        Prism::Entity instantiated = raw->GetInstantiatedEntity();

        if (instantiated && parent)
            m_CommandHistory.Execute(Prism::CreateScope<SetParentCommand>(m_ActiveScene, instantiated, Prism::Entity{}, parent));

        m_SelectedEntity = instantiated;
    }

    void EditorLayer::RenderSaveMaterialPopup() {
        if (m_ShowSaveMaterialPopup) {
            ImGui::OpenPopup(kSaveMaterialPopupId);
            m_ShowSaveMaterialPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kSaveMaterialPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!m_SelectedEntity || !m_SelectedEntity.HasComponent<Prism::MaterialComponent>()) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Nenhuma entidade com Material selecionada.");
                ImGui::Dummy(ImVec2(0, 8));
                if (ImGui::Button("Fechar", ImVec2(120, 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            ImGui::TextWrapped("Nome do material:");
            ImGui::SetNextItemWidth(-1);
            bool confirmedByEnter = ImGui::InputText("##SaveMaterialName", m_SaveMaterialNameBuffer, sizeof(m_SaveMaterialNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            auto project = Prism::Project::GetActive();
            std::string name = m_SaveMaterialNameBuffer;

            static const std::string kForbiddenChars = "/\\:*?\"<>|";
            for (auto& c : name)
                if (kForbiddenChars.find(c) != std::string::npos)
                    c = '_';

            bool nameEmpty = name.empty();
            std::filesystem::path previewPath = project->GetMaterialDirectory() / (name + ".prismmat");
            bool wouldOverwrite = !nameEmpty && std::filesystem::exists(previewPath);

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty)
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o material.");
            else if (wouldOverwrite)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Ja existe um material com este nome - sera sobrescrito.");
            else
                ImGui::TextDisabled("%s", previewPath.filename().string().c_str());

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Salvar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty;

            if (confirmed) {
                auto& material = m_SelectedEntity.GetComponent<Prism::MaterialComponent>();
                if (!Prism::MaterialSerializer::Serialize(material, previewPath))
                    PRISM_ERROR("Falha ao salvar material '", name, "' - ver console para detalhes.");
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    std::vector<std::string> EditorLayer::ListProjectScripts() const {
        std::vector<std::string> result;

        auto project = Prism::Project::GetActive();
        if (!project)
            return result;

        std::error_code ec;
        std::filesystem::path scriptDir = project->GetScriptDirectory();
        if (!std::filesystem::exists(scriptDir, ec))
            return result;

        // NAO recursivo (ver comentario no header) - so arquivos .lua
        // diretamente dentro de Scripts/, comparado case-insensitive para
        // aceitar ".LUA"/".Lua" tambem (extensao pode vir de qualquer jeito
        // dependendo de como o arquivo foi criado fora do editor).
        for (const auto& entry : std::filesystem::directory_iterator(scriptDir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file())
                continue;

            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".lua")
                continue;

            result.push_back(entry.path().filename().string());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    std::filesystem::path EditorLayer::CreateNewScript(const std::string& name) {
        auto project = Prism::Project::GetActive();
        if (!project || name.empty())
            return {};

        // Mesma sanitizacao ja usada em RenderSaveAsPopup - nomes de
        // arquivo nao devem conter caracteres proibidos pelo SO.
        std::string safeName = name;
        static const std::string kForbiddenChars = "/\\:*?\"<>|";
        for (auto& c : safeName)
            if (kForbiddenChars.find(c) != std::string::npos)
                c = '_';

        std::filesystem::path relativePath = safeName + ".lua";
        std::filesystem::path absolutePath = project->GetScriptDirectory() / relativePath;

        if (std::filesystem::exists(absolutePath)) {
            PRISM_CORE_ERROR("CreateNewScript: ja existe um script chamado '", relativePath.string(), "'.");
            return {};
        }

        // Template minimo - os tres callbacks especiais comentados, mesmo
        // vocabulario/formato dos exemplos em
        // PrismEditor/assets/ScriptExamples/ (ver example_spin.lua) - assim
        // um script novo ja mostra a API basica sem o usuario precisar ir
        // procurar um exemplo em outro lugar.
        std::ofstream file(absolutePath);
        if (!file.is_open()) {
            PRISM_CORE_ERROR("CreateNewScript: falha ao criar '", absolutePath.string(), "'.");
            return {};
        }

        file <<
            "-- " << relativePath.string() << "\n"
            "-- Tres funcoes especiais, TODAS opcionais (defina so as que precisar):\n"
            "--   OnCreate()            -- chamada uma vez, quando o Play comeca\n"
            "--   OnUpdate(deltaTime)   -- chamada toda frame enquanto o Play estiver ligado\n"
            "--   OnDestroy()           -- chamada uma vez quando o Play para\n"
            "--\n"
            "-- A variavel global `entity` ja existe dentro do script - e a PROPRIA\n"
            "-- entidade dona deste ScriptComponent.\n"
            "\n"
            "function OnCreate()\n"
            "    log(entity:GetName() .. \": OnCreate\")\n"
            "end\n"
            "\n"
            "function OnUpdate(deltaTime)\n"
            "\n"
            "end\n"
            "\n"
            "function OnDestroy()\n"
            "\n"
            "end\n";
        file.close();

        PRISM_CORE_INFO("CreateNewScript: criado '", absolutePath.string(), "'.");
        return relativePath;
    }

    void EditorLayer::RenderNewScriptPopup() {
        if (m_ShowNewScriptPopup) {
            ImGui::OpenPopup(kNewScriptPopupId);
            m_ShowNewScriptPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kNewScriptPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Nome do script (sem extensao):");
            ImGui::SetNextItemWidth(-1);

            bool confirmedByEnter = ImGui::InputText("##NewScriptName", m_NewScriptNameBuffer, sizeof(m_NewScriptNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            std::string name = m_NewScriptNameBuffer;
            bool nameEmpty = name.empty();

            auto project = Prism::Project::GetActive();
            std::filesystem::path previewPath = project ? (project->GetScriptDirectory() / (name + ".lua")) : std::filesystem::path{};
            bool wouldOverwrite = !nameEmpty && project && std::filesystem::exists(previewPath);

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o script.");
            }
            else if (wouldOverwrite) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Ja existe um script com este nome.");
            }
            else {
                ImGui::TextDisabled("%s", (name + ".lua").c_str());
            }

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Criar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty && !wouldOverwrite;

            if (confirmed) {
                std::filesystem::path relativePath = CreateNewScript(name);
                if (!relativePath.empty() && m_SelectedEntity && m_SelectedEntity.HasComponent<Prism::ScriptComponent>()) {
                    // Atribui o script recem-criado diretamente ao
                    // ScriptComponent da entidade selecionada (a mesma que
                    // tinha o botao "Novo..." clicado - ver
                    // RenderPropertiesPanel) e ja abre no editor, poupando
                    // o usuario de digitar o nome de novo no combo.
                    m_SelectedEntity.GetComponent<Prism::ScriptComponent>().ScriptPath = relativePath.string();
                    m_ScriptEditor.Open(project->GetScriptDirectory() / relativePath);
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // Abre a PlayWindow (janela separada do SO, ver Play/PlayWindow.h) com
    // uma copia clonada de m_ActiveScene. GetNativeWindow() da janela do
    // editor (Application::GetWindow()) e passado como contexto a
    // compartilhar - ver comentario extenso sobre isso no topo de
    // PlayWindow.h.
    void EditorLayer::OnPlayButtonClicked() {
        if (m_PlayWindow.IsOpen())
            return; // ja aberta - o botao vira "Parar" nesse caso, ver RenderDockspace, entao isto nao deveria ser alcancavel na pratica

        GLFWwindow* editorWindow = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();
        m_PlayWindow.Open(m_ActiveScene, editorWindow);
    }

    void EditorLayer::OnStopButtonClicked() {
        m_PlayWindow.Close();
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
        m_CameraLookActive = false;

        // Ajuste de renderizacao editado poucos instantes antes de fechar
        // (a espera de FlushRenderSettingsSave ainda nao acabou): grava agora
        // para nao perder. Project::SaveActive so usa o Project estatico -
        // nada de Application::Get() aqui (ver comentario acima). Sem log:
        // durante a destruicao do Application o logging nao e garantido.
        if (m_RenderSettingsNeedSave) {
            m_RenderSettingsNeedSave = false;
            Prism::Project::SaveActive();
        }
    }

    void EditorLayer::OnUpdate(float deltaTime) {
        // Redimensiona o framebuffer se o painel Viewport mudou de tamanho
        // desde o ultimo frame (arrastar a janela, dockar/desdockar, etc).
        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        if (m_ViewportSize[0] > 0.0f && m_ViewportSize[1] > 0.0f &&
            (spec.Width != (uint32_t)m_ViewportSize[0] || spec.Height != (uint32_t)m_ViewportSize[1])) {
            m_ViewportFramebuffer->Resize((uint32_t)m_ViewportSize[0], (uint32_t)m_ViewportSize[1]);
        }

        // m_ActiveScene->OnUpdate() aqui NAO roda scripts/fisica na pratica
        // (Scene::OnUpdate so simula quando IsRunning() e true - ver
        // Scene.cpp) - m_ActiveScene, a Scene de EDICAO, nunca chama
        // OnScriptsStart() mais (isso agora acontece so na copia clonada
        // dentro de m_PlayWindow, ver PlayWindow::Open). Mantido mesmo
        // assim por seguranca/futuro (caso algo alem de scripts/fisica
        // precise rodar por frame mesmo fora do Play).
        m_ActiveScene->OnUpdate(deltaTime);

        RenderScene(deltaTime);

        // Nao chamamos mais RenderCameraPreview() aqui - agora ela e
        // chamada sob demanda dentro de RenderPropertiesPanel().

        // PlayWindow::OnUpdate cuida do proprio ciclo (simulacao + desenho
        // + eventos) da janela separada de Play, se estiver aberta -
        // idempotente/no-op quando fechada (ver PlayWindow::OnUpdate).
        // Precisa vir DEPOIS de RenderScene()/RenderCameraPreview() acima:
        // aquelas duas funcoes assumem que o contexto OpenGL ativo e o da
        // janela do editor (nunca trocam de contexto) - chamando
        // PlayWindow::OnUpdate por ultimo, qualquer troca de contexto que
        // ela fizer internamente (ver PlayWindow.cpp) so acontece depois
        // que o editor ja terminou de desenhar tudo que precisava neste
        // frame.
        GLFWwindow* editorWindow = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();
        m_PlayWindow.OnUpdate(deltaTime, editorWindow);
    }

    glm::mat4 EditorLayer::ComputeEditorViewMatrix() const {
        // Direcao "frente" da camera a partir de yaw/pitch - mesma convencao
        // de eixos que a antiga camera de orbita usava implicitamente (yaw
        // gira em torno de Y, pitch em torno de X local). Antes este vetor
        // era "da posicao da orbita para a origem"; agora e simplesmente a
        // direcao que a camera olha a partir da posicao atual.
        //
        // -Z e a "frente" padrao de camera em OpenGL/glm (mesma convencao
        // que CameraComponent usa via glm::inverse(worldTransform) em
        // PlayWindow/RenderCameraPreview, e que o gizmo de camera em
        // RenderCameraGizmos assume ao desenhar o frustum para -Z local).
        // Os sinais negativos aqui seguem essa mesma convencao para que a
        // camera do editor e a camera de jogo (Play) olhem "para o mesmo
        // lado" dado o mesmo yaw/pitch.
        float yawRad = glm::radians(m_CameraYaw);
        float pitchRad = glm::radians(m_CameraPitch);

        glm::vec3 forward(
            -cosf(pitchRad) * cosf(yawRad),
            -sinf(pitchRad),
            -cosf(pitchRad) * sinf(yawRad)
        );

        return glm::lookAt(m_CameraPosition, m_CameraPosition + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void EditorLayer::RenderScene(float deltaTime) {
        m_ViewportFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        float aspect = spec.Height > 0 ? (float)spec.Width / (float)spec.Height : 1.0f;

        // A viewport principal do editor usa SEMPRE a camera LIVRE do
        // editor (m_CameraPosition + yaw/pitch) - nunca a
        // CameraComponent::Primary da cena, mesmo que exista uma. Ver o
        // comentario em m_CameraPosition (EditorLayer.h) e em
        // RenderCameraPreview() para o motivo: substituir a viewport
        // principal pela camera de jogo te deixa "preso" dentro de
        // qualquer mesh onde a camera esteja posicionada (ex: dentro da
        // capsula de colisao de um character), sem visao de trabalho para
        // corrigir isso. A camera de jogo tem sua propria preview separada
        // (RenderCameraPreview/m_CameraPreviewFramebuffer).
        //
        // ComputeEditorViewMatrix() e o unico lugar que sabe montar esta
        // view (usado aqui E em RenderViewportPanel, para picking/ImGuizmo)
        // - garantir que os dois usem exatamente a mesma matriz e o que
        // impede a selecao com o mouse de dessincronizar da imagem.
        glm::mat4 view = ComputeEditorViewMatrix();
        // Far de 1000 (era 100): com movimento livre, o usuario pode se
        // afastar bastante da cena antes de querer ver tudo - um far
        // pequeno faria a geometria "sumir" do outro lado antes do usuario
        // terminar de se posicionar.
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
        glm::mat4 viewProjection = projection * view;

        // Renderer::DrawScene ja chama SetCameraPosition() internamente
        // (necessario ANTES de qualquer DrawMesh() deste framebuffer - ver
        // comentario em Renderer::SetCameraPosition, Renderer.h, sobre o
        // teste de "face interna transparente") - nao precisa ser feito
        // aqui separadamente.
        //
        // 'view'/'projection' passadas SEPARADAS (nao 'viewProjection'
        // combinada) desde que DrawScene ganhou SSAO - ver comentario na
        // assinatura de Renderer::DrawScene (Renderer.h).
        Prism::Renderer::DrawScene(*m_ActiveScene, glm::value_ptr(view), glm::value_ptr(projection), glm::value_ptr(m_CameraPosition));
        RenderCameraGizmos(viewProjection);
        RenderSelectedColliderGizmo(viewProjection);
        RenderLightGizmos(viewProjection);
        RenderRaycastGizmos(viewProjection);

        m_ViewportFramebuffer->Unbind();
    }

    uint32_t EditorLayer::RenderCameraPreview(Prism::Entity cameraEntity, float width, float height) {
        if (!cameraEntity || !cameraEntity.HasComponent<Prism::CameraComponent>())
            return 0;

        // Cria o framebuffer sob demanda
        if (!m_CameraPreviewFramebuffer) {
            Prism::FramebufferSpecification fbSpec;
            fbSpec.Width = (uint32_t)std::max(width, 1.0f);
            fbSpec.Height = (uint32_t)std::max(height, 1.0f);
            m_CameraPreviewFramebuffer = Prism::Framebuffer::Create(fbSpec);
        }

        // Redimensiona se necessário
        const auto& spec = m_CameraPreviewFramebuffer->GetSpecification();
        uint32_t w = (uint32_t)std::max(width, 1.0f);
        uint32_t h = (uint32_t)std::max(height, 1.0f);
        if (spec.Width != w || spec.Height != h) {
            m_CameraPreviewFramebuffer->Resize(w, h);
        }

        m_CameraPreviewFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        float aspect = h > 0 ? (float)w / (float)h : 1.0f;
        auto& camera = cameraEntity.GetComponent<Prism::CameraComponent>();
        glm::mat4 worldTransform = m_ActiveScene->GetWorldTransform(cameraEntity);
        glm::mat4 view = glm::inverse(worldTransform);
        glm::mat4 projection = camera.GetProjection(aspect);
        glm::vec3 worldPos = glm::vec3(worldTransform[3]);

        Prism::Renderer::DrawScene(*m_ActiveScene, glm::value_ptr(view), glm::value_ptr(projection), glm::value_ptr(worldPos));

        m_CameraPreviewFramebuffer->Unbind();
        return m_CameraPreviewFramebuffer->GetColorAttachmentID();
    }

    void EditorLayer::RenderCameraGizmos(const glm::mat4& viewProjection) {
        // Desenha um frustum simples (piramide com base retangular, apice
        // na posicao da camera) para toda entidade com CameraComponent -
        // sem isso, uma camera seria invisivel na viewport (ela nao tem
        // MeshRendererComponent). So Perspective desenha um frustum de
        // verdade (leque abrindo do apice); Orthographic desenha uma caixa
        // (os planos near/far tem o mesmo tamanho, sem convergencia).
        auto view = m_ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::CameraComponent>();
        for (auto entityHandle : view) {
            auto& camera = view.get<Prism::CameraComponent>(entityHandle);

            // Tamanho fixo de exibicao (nao o Far real da camera, que pode
            // ser gigante e tornar o gizmo inutilizavel visualmente) - so
            // near/far "curtos" para dar a nocao de direcao/abertura.
            constexpr float kGizmoNear = 0.15f;
            constexpr float kGizmoFar = 0.6f;

            float nearHalfHeight, nearHalfWidth, farHalfHeight, farHalfWidth;
            // Aspect fixo 16:9 para o gizmo, independente do aspect real do
            // viewport de destino - o gizmo e so uma indicacao visual de
            // "aqui existe uma camera olhando nesta direcao", nao uma
            // preview exata do frustum (isso ficaria caro/complexo de
            // manter em sincronia com o Framebuffer real).
            constexpr float kGizmoAspect = 16.0f / 9.0f;

            if (camera.ProjectionType == Prism::CameraProjectionType::Orthographic) {
                float halfHeight = camera.OrthoSize * 0.5f * 0.15f; // escalado para o mesmo tamanho visual do frustum perspective
                nearHalfHeight = farHalfHeight = halfHeight;
                nearHalfWidth = farHalfWidth = halfHeight * kGizmoAspect;
            }
            else {
                float tanHalfFov = tanf(glm::radians(camera.FOV) * 0.5f);
                nearHalfHeight = kGizmoNear * tanHalfFov;
                nearHalfWidth = nearHalfHeight * kGizmoAspect;
                farHalfHeight = kGizmoFar * tanHalfFov;
                farHalfWidth = farHalfHeight * kGizmoAspect;
            }

            glm::mat4 model = m_ActiveScene->GetWorldTransform(Prism::Entity(entityHandle, m_ActiveScene.get()));
            // Camera olha para -Z local (convencao padrao de camera em
            // OpenGL/glm, igual view = glm::inverse(model) em RenderScene()
            // assume implicitamente).
            auto toWorld = [&](float x, float y, float z) {
                return glm::vec3(model * glm::vec4(x, y, -z, 1.0f));
                };

            glm::vec3 apex = toWorld(0, 0, 0);
            glm::vec3 nearTL = toWorld(-nearHalfWidth, nearHalfHeight, kGizmoNear);
            glm::vec3 nearTR = toWorld(nearHalfWidth, nearHalfHeight, kGizmoNear);
            glm::vec3 nearBL = toWorld(-nearHalfWidth, -nearHalfHeight, kGizmoNear);
            glm::vec3 nearBR = toWorld(nearHalfWidth, -nearHalfHeight, kGizmoNear);
            glm::vec3 farTL = toWorld(-farHalfWidth, farHalfHeight, kGizmoFar);
            glm::vec3 farTR = toWorld(farHalfWidth, farHalfHeight, kGizmoFar);
            glm::vec3 farBL = toWorld(-farHalfWidth, -farHalfHeight, kGizmoFar);
            glm::vec3 farBR = toWorld(farHalfWidth, -farHalfHeight, kGizmoFar);

            // 8 segmentos: retangulo near, retangulo far, 4 arestas
            // conectando near->far (para Perspective isso converge para o
            // apice se near for pequeno o bastante - aqui so desenhamos o
            // retangulo near normalmente, ja que kGizmoNear > 0).
            std::vector<glm::vec3> points = {
                nearTL, nearTR,  nearTR, nearBR,  nearBR, nearBL,  nearBL, nearTL, // retangulo near
                farTL, farTR,    farTR, farBR,    farBR, farBL,    farBL, farTL,   // retangulo far
                nearTL, farTL,   nearTR, farTR,   nearBR, farBR,   nearBL, farBL,  // arestas conectando
                apex, nearTL,    apex, nearTR,    apex, nearBR,    apex, nearBL,   // apice ao retangulo near (indica a origem/posicao da camera)
            };

            // Primary usa uma cor diferente (ciano) das demais (cinza) -
            // ajuda a identificar de relance qual camera o modo Play vai
            // usar quando ha mais de uma na cena.
            glm::vec3 color = camera.Primary ? glm::vec3(0.25f, 0.85f, 0.95f) : glm::vec3(0.6f, 0.6f, 0.6f);

            Prism::Renderer::DrawLines(glm::value_ptr(points[0]), (uint32_t)points.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
        }
    }

    void EditorLayer::RenderSelectedColliderGizmo(const glm::mat4& viewProjection) {
        // So desenha se a entidade selecionada tiver Transform + Collider -
        // sem selecao (m_SelectedEntity invalida) ou sem ColliderComponent,
        // nao ha nada a fazer.
        if (!m_SelectedEntity || !m_SelectedEntity.HasComponent<Prism::ColliderComponent>())
            return;

        auto& collider = m_SelectedEntity.GetComponent<Prism::ColliderComponent>();

        // Gizmo do collider usa a posicao/rotacao de MUNDO da entidade
        // (ancestrais inclusos, via GetWorldTransform - ver parenting em
        // Scene.h), mas DELIBERADAMENTE SEM a Scale de mundo - Collider::Size
        // (half-extents/raio) e uma medida em unidades ABSOLUTAS que o
        // usuario ajusta manualmente na Properties panel, independente do
        // tamanho do mesh visual (ver comentario em ColliderComponent,
        // Components.h) - e assim que PhysicsEngine::CreateBodyForEntity
        // tambem trata (usa Size diretamente, nunca multiplica pela Scale
        // da entidade). Usar GetWorldTransform() completo aqui (incluindo
        // Scale) aplicava a escala DUAS vezes de fato (uma no proprio
        // Size, que o usuario ja pensa em unidades finais, e outra via
        // matriz) sempre que a entidade tinha Scale != {1,1,1} - por
        // exemplo, um "chao" com mesh Plane escalado para {10,1,10}
        // deixava o gizmo do collider gigantesco e desalinhado do que a
        // fisica de fato usava (que ignorava a Scale). Corrigido extraindo
        // so posicao+rotacao da matriz de mundo, sem escala.
        glm::mat4 worldMatrixWithScale = m_ActiveScene->GetWorldTransform(m_SelectedEntity);
        glm::vec3 worldPosition = glm::vec3(worldMatrixWithScale[3]);
        glm::vec3 col0 = glm::vec3(worldMatrixWithScale[0]);
        glm::vec3 col1 = glm::vec3(worldMatrixWithScale[1]);
        glm::vec3 col2 = glm::vec3(worldMatrixWithScale[2]);
        glm::mat3 worldRotationOnly(
            glm::length(col0) > 0.00001f ? col0 / glm::length(col0) : glm::vec3(1, 0, 0),
            glm::length(col1) > 0.00001f ? col1 / glm::length(col1) : glm::vec3(0, 1, 0),
            glm::length(col2) > 0.00001f ? col2 / glm::length(col2) : glm::vec3(0, 0, 1)
        );
        auto toWorld = [&](const glm::vec3& local) {
            return worldPosition + worldRotationOnly * local;
            };

        // Amarelo: convencao comum de "gizmo de colisao selecionado" (Unity
        // usa verde-claro, Unreal usa laranja/vermelho, Godot usa um roxo
        // claro - amarelo aqui so para ficar bem distinto do ciano/cinza ja
        // usados pelas cameras, ver RenderCameraGizmos acima).
        glm::vec3 color(0.95f, 0.85f, 0.2f);

        // Gera os pontos (pares consecutivos = segmentos, ver DrawLines) de
        // um circulo de raio 'radius' no plano perpendicular a 'axis' (0=X,
        // 1=Y, 2=Z), centrado em 'center' (espaco local, antes de toWorld),
        // com 'segments' segmentos - usado tanto para Sphere (3 circulos
        // ortogonais) quanto para as tampas da Capsule.
        auto appendCircle = [&](std::vector<glm::vec3>& points, glm::vec3 center, float radius, int axis, int segments) {
            glm::vec3 prev;
            for (int i = 0; i <= segments; i++) {
                float t = (float)i / (float)segments * 2.0f * 3.14159265f;
                float c = radius * cosf(t);
                float s = radius * sinf(t);
                glm::vec3 p = center;
                if (axis == 0)      p += glm::vec3(0.0f, c, s); // circulo no plano YZ (perpendicular a X)
                else if (axis == 1) p += glm::vec3(c, 0.0f, s); // circulo no plano XZ (perpendicular a Y)
                else                p += glm::vec3(c, s, 0.0f); // circulo no plano XY (perpendicular a Z)

                if (i > 0) { points.push_back(prev); points.push_back(p); }
                prev = p;
            }
            };

        // Gera um arco de 180 graus (meio-circulo) de raio 'radius',
        // centrado em 'center', comecando na direcao 'startAxis' e
        // terminando na direcao 'endAxis' (dois eixos ortogonais entre si -
        // ex: startAxis=(1,0,0), endAxis=(0,1,0) desenha o quarto de volta
        // de +X ate +Y, e o proximo quarto de +Y ate -X, completando meia
        // volta). Usado para as calotas hemisfericas da Capsule (2 arcos
        // por calota = uma "cruz" de meridianos, dando a nocao de cupula
        // sem precisar de uma malha completa).
        auto appendArc = [&](std::vector<glm::vec3>& points, glm::vec3 center, float radius, glm::vec3 startAxis, glm::vec3 endAxis, int segments) {
            glm::vec3 prev;
            for (int i = 0; i <= segments; i++) {
                float t = (float)i / (float)segments * 3.14159265f; // 0 .. PI (meia volta)
                glm::vec3 p = center + radius * (startAxis * cosf(t) + endAxis * sinf(t));
                if (i > 0) { points.push_back(prev); points.push_back(p); }
                prev = p;
            }
            };

        std::vector<glm::vec3> localPoints;
        constexpr int kCircleSegments = 24;
        constexpr int kArcSegments = 12;

        switch (collider.Shape) {
        case Prism::ColliderShape::Box: {
            // Size e ja meio-extensao (half-extents) - ver comentario em
            // ColliderComponent (Components.h).
            glm::vec3 e = collider.Size;
            glm::vec3 c[8] = {
                { -e.x,-e.y,-e.z }, {  e.x,-e.y,-e.z }, {  e.x, e.y,-e.z }, { -e.x, e.y,-e.z }, // face -Z
                { -e.x,-e.y, e.z }, {  e.x,-e.y, e.z }, {  e.x, e.y, e.z }, { -e.x, e.y, e.z }, // face +Z
            };
            int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0}, // face -Z
                {4,5},{5,6},{6,7},{7,4}, // face +Z
                {0,4},{1,5},{2,6},{3,7}, // arestas conectando as duas faces
            };
            for (auto& e2 : edges) { localPoints.push_back(c[e2[0]]); localPoints.push_back(c[e2[1]]); }
            break;
        }
        case Prism::ColliderShape::Sphere: {
            float r = collider.Size.x; // so Size.x e usado como raio, ver ColliderComponent
            appendCircle(localPoints, glm::vec3(0.0f), r, 0, kCircleSegments);
            appendCircle(localPoints, glm::vec3(0.0f), r, 1, kCircleSegments);
            appendCircle(localPoints, glm::vec3(0.0f), r, 2, kCircleSegments);
            break;
        }
        case Prism::ColliderShape::Capsule: {
            // Size.x = raio, Size.y = altura TOTAL da capsula, incluindo
            // as duas calotas hemisfericas (Size.z ignorado - ver
            // ColliderComponent). O "cilindro" do meio vai de
            // -halfCylinderHeight a +halfCylinderHeight; cada calota e
            // uma hemisfera de raio 'radius' colada em cada ponta,
            // desenhada com 2 arcos de meridiano (planos XY e ZY) + o
            // equador (reaproveitando appendCircle) - suficiente para
            // ler "isto e uma capsula, nao um cilindro" de relance, sem
            // precisar de uma malha completa de esfera.
            float radius = collider.Size.x;
            float halfCylinderHeight = std::max(collider.Size.y * 0.5f - radius, 0.0f); // metade da parte cilindrica, descontando as 2 calotas de raio 'radius'

            // Equador do cilindro (topo e base da parte reta).
            appendCircle(localPoints, glm::vec3(0.0f, halfCylinderHeight, 0.0f), radius, 1, kCircleSegments);
            appendCircle(localPoints, glm::vec3(0.0f, -halfCylinderHeight, 0.0f), radius, 1, kCircleSegments);

            // Calota de cima: hemisferio acima de y=halfCylinderHeight,
            // desenhado como 2 meridianos de 180 graus (de +X a +Y, e
            // de +Z a +Y) - a metade "de cima" do arco (de 0 a PI/2 ja
            // cobre o quarto que importa, mas usar o arco completo de
            // +X/+Z ate -X/-Z passando por +Y da a cupula inteira numa
            // linha so por meridiano).
            glm::vec3 topCenter(0.0f, halfCylinderHeight, 0.0f);
            appendArc(localPoints, topCenter, radius, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), kArcSegments);
            appendArc(localPoints, topCenter, radius, glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), kArcSegments);
            appendArc(localPoints, topCenter, radius, glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), kArcSegments);
            appendArc(localPoints, topCenter, radius, glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), kArcSegments);

            // Calota de baixo: espelhada (aponta para -Y em vez de +Y).
            glm::vec3 bottomCenter(0.0f, -halfCylinderHeight, 0.0f);
            appendArc(localPoints, bottomCenter, radius, glm::vec3(1, 0, 0), glm::vec3(0, -1, 0), kArcSegments);
            appendArc(localPoints, bottomCenter, radius, glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0), kArcSegments);
            appendArc(localPoints, bottomCenter, radius, glm::vec3(0, 0, 1), glm::vec3(0, -1, 0), kArcSegments);
            appendArc(localPoints, bottomCenter, radius, glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), kArcSegments);

            // 4 linhas verticais ao redor do cilindro (nas direcoes
            // +X/-X/+Z/-Z) conectando o equador de cima ao de baixo -
            // sem essas, as duas calotas + equadores pareceriam 2
            // esferas soltas em vez de uma capsula conectada.
            glm::vec3 dirs[4] = { {radius,0,0}, {-radius,0,0}, {0,0,radius}, {0,0,-radius} };
            for (auto& d : dirs) {
                localPoints.push_back(topCenter + d);
                localPoints.push_back(bottomCenter + d);
            }
            break;
        }
        }

        std::vector<glm::vec3> worldPoints;
        worldPoints.reserve(localPoints.size());
        for (auto& p : localPoints)
            worldPoints.push_back(toWorld(p));

        if (!worldPoints.empty())
            Prism::Renderer::DrawLines(glm::value_ptr(worldPoints[0]), (uint32_t)worldPoints.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
    }

    void EditorLayer::RenderLightGizmos(const glm::mat4& viewProjection) {
        // Mesmo helper de circulo usado por RenderSelectedColliderGizmo
        // (Sphere/Capsule) - reaproveitado aqui para a esfera de alcance
        // do Point e a base do cone do Spot. Duplicar em vez de extrair
        // para um lugar compartilhado por enquanto: as duas funcoes tem
        // necessidades ligeiramente diferentes (aqui tambem precisamos de
        // "leques" saindo do apice do cone, que o collider nao usa) e o
        // arquivo ja segue esse padrao de helpers locais por funcao (ver
        // appendCircle/appendArc acima) - extrair um Gizmos.h so vale a
        // pena se um terceiro gizmo precisar dos mesmos helpers.
        auto appendCircle = [](std::vector<glm::vec3>& points, glm::vec3 center, glm::vec3 axisU, glm::vec3 axisV, float radius, int segments) {
            glm::vec3 prev;
            for (int i = 0; i <= segments; i++) {
                float t = (float)i / (float)segments * 2.0f * 3.14159265f;
                glm::vec3 p = center + radius * (axisU * cosf(t) + axisV * sinf(t));
                if (i > 0) { points.push_back(prev); points.push_back(p); }
                prev = p;
            }
            };

        constexpr int kCircleSegments = 24;
        constexpr int kConeRaySegments = 8; // quantas linhas do apice ate a borda do cone (leque)

        auto view = m_ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::LightComponent>();
        for (auto entityHandle : view) {
            auto& light = view.get<Prism::LightComponent>(entityHandle);
            Prism::Entity entity(entityHandle, m_ActiveScene.get());

            // Mesma extracao de posicao+rotacao SEM escala que
            // RenderSelectedColliderGizmo usa (ver comentario grande la) -
            // pelo mesmo motivo: Range/SpotAngle sao medidas absolutas
            // (metros/graus), independentes da Scale da entidade. Uma luz
            // dentro de um objeto escalado nao deve ter seu gizmo (nem o
            // calculo de iluminacao real, ver Renderer::CollectGPULights)
            // deformado por essa escala.
            glm::mat4 worldMatrixWithScale = m_ActiveScene->GetWorldTransform(entity);
            glm::vec3 worldPosition = glm::vec3(worldMatrixWithScale[3]);
            glm::vec3 col0 = glm::vec3(worldMatrixWithScale[0]);
            glm::vec3 col1 = glm::vec3(worldMatrixWithScale[1]);
            glm::vec3 col2 = glm::vec3(worldMatrixWithScale[2]);
            glm::mat3 worldRotationOnly(
                glm::length(col0) > 0.00001f ? col0 / glm::length(col0) : glm::vec3(1, 0, 0),
                glm::length(col1) > 0.00001f ? col1 / glm::length(col1) : glm::vec3(0, 1, 0),
                glm::length(col2) > 0.00001f ? col2 / glm::length(col2) : glm::vec3(0, 0, 1)
            );
            auto toWorld = [&](const glm::vec3& local) {
                return worldPosition + worldRotationOnly * local;
                };

            // "Frente" da luz em espaco local - mesma convencao -Z usada
            // por CameraComponent (ver RenderCameraGizmos/RenderScene) e
            // por Renderer::CollectGPULights (Renderer.cpp), para o gizmo
            // sempre apontar exatamente para onde a luz de fato ilumina.
            glm::vec3 forward = worldRotationOnly * glm::vec3(0.0f, 0.0f, -1.0f);

            // Cor da propria luz (ver LightComponent::Color) - assim o
            // gizmo ja da uma pista visual de qual luz e qual sem precisar
            // selecionar cada uma. A entidade selecionada fica em branco
            // (sobrescrevendo a cor) para se destacar de forma inequivoca
            // mesmo quando a cor da luz e escura ou parecida com outras.
            bool isSelected = (m_SelectedEntity == entity);
            glm::vec3 color = isSelected ? glm::vec3(1.0f, 1.0f, 1.0f) : light.Color;

            std::vector<glm::vec3> localPoints;

            switch (light.Type) {
            case Prism::LightType::Point: {
                // Omnilight: esfera de raio Range, mesma tecnica de 3
                // circulos ortogonais que RenderSelectedColliderGizmo
                // usa para Sphere - da a nocao de volume sem exigir
                // uma malha completa.
                appendCircle(localPoints, glm::vec3(0.0f), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), light.Range, kCircleSegments); // plano YZ
                appendCircle(localPoints, glm::vec3(0.0f), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), light.Range, kCircleSegments); // plano XZ
                appendCircle(localPoints, glm::vec3(0.0f), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), light.Range, kCircleSegments); // plano XY
                break;
            }
            case Prism::LightType::Spot: {
                // Cone: apice na origem (posicao da luz), abrindo na
                // direcao -Z local ate uma base circular a distancia
                // Range, com raio determinado pelo angulo EXTERNO do
                // cone (SpotAngle - o angulo que efetivamente delimita
                // onde a luz chega a zero, ver LightComponent e o
                // shader em Renderer.cpp). InnerSpotAngle (soft edge)
                // nao ganha um circulo proprio aqui de proposito - um
                // segundo circulo concentrico so adicionaria ruido
                // visual sem ajudar a posicionar a luz, que e o
                // objetivo deste gizmo.
                float coneLength = light.Range;
                float baseRadius = coneLength * tanf(glm::radians(light.SpotAngle));
                glm::vec3 baseCenter(0.0f, 0.0f, -coneLength); // -Z local = "frente" (ver 'forward' acima)

                appendCircle(localPoints, baseCenter, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), baseRadius, kCircleSegments);

                // Leque de linhas do apice (origem) ate a borda do
                // circulo da base - poucas linhas (kConeRaySegments),
                // so para comunicar "isto e um cone solido", nao um
                // anel solto no ar.
                for (int i = 0; i < kConeRaySegments; i++) {
                    float t = (float)i / (float)kConeRaySegments * 2.0f * 3.14159265f;
                    glm::vec3 edge = baseCenter + baseRadius * (glm::vec3(1, 0, 0) * cosf(t) + glm::vec3(0, 1, 0) * sinf(t));
                    localPoints.push_back(glm::vec3(0.0f));
                    localPoints.push_back(edge);
                }
                break;
            }
            case Prism::LightType::Directional: {
                // Sem posicao nem alcance reais (ver LightComponent) -
                // o gizmo e so uma seta curta indicando a DIREcao que
                // a luz viaja, saindo da posicao da entidade (que e
                // arbitraria/so para posicionar o gizmo na viewport,
                // ja que Renderer::CollectGPULights ignora a posicao
                // deste tipo). Tamanho fixo (nao ha Range para
                // escalar) - grande o suficiente para ser visivel sem
                // depender do tamanho da cena.
                constexpr float kArrowLength = 1.5f;
                constexpr float kArrowHeadSize = 0.25f;
                glm::vec3 tip(0.0f, 0.0f, -kArrowLength);

                localPoints.push_back(glm::vec3(0.0f));
                localPoints.push_back(tip);

                // Cabeca da seta: 4 linhas curtas da ponta "voltando"
                // em diagonal - leitura clara de qual ponta e a ponta
                // sem precisar de um cone/malha completa.
                glm::vec3 back = tip + glm::vec3(0, 0, kArrowHeadSize);
                glm::vec3 heads[4] = {
                    back + glm::vec3(kArrowHeadSize, 0, 0), back + glm::vec3(-kArrowHeadSize, 0, 0),
                    back + glm::vec3(0, kArrowHeadSize, 0), back + glm::vec3(0, -kArrowHeadSize, 0),
                };
                for (auto& h : heads) { localPoints.push_back(tip); localPoints.push_back(h); }
                break;
            }
                                              // TODO(Area/IES): quando LightType ganhar esses valores
                                              // (ver Components.h), adicionar o wireframe correspondente
                                              // aqui - ex: Area desenharia um retangulo (Size.x/Size.y)
                                              // em vez de esfera/cone.
            }

            std::vector<glm::vec3> worldPoints;
            worldPoints.reserve(localPoints.size());
            for (auto& p : localPoints)
                worldPoints.push_back(toWorld(p));

            if (!worldPoints.empty())
                Prism::Renderer::DrawLines(glm::value_ptr(worldPoints[0]), (uint32_t)worldPoints.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
        }
    }

    void EditorLayer::SetPrimaryCamera(Prism::Entity newPrimary) {
        auto view = m_ActiveScene->GetRegistry().view<Prism::CameraComponent>();
        for (auto entityHandle : view) {
            Prism::Entity entity(entityHandle, m_ActiveScene.get());
            entity.GetComponent<Prism::CameraComponent>().Primary = (entity == newPrimary);
        }
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
        if (!io.WantTextInput && !ImGui::IsPopupOpen(kSaveAsPopupId) && !ImGui::IsPopupOpen(kCreatePrefabPopupId) && !ImGui::IsPopupOpen(kSaveMaterialPopupId) && !ImGui::IsPopupOpen(kUnsavedChangesPopupId)) {
            bool ctrl = io.KeyCtrl;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_CommandHistory.Undo();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                m_CommandHistory.Redo();
            else if (ctrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveActiveSceneAs();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveActiveScene();
            // Ctrl+D duplica a entidade selecionada - mesma tecla que
            // Unity/Blender usam para "Duplicate". Delete/Backspace exclui -
            // convencao de Unity (Delete) e Blender (X/Delete); cobrimos os
            // dois para nao depender de um teclado especifico (alguns
            // notebooks nao tem uma tecla Delete dedicada facil de achar).
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && m_SelectedEntity)
                DuplicateEntity(m_SelectedEntity);
            else if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && m_SelectedEntity)
                DeleteEntity(m_SelectedEntity);
        }

        RenderMenuBar();
        RenderSaveAsPopup(); // popup modal - precisa ser chamado todo frame, mesmo fechado (ver comentario no metodo)
        RenderUnsavedChangesPopup(); // idem - "Salvar alteracoes?" ao fechar/trocar de mapa com alteracoes nao salvas
        RenderNewScriptPopup(); // idem - popup modal do botao "Novo..." do ScriptComponent
        RenderCreatePrefabPopup(); // idem - popup modal do item "Criar Prefab..." do menu de contexto
        RenderSaveMaterialPopup(); // idem - popup modal do botao "Salvar como Asset..." do painel Material

        ImGui::End();

        // Paineis - cada um e sua propria janela ImGui, e o dockspace acima
        // permite que o usuario os arraste/organize livremente.
        RenderViewportPanel();
        RenderHierarchyPanel();
        RenderPropertiesPanel();
        RenderConsolePanel();
        RenderContentBrowserPanel();
        RenderScriptEditorPanel();
        // REMOVIDO: RenderCameraPreviewPanel() - agora integrado ao painel de Propriedades
    }

    void EditorLayer::FlushRenderSettingsSave() {
        if (!m_RenderSettingsNeedSave)
            return;

        // Arrastando slider ou cor: o mouse esta apertado. Espera soltar.
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            return;

        // Ainda editando (teclado com repeticao, varios ajustes seguidos).
        if (ImGui::GetTime() - m_RenderSettingsLastEdit < kRenderSettingsSaveDelay)
            return;

        m_RenderSettingsNeedSave = false;
        if (!Prism::Project::SaveActive())
            PRISM_ERROR("Nao foi possivel gravar os ajustes de renderizacao no projeto.");
    }

    void EditorLayer::RenderRenderSettingsMenu() {
        // Sem projeto ativo nao ha onde guardar os valores: nao mostra o menu.
        auto project = Prism::Project::GetActive();
        if (!project)
            return;

        if (!ImGui::BeginMenu("Renderizacao"))
            return;

        Prism::RenderSettings& settings = project->GetRenderSettings();
        bool changed = false;

        // Largura fixa: sem ela, os widgets de um menu (janela que se
        // ajusta ao conteudo) saem com largura minima/instavel.
        ImGui::PushItemWidth(240.0f);

        ImGui::SeparatorText("Camera");
        changed |= ImGui::SliderFloat("Exposicao", &settings.Exposure, 0.1f, 8.0f, "%.2f",
            ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetItemTooltip("Brilho final da imagem, aplicado antes do tone mapping.\n1.0 = neutro; maior clareia, menor escurece.");

        ImGui::SeparatorText("Ambiente");
        changed |= ImGui::SliderFloat("Intensidade", &settings.Ambient, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetItemTooltip("Brilho medio da luz ambiente.\n0 = so as luzes iluminam (areas fora do alcance ficam escuras).");
        changed |= ImGui::ColorEdit3("Ceu", settings.EnvZenith);
        changed |= ImGui::ColorEdit3("Horizonte", settings.EnvHorizon);
        changed |= ImGui::ColorEdit3("Chao", settings.EnvGround);

        ImGui::Spacing();
        if (ImGui::Button("Restaurar padrao")) {
            settings = Prism::RenderSettings{};
            changed = true;
        }
        ImGui::TextDisabled("Salvo no projeto (.prismproj)");

        ImGui::PopItemWidth();

        if (changed) {
            // Aplica na hora (a viewport atualiza ao vivo); a gravacao no
            // arquivo fica para FlushRenderSettingsSave.
            Prism::Renderer::ApplyRenderSettings(settings);
            m_RenderSettingsNeedSave = true;
            m_RenderSettingsLastEdit = ImGui::GetTime();
        }

        ImGui::EndMenu();
    }

    void EditorLayer::RenderMenuBar() {
        FlushRenderSettingsSave();

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Arquivo")) {
                if (ImGui::MenuItem("Novo Mapa")) {
                    RunAfterUnsavedCheck([this]() { NewMap(); });
                }
                if (ImGui::MenuItem("Salvar Mapa", "Ctrl+S")) {
                    SaveActiveScene();
                }
                if (ImGui::MenuItem("Salvar Como...", "Ctrl+Shift+S")) {
                    SaveActiveSceneAs();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Fechar Projeto")) {
                    // TODO: voltar ao ProjectManagerLayer em vez de fechar
                    RunAfterUnsavedCheck([this]() {
                        m_AllowWindowClose = true;
                        Prism::Application::Get().Close();
                        });
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
                if (ImGui::MenuItem("Criar Camera")) {
                    // Preset de camera de jogo: so Transform + CameraComponent
                    // (nasce Primary=true - ver Components.h). Se ja existir
                    // outra Primary na cena, desmarcamos ela para manter a
                    // regra de "no maximo uma Primary" (mesmo ajuste feito
                    // em RenderAddComponentButton() para o botao Add Component).
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_ActiveScene, "Camera", "Camera",
                        [](Prism::Entity entity) { entity.AddComponent<Prism::CameraComponent>(); });
                    CreatePresetEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                    if (m_SelectedEntity)
                        SetPrimaryCamera(m_SelectedEntity); // garante unicidade - desmarca qualquer Primary anterior
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
                if (ImGui::MenuItem("Duplicar selecionada", "Ctrl+D", false, (bool)m_SelectedEntity))
                    DuplicateEntity(m_SelectedEntity);
                if (ImGui::MenuItem("Excluir selecionada", "Delete", false, (bool)m_SelectedEntity))
                    DeleteEntity(m_SelectedEntity);
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

            RenderRenderSettingsMenu();

            // Botao Play/Parar: abre/fecha a PlayWindow (janela SEPARADA
            // do SO, ver Play/PlayWindow.h) rodando uma copia clonada da
            // Scene ativa - scripts/fisica nunca tocam a Scene de edicao
            // (m_ActiveScene) diretamente. Alinhado a direita da menu bar
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

        // Posicao/tamanho REAIS (em pixels de tela do SO) de onde a imagem
        // acabou de ser desenhada - GetItemRectMin() pega isso do ULTIMO
        // item (o ImGui::Image logo acima), diferente de GetWindowPos()
        // (que da a posicao da JANELA inteira, incluindo a barra de
        // titulo "Viewport" no topo). Usar GetWindowPos() aqui foi o bug
        // original que deixava o retangulo do ImGuizmo desalinhado da
        // imagem por ~20-30px (a altura da barra de titulo) - o gizmo
        // aparecia desenhado no lugar certo (ele so precisa de
        // view/projection para isso), mas a area de CLIQUE/hover do
        // ImGuizmo usava esse retangulo errado, entao passar o mouse ou
        // clicar em cima dele nao registrava nada. Guardado aqui (em vez
        // de so dentro de RenderTransformGizmo) porque a toolbar abaixo
        // tambem precisa saber onde a imagem comeca.
        ImVec2 imageMin = ImGui::GetItemRectMin();

        // Soltar um .prismprefab do Content Browser sobre a Viewport
        // instancia ele na cena ativa - como RAIZ, na origem (0,0,0),
        // mesmo ponto de partida que todo preset do menu "Entidade" ja
        // usa (ver RenderMenuBar, "Criar Cubo" etc). Posicionar a
        // instancia exatamente sob o cursor (via raycast contra o plano
        // do chao ou a superficie sob o mouse) e um refinamento futuro -
        // por ora, arrastar e so uma forma rapida de trazer o prefab para
        // a cena, ajustar a posicao depois pelo gizmo/Properties panel
        // continua sendo o fluxo normal.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_PREFAB_PATH")) {
                std::string pathString((const char*)payload->Data, payload->DataSize - 1);
                InstantiatePrefab(pathString);
            }
            ImGui::EndDragDropTarget();
        }

        // Toolbar flutuante do gizmo (Translate/Rotate/Scale + Local/World)
        // - desenhada por CIMA do canto superior esquerdo da IMAGEM (nao
        // da janela) via SetCursorScreenPos, que usa coordenadas de tela
        // absolutas (mesmo espaco de GetItemRectMin() acima) - diferente
        // de SetCursorPos usado antes, que e relativo ao CONTEUDO da
        // janela e nao contava com a barra de titulo, entao a toolbar
        // ficava desenhada por cima/atras dela em vez de dentro da area da
        // imagem. So aparece com alguma entidade selecionada, ja que sem
        // selecao o gizmo em si nao e desenhado (ver RenderTransformGizmo).
        // Atalhos W/E/R fazem a mesma coisa que estes botoes - a toolbar
        // existe para quem prefere clicar, ou nao lembra dos atalhos.
        if (m_SelectedEntity) {
            ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
            ImGui::BeginGroup();
            if (ImGui::Button("Mover (W)")) m_GizmoOperation = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::Button("Rotacionar (E)")) m_GizmoOperation = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::Button("Escalar (R)")) m_GizmoOperation = ImGuizmo::SCALE;
            ImGui::SameLine();
            ImGui::TextUnformatted("|");
            ImGui::SameLine();
            if (ImGui::Button(m_GizmoMode == ImGuizmo::WORLD ? "Mundo" : "Local"))
                m_GizmoMode = (m_GizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            ImGui::EndGroup();
        }

        // ============================================================
        // Camera LIVRE do editor - estilo Godot
        // ============================================================
        // Camera com POSICAO livre, usando a mesma convencao de controles
        // do editor da Godot:
        //
        //   - Segurar o botao DIREITO do mouse sobre a viewport entra em
        //     "modo voar": o cursor e escondido e LOCKADO via
        //     GLFW_CURSOR_DISABLED (mesmo modo que jogos FPS usam para
        //     mouse look - o cursor nao sai da janela, entao o usuario
        //     nunca fica "travado na borda" no meio de um voo longo).
        //   - Enquanto voa:
        //       Mouse      = olhar (yaw/pitch)
        //       WASD       = mover no plano (W frente, S tras, A esq, D dir)
        //       Q/E        = descer/subir (eixo Y do MUNDO, absoluto)
        //       Shift      = 3x boost;  Alt = 3x slow (ajuste fino)
        //       Scroll     = ajusta a velocidade base de movimento
        //   - Soltar o RMB sai do modo voar; o cursor volta ao normal.
        //   - Fora do modo voar, scroll sobre a viewport faz DOLLY:
        //     avanca/recua a camera ao longo da direcao que ela olha,
        //     sem mudar a rotacao (mesma convencao da Godot para o
        //     scroll do editor).
        //
        // W/E/R SOZINHOS (sem RMB) continuam trocando a operacao do
        // gizmo (ver RenderTransformGizmo) - sem conflito, porque o
        // movimento exige o botao direito segurado (com o RMB segurado,
        // 'E' e "subir", nao "trocar para modo Rotate").
        GLFWwindow* nativeWindow = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();

        // --- Entrada no modo voar (RMB sobre a viewport) ---------------
        // So entra se o mouse estiver SOBRE a viewport no momento do
        // clique com RMB - mesmo comportamento da Godot (RMB em outro
        // painel faz o que aquele painel faz, nao entra em fly mode).
        if (!m_CameraLookActive && m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_CameraLookActive = true;
            m_CameraLookSkipNextDelta = true; // ver comentario no header
            glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }

        // --- Saida do modo voar (RMB solto) ----------------------------
        // Nao usa m_ViewportHovered aqui: uma vez comecado a voar, o
        // cursor virtual pode sair da area do painel e ainda queremos
        // continuar em fly mode ate o usuario soltar o botao.
        if (m_CameraLookActive && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            m_CameraLookActive = false;
            glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        // --- Enquanto voa: olhar + mover ------------------------------
        if (m_CameraLookActive) {
            ImGuiIO& io = ImGui::GetIO();

            // Ignora o delta do mouse no primeiro frame apos entrar em
            // fly mode: em algumas plataformas, o GLFW reseta a posicao
            // virtual do cursor ao trocar para GLFW_CURSOR_DISABLED, o
            // que geraria um "snap" grande de rotacao (delta enorme num
            // unico frame). Depois desse primeiro frame, io.MouseDelta
            // ja esta correto.
            if (m_CameraLookSkipNextDelta) {
                m_CameraLookSkipNextDelta = false;
            }
            else {
                m_CameraYaw += io.MouseDelta.x * 0.15f;

                // ============================================================
                // Sinal do eixo Y do mouse look
                // ============================================================
                // O ImGui reporta MouseDelta.y em coordenadas de TELA, que
                // crescem para BAIXO (mover o mouse fisicamente para baixo
                // da um delta.y POSITIVO). No 'forward' da camera (ver
                // ComputeEditorViewMatrix), pitch positivo olha para BAIXO
                // (-sin(pitch) no componente Y).
                //
                // A conta correta e: mouse para CIMA (delta.y negativo) ->
                // pitch DIMINUI (fica mais negativo) -> -sin(pitch) fica
                // POSITIVO -> camera olha para CIMA. Isso exige '+', nao
                // '-': quando delta.y e negativo, somar 'delta.y * k' a
                // m_CameraPitch DIMINUI o pitch (que e o que queremos).
                // Usar '-' inverteria o eixo vertical inteiro (mouse para
                // cima -> camera olha para baixo, e vice-versa).
                m_CameraPitch = std::clamp(m_CameraPitch + io.MouseDelta.y * 0.15f, -89.0f, 89.0f);
            }

            // Scroll ajusta a velocidade BASE de movimento (nao mais
            // "distancia de orbita" - a camera nao orbita mais nada).
            // Ajuste multiplicativo para ter um "feel" logaritmico: cada
            // tique do scroll multiplica a velocidade por um fator, entao
            // ajustar para "bem devagar" ou "bem rapido" funciona
            // igualmente bem, sem precisar de muitos cliques.
            if (io.MouseWheel != 0.0f)
                m_CameraMoveSpeed = std::clamp(m_CameraMoveSpeed * (1.0f + io.MouseWheel * 0.15f), 0.1f, 200.0f);

            // WASD/QE - speed base, com boost/slow ao estilo Godot: Shift
            // = 3x mais rapido, Alt = 3x mais lento (util para
            // posicionamento fino sem precisar baixar a velocidade base
            // no scroll).
            float speed = m_CameraMoveSpeed;
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift))
                speed *= 3.0f;
            if (ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt))
                speed *= 0.33f;

            // Mesma "frente" que ComputeEditorViewMatrix usa, para que
            // "andar para frente" (W) mova exatamente na direcao que a
            // camera esta olhando - sem isso, o movimento seria sempre no
            // plano horizontal com "frente" fixa (0,0,-1), independente
            // de onde o usuario esteja olhando.
            float yawRad = glm::radians(m_CameraYaw);
            float pitchRad = glm::radians(m_CameraPitch);
            glm::vec3 forward(
                -cosf(pitchRad) * cosf(yawRad),
                -sinf(pitchRad),
                -cosf(pitchRad) * sinf(yawRad)
            );
            // 'right' perpendicular a 'forward' NO PLANO HORIZONTAL -
            // cross com o "up" do mundo (e nao com o up local da camera),
            // o que da um "strafe" sempre paralelo ao chao (mesmo olhando
            // para cima/baixo). Ordem (forward x up) e a que da "direita"
            // no sistema destro do OpenGL (verificado: forward (0,0,-1) x
            // up (0,1,0) = (1,0,0) = +X, que e "direita" quando olhamos
            // para -Z, que e a convencao padrao de camera). Se a camera
            // estiver olhando praticamente reto para cima/baixo (forward
            // quase paralelo a up), o cross degenera - o normalize abaixo
            // cai para um "right" arbitrario nesse caso (mesmo com o
            // clamp de pitch em +/- 89, prefiro nao depender so disso
            // aqui).
            glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(right) > 0.0001f)
                right = glm::normalize(right);
            else
                right = glm::vec3(1.0f, 0.0f, 0.0f); // caso degenerado (olhando reto para cima/baixo) - right arbitrario, movimento horizontal continua funcional

            glm::vec3 moveDir(0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_W)) moveDir += forward;
            if (ImGui::IsKeyDown(ImGuiKey_S)) moveDir -= forward;
            if (ImGui::IsKeyDown(ImGuiKey_D)) moveDir += right;
            if (ImGui::IsKeyDown(ImGuiKey_A)) moveDir -= right;
            // Q/E sempre no eixo Y do MUNDO (nao no up local da camera) -
            // subir/descer deve ser sempre "para cima/para baixo" no
            // sentido absoluto, mesmo se a camera estiver de cabeca para
            // baixo.
            if (ImGui::IsKeyDown(ImGuiKey_E)) moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

            // Normaliza antes de multiplicar por 'speed': sem isso, W+D
            // moveria ~1.41x mais rapido que so W (diagonal mais longa
            // que o lado) - "diagonal strafe" seria mais rapido que
            // andar reto, o que e perceptivel e errado. Normalizando,
            // todas as direcoes tem a mesma velocidade.
            if (glm::length(moveDir) > 0.0001f) {
                // io.DeltaTime (do ImGui) - RenderViewportPanel nao recebe
                // 'deltaTime' do editor (so RenderScene recebe, vindo de
                // OnUpdate), mas o ImGui ja rastreia o delta por frame do
                // proprio NewFrame - usar isso e mais simples que passar
                // o deltaTime por toda a cadeia de chamadas de UI so para
                // este uso.
                m_CameraPosition += glm::normalize(moveDir) * speed * io.DeltaTime;
            }
        }
        else if (m_ViewportHovered && ImGui::GetIO().MouseWheel != 0.0f) {
            // --- Fora do modo voar: scroll faz DOLLY (zoom Godot-style)
            // Move a camera para frente/tras ao longo da direcao que ela
            // olha, sem mudar a rotacao - mesma convencao da Godot para
            // o scroll do editor (nao e "orbitar mais perto/longe", e
            // literalmente "andar um passo na direcao do olhar"). O
            // passo de 0.5 unidades por tique e confortavel em cenas de
            // escala "1 unidade = 1 metro".
            float yawRad = glm::radians(m_CameraYaw);
            float pitchRad = glm::radians(m_CameraPitch);
            glm::vec3 forward(
                -cosf(pitchRad) * cosf(yawRad),
                -sinf(pitchRad),
                -cosf(pitchRad) * sinf(yawRad)
            );
            m_CameraPosition += forward * ImGui::GetIO().MouseWheel * 0.5f;
        }

        // Recalcula as MESMAS matrizes view/projection que RenderScene() ja
        // montou este frame (baratas o bastante para nao valer a pena
        // cachear em membros so por isto) - tanto o picking abaixo quanto o
        // ImGuizmo (RenderTransformGizmo) precisam delas separadas (nao a
        // viewProjection combinada). ComputeEditorViewMatrix garante que
        // sao IDENTICAS as usadas por RenderScene (nao uma segunda copia
        // da formula que poderia dessincronizar).
        //
        // TODO O BLOCO DE PICKING/GIZMO E PULADO enquanto m_CameraLookActive
        // e true: com o cursor "disabled" pelo GLFW, a posicao reportada
        // e virtual (pode estar em qualquer lugar), entao clicar em LMB
        // durante um voo selecionaria uma entidade aleatoria - e o
        // ImGuizmo receberia um retangulo de hover baseado numa posicao
        // de cursor que o usuario nem ve. Melhor suspender tudo durante o
        // voo.
        if (!m_CameraLookActive) {
            float aspect = m_ViewportSize[1] > 0.0f ? m_ViewportSize[0] / m_ViewportSize[1] : 1.0f;
            glm::mat4 view = ComputeEditorViewMatrix();
            // Far de 1000 - tem que bater com o far de RenderScene(),
            // senao o raio de picking nao corresponderia exatamente ao que
            // a camera "ve" (na pratica o far de picking e so o limite
            // superior de profundidade que o raio de mundo pode atingir;
            // manter igual e so bom senso).
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);

            // Picking: clique ESQUERDO simples (sem arrastar - IsMouseClicked
            // dispara so no frame em que o botao desce) sobre a viewport
            // seleciona a entidade sob o cursor, igual Unity/Unreal/Godot.
            // Duas checagens evitam roubar o clique de outra coisa:
            //   - !ImGuizmo::IsOver(): um clique EM CIMA do gizmo de
            //     manipulacao (quando ha selecao) deve mover/rotacionar/
            //     escalar a entidade, nao trocar a selecao por baixo dele.
            //   - !ImGui::IsAnyItemHovered(): cobre a toolbar flutuante
            //     (Mover/Rotacionar/Escalar/Mundo, ver acima) desenhada por
            //     cima do canto da viewport - clicar nela nao deve
            //     "vazar" como picking na cena atras.
            if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGuizmo::IsOver() && !ImGui::IsAnyItemHovered()) {

                // Posicao do mouse RELATIVA a imagem da viewport (nao a
                // janela) e em [0,1] - mesmo espaco de imageMin/m_ViewportSize
                // usados por ImGuizmo::SetRect acima.
                ImVec2 mousePos = ImGui::GetMousePos();
                float mouseX = mousePos.x - imageMin.x;
                float mouseY = mousePos.y - imageMin.y;
                float ndcX = (mouseX / m_ViewportSize[0]) * 2.0f - 1.0f;
                // Y de tela cresce para BAIXO, NDC cresce para CIMA - inverte.
                float ndcY = 1.0f - (mouseY / m_ViewportSize[1]) * 2.0f;

                // Unprojection classica: leva dois pontos em clip space (no
                // near e no far plane, mesmo XY de NDC) de volta para
                // espaço de mundo via a inversa da view-projection - a reta
                // entre eles E o raio de mundo que passa pelo pixel
                // clicado. Mais simples e robusto que tentar reconstruir o
                // raio a partir so do FOV/aspect manualmente, e reaproveita
                // exatamente as mesmas view/projection que desenharam a
                // cena, entao nao pode dessincronizar delas.
                glm::mat4 invViewProjection = glm::inverse(projection * view);

                glm::vec4 nearPointClip(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 farPointClip(ndcX, ndcY, 1.0f, 1.0f);

                glm::vec4 nearPointWorld = invViewProjection * nearPointClip;
                glm::vec4 farPointWorld = invViewProjection * farPointClip;
                nearPointWorld /= nearPointWorld.w;
                farPointWorld /= farPointWorld.w;

                Prism::VisualRay ray;
                ray.Origin = glm::vec3(nearPointWorld);
                ray.Direction = glm::normalize(glm::vec3(farPointWorld - nearPointWorld));

                Prism::VisualRaycastHit hit = m_ActiveScene->VisualRaycast(ray);
                m_SelectedEntity = hit.Hit ? Prism::Entity(hit.Entity, m_ActiveScene.get())
                    : Prism::Entity{};
            }

            RenderTransformGizmo(view, projection, imageMin);
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::RenderRaycastGizmos(const glm::mat4& viewProjection) {
        auto view = m_ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::RaycastComponent>();
        for (auto entityHandle : view) {
            auto& raycast = view.get<Prism::RaycastComponent>(entityHandle);
            Prism::Entity entity(entityHandle, m_ActiveScene.get());

            // TargetPosition e um PONTO local (identico em espirito a
            // TransformComponent::Translation, NAO uma medida absoluta
            // como ColliderComponent::Size/LightComponent::Range) - por
            // isso usamos GetWorldTransform() COMPLETO (com Scale
            // inclusa), diferente de RenderSelectedColliderGizmo/
            // RenderLightGizmos acima, que extraem so posicao+rotacao de
            // proposito. Mesma matriz que Scene::UpdateRaycastComponents
            // usa para o teste fisico de verdade - o gizmo sempre bate
            // com o que o raio realmente testou.
            glm::mat4 world = m_ActiveScene->GetWorldTransform(entity);
            glm::vec3 worldOrigin = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            glm::vec3 worldTarget = glm::vec3(world * glm::vec4(raycast.TargetPosition, 1.0f));

            // Enquanto a Scene esta rodando E o raio acertou algo, desenha
            // so ate o ponto de impacto (nao ate TargetPosition) - deixa
            // claro visualmente ONDE o raio parou, igual o debug draw de
            // raycast de qualquer engine. Nos demais casos (nao rodando,
            // ou rodando mas sem acerto) desenha ate TargetPosition
            // inteiro - ver comentario no header sobre nao mostrar um
            // HitPoint congelado/desatualizado fora do modo Play.
            bool showingHit = m_ActiveScene->IsRunning() && raycast.Hit;
            glm::vec3 lineEnd = showingHit ? raycast.HitPoint : worldTarget;

            // Verde = acertou algo (mesma convencao universal de "hit" em
            // debug draw); cinza = sem acerto ou fora do modo Play (raio
            // "inativo"). Amarelo ja e usado pelo Collider (ver
            // RenderSelectedColliderGizmo) - evitado aqui para os dois
            // gizmos nunca se confundirem quando aparecem juntos na mesma
            // entidade (um RaycastComponent sensor de chao, por exemplo,
            // tipicamente vive numa entidade que TAMBEM tem Collider).
            glm::vec3 color = showingHit ? glm::vec3(0.25f, 0.9f, 0.35f) : glm::vec3(0.55f, 0.55f, 0.55f);

            std::vector<glm::vec3> points = { worldOrigin, lineEnd };
            Prism::Renderer::DrawLines(glm::value_ptr(points[0]), (uint32_t)points.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));

            // Uma pequena cruz no ponto de impacto (3 segmentos curtos
            // cruzando nos eixos) so quando ha um Hit de verdade - ajuda a
            // localizar o ponto exato sem precisar aproximar a camera,
            // mesmo padrao visual usado por editores para marcar um ponto
            // de impacto (diferente de um circulo/esfera, que exigiria
            // saber a normal para orientar).
            if (showingHit) {
                constexpr float kMarkerSize = 0.1f;
                std::vector<glm::vec3> marker = {
                    raycast.HitPoint - glm::vec3(kMarkerSize, 0, 0), raycast.HitPoint + glm::vec3(kMarkerSize, 0, 0),
                    raycast.HitPoint - glm::vec3(0, kMarkerSize, 0), raycast.HitPoint + glm::vec3(0, kMarkerSize, 0),
                    raycast.HitPoint - glm::vec3(0, 0, kMarkerSize), raycast.HitPoint + glm::vec3(0, 0, kMarkerSize),
                };
                Prism::Renderer::DrawLines(glm::value_ptr(marker[0]), (uint32_t)marker.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
            }
        }
    }

    void EditorLayer::RenderTransformGizmo(const glm::mat4& view, const glm::mat4& projection, const ImVec2& imageScreenPos) {
        if (!m_SelectedEntity)
            return;
        // So faz sentido manipular Transform de entidades que tem uma (toda
        // entidade tem, ver Scene::CreateEntity, mas a checagem custa nada
        // e protege contra qualquer excecao futura).
        if (!m_SelectedEntity.HasComponent<Prism::TransformComponent>())
            return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        // ImGuizmo desenha por cima da JANELA IMGUI ATUAL ("Viewport", ja
        // que RenderTransformGizmo e chamado de dentro de
        // RenderViewportPanel antes do ImGui::End()) - SetRect define a
        // regiao de tela (em pixels, espaco de janela do SO) onde a IMAGEM
        // foi desenhada (imageScreenPos, capturado via GetItemRectMin()
        // logo apos o ImGui::Image em RenderViewportPanel - NAO
        // GetWindowPos(), que da a janela inteira incluindo a barra de
        // titulo e desalinhava a area de clique do gizmo da imagem por
        // conta da altura dessa barra - bug corrigido).
        ImGuizmo::SetRect(imageScreenPos.x, imageScreenPos.y, m_ViewportSize[0], m_ViewportSize[1]);

        // Atalhos de teclado (W/E/R) - so quando a viewport esta em foco E
        // o ImGuizmo nao esta sendo arrastado no momento (nao faz sentido
        // trocar de operacao no meio de um gesto). Mesma convencao de
        // Unity/Unreal/Godot. IsAnyItemActive cobre o caso de estar
        // digitando texto em outro painel (ex: campo "Nome") - nao rouba a
        // tecla 'e' de dentro de um InputText nesse caso.
        //
        // !IsMouseDown(Right): com o RMB segurado, W/E/R pertencem a
        // CAMERA do editor (ver RenderViewportPanel) - 'E' e "subir", nao
        // "trocar o gizmo para Rotate". Sem essa checagem, um voo com E
        // pressionado trocaria a operacao do gizmo silenciosamente.
        // (Note que este bloco so roda quando !m_CameraLookActive, entao
        // na pratica o RMB ja esta solto aqui - a checagem extra e
        // defensiva, caso alguem chame RenderTransformGizmo de outro
        // contexto no futuro.)
        if (m_ViewportFocused && !ImGuizmo::IsUsing() && !ImGui::IsAnyItemActive() &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoOperation = ImGuizmo::SCALE;
        }

        // A entidade pode ter um pai (RelationshipComponent) - o gizmo
        // sempre opera em espaco de MUNDO (para o usuario, arrastar "para
        // a direita" deve sempre significar direita do mundo, nao do pai),
        // entao passamos a matriz de MUNDO para o Manipulate() e, se o
        // gesto mudou algo, convertemos o resultado de volta para o espaco
        // LOCAL do pai antes de escrever em TransformComponent (que e
        // sempre local - ver comentario em Scene::GetWorldTransform).
        glm::mat4 worldMatrix = m_ActiveScene->GetWorldTransform(m_SelectedEntity);

        bool snap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        float snapValues[3] = { 0.0f, 0.0f, 0.0f };
        if (snap) {
            // Passos de snap convencionais: 1 unidade para posicao/escala,
            // 15 graus para rotacao - mesmos defaults que Unity usa com
            // Ctrl segurado.
            float step = (m_GizmoOperation == ImGuizmo::ROTATE) ? 15.0f : 1.0f;
            snapValues[0] = snapValues[1] = snapValues[2] = step;
        }

        // m_GizmoOperation/m_GizmoMode ja sao dos tipos ImGuizmo::OPERATION
        // / ImGuizmo::MODE (ver EditorLayer.h) - NAO sao mais int com um
        // cast "por fora". Era exatamente esse cast de um int=0 assumindo
        // TRANSLATE que fazia o gizmo nao aparecer por padrao ao
        // selecionar uma entidade (dependendo do fork do ImGuizmo, 0 nao
        // e necessariamente TRANSLATE, entao Manipulate() recebia uma
        // operacao invalida e nao desenhava nada).
        ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(projection),
            m_GizmoOperation, m_GizmoMode,
            glm::value_ptr(worldMatrix), nullptr,
            snap ? snapValues : nullptr);

        bool isUsing = ImGuizmo::IsUsing();

        // Captura o estado "antes" no exato frame em que o arraste COMECA
        // - mesmo padrao de m_TransformBeforeEdit para os DragFloat3 da
        // Properties panel (ver comentario no header).
        if (isUsing && !m_GizmoWasUsingLastFrame)
            m_GizmoTransformBeforeEdit = m_SelectedEntity.GetComponent<Prism::TransformComponent>();

        if (isUsing) {
            glm::mat4 localMatrix = worldMatrix;

            // Se a entidade tem pai, o TransformComponent e relativo a ELE
            // - multiplicamos pela inversa da matriz de mundo do PAI para
            // voltar ao espaco local.
            Prism::Entity parent;
            if (auto* rel = m_ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(m_SelectedEntity.GetHandle())) {
                if (rel->Parent != entt::null)
                    parent = Prism::Entity(rel->Parent, m_ActiveScene.get());
            }
            if (parent) {
                glm::mat4 parentWorld = m_ActiveScene->GetWorldTransform(parent);
                localMatrix = glm::inverse(parentWorld) * worldMatrix;
            }

            glm::vec3 translation, scale, skew;
            glm::vec4 perspective;
            glm::quat rotationQuat;
            if (glm::decompose(localMatrix, scale, rotationQuat, translation, skew, perspective)) {
                auto& transform = m_SelectedEntity.GetComponent<Prism::TransformComponent>();
                transform.Translation = translation;
                transform.Scale = scale;
                // glm::decompose retorna um quaternion; a engine guarda
                // rotacao em euler-graus (ver TransformComponent,
                // Components.h) - glm::eulerAngles devolve radianos na
                // ordem (pitch=x, yaw=y, roll=z), que e exatamente o que
                // TransformComponent::GetTransform() espera de volta via
                // yawPitchRoll (ver Components.h). Sem isso o objeto
                // "pularia" de rotacao toda vez que o gizmo fosse usado.
                glm::vec3 eulerRad = glm::eulerAngles(rotationQuat);
                transform.Rotation = glm::degrees(eulerRad);
            }
        }
        else if (m_GizmoWasUsingLastFrame) {
            // Arraste acabou de terminar neste frame (IsUsing() era true no
            // frame anterior, false agora) - empurra UM TransformCommand
            // cobrindo o gesto inteiro, mesmo padrao de
            // IsItemDeactivatedAfterEdit() nos DragFloat3 da Properties
            // panel. So gera comando se algo de fato mudou (evita entulhar
            // o historico de undo com um clique que nao moveu nada).
            const auto& after = m_SelectedEntity.GetComponent<Prism::TransformComponent>();
            bool changed = m_GizmoTransformBeforeEdit.Translation != after.Translation
                || m_GizmoTransformBeforeEdit.Rotation != after.Rotation
                || m_GizmoTransformBeforeEdit.Scale != after.Scale;
            if (changed) {
                m_CommandHistory.Execute(Prism::CreateScope<TransformCommand>(
                    m_SelectedEntity, m_GizmoTransformBeforeEdit, after));
            }
        }

        m_GizmoWasUsingLastFrame = isUsing;
    }

    void EditorLayer::RenderHierarchyPanel() {
        ImGui::Begin("Hierarquia");

        // So desenha as RAIZES (entidades sem pai) - cada uma desenha seus
        // proprios filhos recursivamente por dentro de RenderHierarchyNode.
        // Isso substitui a lista plana antiga por uma arvore de verdade
        // (ver RelationshipComponent em Components.h).
        m_ActiveScene->ForEachRootEntity([&](entt::entity handle, Prism::TagComponent&) {
            RenderHierarchyNode(Prism::Entity(handle, m_ActiveScene.get()));
            });

        // Area vazia do painel tambem e um alvo de drop valido - soltar
        // uma entidade aqui a torna raiz de novo (reparentar para
        // "nenhum pai"). Precisa vir DEPOIS do loop acima para cobrir o
        // espaco em branco abaixo da arvore inteira.
        ImGui::Dummy(ImGui::GetContentRegionAvail());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRISM_ENTITY_HANDLE")) {
                entt::entity draggedHandle = *(const entt::entity*)payload->Data;
                Prism::Entity dragged(draggedHandle, m_ActiveScene.get());
                if (dragged) {
                    Prism::Entity oldParent;
                    if (auto* rel = m_ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(draggedHandle)) {
                        if (rel->Parent != entt::null)
                            oldParent = Prism::Entity(rel->Parent, m_ActiveScene.get());
                    }
                    if (oldParent) // so gera comando se realmente tinha pai (senao ja era raiz, nada muda)
                        m_CommandHistory.Execute(Prism::CreateScope<SetParentCommand>(m_ActiveScene, dragged, oldParent, Prism::Entity{}));
                }
            }
            // Soltar um .prismprefab do Content Browser aqui instancia ele
            // como uma nova RAIZ da cena (mesmo comportamento de arrastar
            // um prefab para a arvore de cena na Unity/Godot) - ver
            // ContentBrowserPanel::RenderGrid, fonte deste payload.
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_PREFAB_PATH")) {
                std::string pathString((const char*)payload->Data, payload->DataSize - 1); // -1: string no payload inclui o terminador nulo
                InstantiatePrefab(pathString);
            }
            ImGui::EndDragDropTarget();
        }

        // Clicar em area vazia do painel desseleciona - convencao comum em
        // editores (Unity/Godot fazem o mesmo). IsAnyItemHovered ja e
        // false aqui porque o Dummy acima nao conta como "item" para fins
        // de clique (so como alvo de drag-and-drop).
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
            m_SelectedEntity = {};

        ImGui::End();
    }

    void EditorLayer::RenderHierarchyNode(Prism::Entity entity) {
        entt::entity handle = entity.GetHandle();
        auto& tag = entity.GetComponent<Prism::TagComponent>();
        bool isSelected = (m_SelectedEntity == entity);

        auto* rel = m_ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(handle);
        bool hasChildren = rel && !rel->Children.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (isSelected)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)handle, flags, "%s", tag.Tag.c_str());
        if (ImGui::IsItemClicked())
            m_SelectedEntity = entity;

        // Menu de contexto (botao direito) - BeginPopupContextItem reage ao
        // ULTIMO item desenhado (o TreeNodeEx acima), por isso vem logo
        // depois dele. Botao direito sobre um node TAMBEM seleciona a
        // entidade (convencao normal de qualquer editor: botao direito
        // implica "isto e sobre o que eu cliquei", nao so o esquerdo) -
        // sem isso, seria possivel abrir "Excluir" sobre uma entidade
        // diferente da que estava selecionada antes.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            m_SelectedEntity = entity;
        m_EntityContextMenu.OnImGuiRender(entity, (uint32_t)handle);

        // Origem do drag: qualquer node pode ser arrastado. So guardamos o
        // handle bruto (4 bytes) no payload - suficiente para reconstruir
        // uma Prism::Entity do lado de quem recebe (m_ActiveScene.get() e
        // sempre a mesma Scene).
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("PRISM_ENTITY_HANDLE", &handle, sizeof(entt::entity));
            ImGui::Text("%s", tag.Tag.c_str());
            ImGui::EndDragDropSource();
        }

        // Alvo do drag: soltar outra entidade sobre este node a torna
        // FILHA dele. Scene::SetParent ja recusa ciclos internamente (ver
        // Scene.cpp) - um IsAncestorOf() extra aqui so evita nem tentar
        // caso a UI queira, no futuro, desenhar feedback visual diferente
        // para um drop invalido; por ora deixamos SetParent decidir.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRISM_ENTITY_HANDLE")) {
                entt::entity draggedHandle = *(const entt::entity*)payload->Data;
                if (draggedHandle != handle) {
                    Prism::Entity dragged(draggedHandle, m_ActiveScene.get());
                    if (dragged) {
                        Prism::Entity oldParent;
                        if (auto* draggedRel = m_ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(draggedHandle)) {
                            if (draggedRel->Parent != entt::null)
                                oldParent = Prism::Entity(draggedRel->Parent, m_ActiveScene.get());
                        }
                        m_CommandHistory.Execute(Prism::CreateScope<SetParentCommand>(m_ActiveScene, dragged, oldParent, entity));
                    }
                }
            }
            // Soltar um .prismprefab EM CIMA de um node existente instancia
            // o prefab como FILHO dele (ver comentario em
            // EditorLayer::InstantiatePrefab sobre o parametro 'parent') -
            // mesmo payload que a area vazia da Hierarchy panel ja aceita
            // (ver RenderHierarchyPanel), so que aqui reparenta em seguida.
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_PREFAB_PATH")) {
                std::string pathString((const char*)payload->Data, payload->DataSize - 1);
                InstantiatePrefab(pathString, entity);
            }
            ImGui::EndDragDropTarget();
        }

        if (open && hasChildren) {
            // Copia a lista de filhos antes de iterar: um reparentamento
            // (drag-and-drop) disparado enquanto desenhamos esta arvore
            // poderia mudar RelationshipComponent::Children no meio do
            // loop - iterar a copia evita invalidar o iterator/o proprio
            // vector sendo lido.
            std::vector<entt::entity> childrenCopy = rel->Children;
            for (entt::entity childHandle : childrenCopy) {
                if (m_ActiveScene->GetRegistry().valid(childHandle))
                    RenderHierarchyNode(Prism::Entity(childHandle, m_ActiveScene.get()));
            }
            ImGui::TreePop();
        }
        else if (open && !hasChildren) {
            // Leaf com NoTreePushOnOpen nao empurra um nivel de arvore -
            // nada a fazer aqui, mas o 'open' de um Leaf via
            // NoTreePushOnOpen sempre vem true quando clicado (nao abre
            // nada de fato); sem TreePop correspondente porque
            // NoTreePushOnOpen nunca empurrou um.
        }
    }

    void EditorLayer::DuplicateEntity(Prism::Entity entity) {
        if (!entity)
            return;
        auto command = Prism::CreateScope<DuplicateEntityCommand>(m_ActiveScene, entity);
        DuplicateEntityCommand* raw = command.get();
        m_CommandHistory.Execute(std::move(command));
        m_SelectedEntity = raw->GetDuplicatedEntity();
    }

    void EditorLayer::DeleteEntity(Prism::Entity entity) {
        if (!entity)
            return;
        m_CommandHistory.Execute(Prism::CreateScope<DeleteEntityCommand>(m_ActiveScene, entity));
        if (m_SelectedEntity == entity)
            m_SelectedEntity = {};
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
                const char* meshNames[] = { "Cubo", "Esfera", "Capsula", "Cilindro", "Plano" };
                int meshIndex = (int)meshRenderer.Mesh;
                if (ImGui::Combo("Mesh", &meshIndex, meshNames, IM_ARRAYSIZE(meshNames)))
                    meshRenderer.Mesh = (Prism::PrimitiveMesh)meshIndex;
                ImGui::TextDisabled("Primitiva embutida (sem importacao de assets ainda).");

                ImGui::ColorEdit3("Cor", glm::value_ptr(meshRenderer.Color));
                if (ImGui::IsItemActivated())
                    m_ColorBeforeEdit = meshRenderer.Color;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<MeshColorCommand>(m_SelectedEntity, m_ColorBeforeEdit, meshRenderer.Color));
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::MeshRendererComponent>>(m_SelectedEntity, "Mesh Renderer"));
        }

        if (m_SelectedEntity.HasComponent<Prism::MaterialComponent>()) {
            auto& material = m_SelectedEntity.GetComponent<Prism::MaterialComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Material", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                // --- Material Asset (.prismmat) - reutilizar entre entidades ---
                // "Salvar como Asset" grava os campos ATUAIS deste
                // MaterialComponent num arquivo .prismmat (ver
                // MaterialSerializer.h) dentro de Assets/Materials -
                // "Carregar de Asset" faz o inverso, sobrescrevendo os
                // campos deste MaterialComponent com os de um .prismmat
                // existente (via LoadMaterialAssetCommand, com undo - ver
                // EditorCommands.h). Arrastar um .prismmat do Content
                // Browser direto para este cabecalho faz o mesmo que
                // "Carregar de Asset" (ver drop target logo abaixo) - dois
                // jeitos de chegar no mesmo resultado, mesmo espirito dos
                // slots de textura abaixo (arrastar OU digitar o path).
                //
                // SEM VINCULO VIVO com o arquivo (ver comentario grande em
                // MaterialSerializer.h) - isto e uma copia pontual dos
                // campos, nao uma referencia compartilhada; editar o
                // .prismmat depois nao afeta entidades que ja carregaram
                // dele antes.
                if (ImGui::Button("Salvar como Asset...")) {
                    m_ShowSaveMaterialPopup = true;
                    std::string suggested = m_SelectedEntity.HasComponent<Prism::TagComponent>()
                        ? m_SelectedEntity.GetComponent<Prism::TagComponent>().Tag : std::string("Material");
                    strncpy(m_SaveMaterialNameBuffer, suggested.c_str(), sizeof(m_SaveMaterialNameBuffer) - 1);
                    m_SaveMaterialNameBuffer[sizeof(m_SaveMaterialNameBuffer) - 1] = '\0';
                }
                ImGui::SameLine();
                ImGui::TextDisabled("ou arraste um .prismmat aqui:");

                ImGui::Button("Carregar de Asset (arraste aqui)", ImVec2(-1, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MATERIAL_PATH")) {
                        std::string pathString((const char*)payload->Data, payload->DataSize - 1);
                        m_CommandHistory.Execute(Prism::CreateScope<LoadMaterialAssetCommand>(m_SelectedEntity, pathString));
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::Separator();

                // Cada slot de textura vira um "cartao": preview GRANDE
                // (96x96, arrastavel/solta-vel) a esquerda, nome do
                // arquivo + botoes (limpar/editar path manualmente) a
                // direita - layout em duas colunas fixas, nao um
                // InputText inteiro em cima do preview como na versao
                // anterior (mais dificil de escanear com 3 slots
                // seguidos). O path completo (nem sempre curto) fica so
                // no tooltip, nao ocupando espaco de tela permanente.
                //
                // 'isSRGB' passado para GetOrLoadTexture deve bater
                // EXATAMENTE com o que Renderer::DrawMesh usa para o
                // mesmo campo (Albedo=true, Normal/RoughnessMetallic=
                // false) - ver comentario grande em Texture.h sobre por
                // que isso importa para PBR correto.
                constexpr float previewSize = 96.0f;

                auto renderTextureSlot = [&](const char* label, const char* hint, std::string& path, bool isSRGB) {
                    ImGui::PushID(label);

                    Prism::Texture2D* texture = path.empty() ? nullptr : Prism::Renderer::GetOrLoadTexture(path, isSRGB);
                    bool hasValidTexture = texture && texture->IsValid();
                    bool hasBrokenPath = !path.empty() && !hasValidTexture;

                    // --- Preview / drop target (coluna esquerda) --------
                    ImGui::BeginGroup();
                    if (hasValidTexture) {
                        ImGui::Image((ImTextureID)(uintptr_t)texture->GetRendererID(), ImVec2(previewSize, previewSize));
                    }
                    else {
                        // Sem textura (ou path quebrado): um botao vazio
                        // do mesmo tamanho do preview, so para servir de
                        // area de drop e dar feedback visual claro de
                        // "solte uma imagem aqui" - cor vermelha se o
                        // path atual esta quebrado, cinza neutro se
                        // realmente vazio.
                        ImVec4 emptyColor = hasBrokenPath ? ImVec4(0.35f, 0.18f, 0.18f, 1.0f) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, emptyColor);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, emptyColor);
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, emptyColor);
                        ImGui::Button(hasBrokenPath ? "Path\nquebrado" : "Arraste uma\nimagem aqui", ImVec2(previewSize, previewSize));
                        ImGui::PopStyleColor(3);
                    }

                    // Drop target: aceita CONTENT_BROWSER_IMAGE_PATH (ver
                    // ContentBrowserPanel::RenderGrid) sobre o preview
                    // INTEIRO (funciona igual solte numa textura ja
                    // carregada ou no botao vazio acima).
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_IMAGE_PATH")) {
                            std::filesystem::path droppedPath((const char*)payload->Data);
                            // ContentBrowserPanel manda o path ABSOLUTO
                            // (ver comentario la) - convertemos de volta
                            // para relativo a pasta do projeto antes de
                            // salvar em MaterialComponent, mesma
                            // convencao que o campo de texto manual usa
                            // (ver comentario grande em Renderer.h sobre
                            // GetOrLoadTexture resolvendo o inverso).
                            if (auto project = Prism::Project::GetActive()) {
                                std::error_code ec;
                                auto relativePath = std::filesystem::relative(droppedPath, project->GetProjectDirectory(), ec);
                                path = !ec ? relativePath.generic_string() : droppedPath.generic_string();
                            }
                            else {
                                path = droppedPath.generic_string();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", path.empty() ? "Arraste uma imagem do painel Conteudo do Projeto, ou edite o caminho ao lado." : path.c_str());
                    ImGui::EndGroup();

                    // --- Nome + status + acoes (coluna direita) ---------
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(label);
                    ImGui::TextDisabled("%s", hint);

                    std::string fileName = path.empty() ? "(nenhuma)" : std::filesystem::path(path).filename().string();
                    ImGui::TextWrapped("%s", fileName.c_str());

                    if (hasValidTexture)
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%u x %u", texture->GetWidth(), texture->GetHeight());
                    else if (hasBrokenPath)
                        ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "Nao encontrada");

                    if (!path.empty() && ImGui::SmallButton("Limpar"))
                        path.clear();

                    // Edicao manual do path continua disponivel (colapsada
                    // atras de um CollapsingHeader pequeno) - drag & drop
                    // e o fluxo principal agora, mas digitar/colar ainda
                    // e util (ex: corrigir um path quebrado sem precisar
                    // achar o arquivo de novo no Content Browser).
                    if (ImGui::TreeNodeEx("Editar caminho manualmente", ImGuiTreeNodeFlags_None)) {
                        char buffer[256];
                        strncpy(buffer, path.c_str(), sizeof(buffer) - 1);
                        buffer[sizeof(buffer) - 1] = '\0';
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputText("##Path", buffer, sizeof(buffer)))
                            path = buffer;
                        ImGui::TreePop();
                    }
                    ImGui::EndGroup();

                    ImGui::PopID();
                    };

                renderTextureSlot("Albedo", "Cor base (RGB)", material.AlbedoPath, /*isSRGB*/ true);
                ImGui::ColorEdit3("Tint de Albedo", glm::value_ptr(material.AlbedoTint));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Multiplica a textura de Albedo (ou serve como cor solida, se nenhuma textura estiver configurada acima).");

                ImGui::Separator();
                renderTextureSlot("Normal Map", "Tangent-space", material.NormalPath, /*isSRGB*/ false);

                ImGui::Separator();
                renderTextureSlot("Roughness/Metallic", "G=roughness, B=metallic (glTF)", material.RoughnessMetallicPath, /*isSRGB*/ false);
                ImGui::SliderFloat("Roughness Factor", &material.RoughnessFactor, 0.0f, 1.0f);
                ImGui::SliderFloat("Metallic Factor", &material.MetallicFactor, 0.0f, 1.0f);
                ImGui::TextDisabled("(?) Roughness: 0 = espelhado, 1 = fosco. Metallic: 0 = plastico/madeira/pedra, 1 = metal. Com um mapa carregado, o fator multiplica o mapa (G = roughness, B = metallic).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::MaterialComponent>>(m_SelectedEntity, "Material"));
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

                if (light.Type == Prism::LightType::Spot) {
                    // Angulo externo primeiro: se o usuario reduzir o
                    // externo abaixo do interno atual, arrasta o interno
                    // junto (evita um estado "invalido" visualmente
                    // confuso, mesmo que Renderer::CollectGPULights ja
                    // clampe isso ao montar o GPULight).
                    if (ImGui::DragFloat("Angulo do Cone (Externo)", &light.SpotAngle, 0.5f, 1.0f, 90.0f)) {
                        if (light.InnerSpotAngle > light.SpotAngle)
                            light.InnerSpotAngle = light.SpotAngle;
                    }
                    ImGui::DragFloat("Angulo do Cone (Interno)", &light.InnerSpotAngle, 0.5f, 0.0f, light.SpotAngle);
                    ImGui::TextDisabled("(?) Entre os dois angulos a luz cai suavemente ate a borda.");
                }

                ImGui::Checkbox("Projetar Sombras", &light.CastShadows);
                if (light.Type != Prism::LightType::Directional && light.CastShadows && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Shadow mapping hoje so suporta luzes Directional - marcar aqui nao tem efeito visual para Point/Spot ainda.");
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
                ImGui::TextDisabled("Precisa de um Rigid Body para participar da fisica.");
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

                // So faz sentido fisicamente para Kinematic/Dynamic (Static
                // nunca gira de qualquer jeito, ja que nunca se move - ver
                // BodyType, Components.h) - mas nao ha necessidade de
                // desabilitar o checkbox para Static: um valor "true" nele
                // e simplesmente ignorado nesse caso (CreateBodyForEntity
                // ainda passa fixedRotation para o Jolt independente do
                // tipo, e um corpo Static ja tem rotacao fixa por natureza).
                ImGui::Checkbox("Rotacao Fixa (nao tomba/gira por fisica)", &rigidBody.FixedRotation);
                if (rigidBody.FixedRotation)
                    ImGui::TextDisabled("Corpo ainda translada normalmente - so a ROTACAO fica travada. Use para camera/player controlado por script.");

                ImGui::Separator();
                ImGui::TextDisabled("Material fisico");
                // Friction/Restitution valem para qualquer BodyType (uma
                // rampa Static com atrito baixo ainda afeta o que desliza
                // nela) - ver comentario em RigidBodyComponent, Components.h.
                ImGui::SliderFloat("Atrito", &rigidBody.Friction, 0.0f, 1.0f);
                ImGui::SliderFloat("Restituicao (quique)", &rigidBody.Restitution, 0.0f, 1.0f);

                // Damping so tem efeito em corpos Dynamic (o Jolt integra
                // isso a cada step da simulacao - Static/Kinematic nao sao
                // integrados de qualquer forma).
                ImGui::BeginDisabled(!dynamicOnly);
                ImGui::SliderFloat("Amortecimento Linear", &rigidBody.LinearDamping, 0.0f, 1.0f);
                ImGui::SliderFloat("Amortecimento Angular", &rigidBody.AngularDamping, 0.0f, 1.0f);
                ImGui::EndDisabled();
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::RigidBodyComponent>>(m_SelectedEntity, "Rigid Body"));
        }

        if (m_SelectedEntity.HasComponent<Prism::RaycastComponent>()) {
            auto& raycast = m_SelectedEntity.GetComponent<Prism::RaycastComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Raycast", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Ativo", &raycast.Enabled);

                // Mesmo padrao Godot: um PONTO local, nao um vetor
                // direcao + distancia separados (ver comentario grande em
                // RaycastComponent, Components.h). DragFloat3 comum, mesmo
                // widget usado por TransformComponent::Translation na
                // Properties panel - o usuario ja conhece essa UI.
                ImGui::DragFloat3("Alvo (espaco local)", glm::value_ptr(raycast.TargetPosition), 0.05f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Ponto ate onde o raio vai, em espaco LOCAL da entidade (gira/translada junto com ela).\nEx: (0,0,-3) = para frente, 3 unidades. (0,-2,0) = para baixo, 2 unidades (sensor de chao).");

                ImGui::Checkbox("Ignorar Pai/Irmas", &raycast.IgnoreParentAndSiblings);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Se marcado, o raio ignora a entidade PAI (se houver) e todas as entidades IRMAS (que compartilham o mesmo pai) - util para um sensor filho do corpo do personagem nao acertar o proprio corpo/outros colliders do mesmo personagem.");

                ImGui::Separator();
                ImGui::TextDisabled("Resultado (fisico - so atualiza durante o modo Play):");
                if (!m_ActiveScene->IsRunning()) {
                    ImGui::TextDisabled("(fora do modo Play - sem resultado ainda)");
                }
                else if (raycast.Hit) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Acertou algo");
                    std::string hitName = "(entidade invalida)";
                    if (m_ActiveScene->GetRegistry().valid(raycast.HitEntity)) {
                        Prism::Entity hitEntity(raycast.HitEntity, m_ActiveScene.get());
                        if (hitEntity.HasComponent<Prism::TagComponent>())
                            hitName = hitEntity.GetComponent<Prism::TagComponent>().Tag;
                    }
                    ImGui::Text("Entidade: %s", hitName.c_str());
                    ImGui::Text("Distancia: %.2f", raycast.HitDistance);
                    ImGui::Text("Ponto: (%.2f, %.2f, %.2f)", raycast.HitPoint.x, raycast.HitPoint.y, raycast.HitPoint.z);
                }
                else {
                    ImGui::TextDisabled("Sem acerto");
                }
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::RaycastComponent>>(m_SelectedEntity, "Raycast"));
        }

        // ==================== SEÇÃO CAMERA (CORRIGIDA) ====================
        if (m_SelectedEntity.HasComponent<Prism::CameraComponent>()) {
            auto& camera = m_SelectedEntity.GetComponent<Prism::CameraComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Camera", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* projectionNames[] = { "Perspectiva", "Ortografica" };
                int projectionIndex = (int)camera.ProjectionType;
                if (ImGui::Combo("Projecao", &projectionIndex, projectionNames, IM_ARRAYSIZE(projectionNames)))
                    camera.ProjectionType = (Prism::CameraProjectionType)projectionIndex;

                if (camera.ProjectionType == Prism::CameraProjectionType::Perspective)
                    ImGui::DragFloat("FOV", &camera.FOV, 0.5f, 1.0f, 179.0f);
                else
                    ImGui::DragFloat("Tamanho Ortografico", &camera.OrthoSize, 0.1f, 0.01f, 1000.0f);

                ImGui::DragFloat("Near Clip", &camera.NearClip, 0.01f, 0.001f, camera.FarClip - 0.01f);
                ImGui::DragFloat("Far Clip", &camera.FarClip, 1.0f, camera.NearClip + 0.01f, 100000.0f);

                bool isPrimary = camera.Primary;
                if (ImGui::Checkbox("Primary", &isPrimary)) {
                    if (isPrimary)
                        SetPrimaryCamera(m_SelectedEntity);
                    else
                        camera.Primary = false;
                }
                if (!isPrimary)
                    ImGui::TextDisabled("Nao e a camera principal - o modo Play/viewport nao vai usar esta.");

                // --- Pre-visualizacao da camera (integrada) ---
                ImGui::Separator();
                ImGui::TextDisabled("Pre-visualizacao");

                // Reserva uma area com altura fixa (ex: 200px) para o preview
                float previewHeight = 200.0f;
                float previewWidth = ImGui::GetContentRegionAvail().x;
                // Mantem proporção 16:9, mas respeita a largura
                float aspect = 16.0f / 9.0f;
                if (previewWidth / aspect < previewHeight)
                    previewHeight = previewWidth / aspect;
                else
                    previewWidth = previewHeight * aspect;
                previewWidth = std::max(1.0f, previewWidth);
                previewHeight = std::max(1.0f, previewHeight);

                // Cria um child para delimitar a area e evitar vazamento
                ImGui::BeginChild("CameraPreviewContainer", ImVec2(previewWidth, previewHeight), false);
                {
                    // Centraliza a imagem dentro do child
                    ImVec2 availChild = ImGui::GetContentRegionAvail();
                    float offsetX = (availChild.x - previewWidth) * 0.5f;
                    float offsetY = (availChild.y - previewHeight) * 0.5f;
                    if (offsetX > 0) ImGui::SetCursorPosX(offsetX);
                    if (offsetY > 0) ImGui::SetCursorPosY(offsetY);

                    uint32_t texID = RenderCameraPreview(m_SelectedEntity, previewWidth, previewHeight);
                    if (texID != 0) {
                        ImGui::Image((ImTextureID)(uintptr_t)texID, ImVec2(previewWidth, previewHeight),
                            ImVec2(0, 1), ImVec2(1, 0));
                    }
                    else {
                        ImGui::TextDisabled("Falha ao renderizar preview");
                    }
                }
                ImGui::EndChild();
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::CameraComponent>>(m_SelectedEntity, "Camera"));
        }
        // ==================== FIM SEÇÃO CAMERA ==========================

        if (m_SelectedEntity.HasComponent<Prism::ScriptComponent>()) {
            auto& script = m_SelectedEntity.GetComponent<Prism::ScriptComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Script", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                // Combo com os .lua ja existentes em Scripts/ - evita ter
                // que saber/digitar o nome de cor (ver ListProjectScripts).
                // "(nenhum)" e sempre a primeira opcao, para poder limpar
                // ScriptPath sem sair do combo.
                std::vector<std::string> availableScripts = ListProjectScripts();

                std::string previewLabel = script.ScriptPath.empty() ? "(nenhum)" : script.ScriptPath;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##ScriptSelect", previewLabel.c_str())) {
                    bool noneSelected = script.ScriptPath.empty();
                    if (ImGui::Selectable("(nenhum)", noneSelected))
                        script.ScriptPath.clear();

                    for (const auto& scriptFile : availableScripts) {
                        bool selected = (script.ScriptPath == scriptFile);
                        if (ImGui::Selectable(scriptFile.c_str(), selected))
                            script.ScriptPath = scriptFile;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }

                    if (availableScripts.empty())
                        ImGui::TextDisabled("Nenhum .lua em Scripts/ ainda.");

                    ImGui::EndCombo();
                }

                // "Novo..." abre o popup que cria o arquivo (ver
                // RenderNewScriptPopup/CreateNewScript) e ja atribui a esta
                // entidade. "Editar" so aparece com um script ja escolhido -
                // abre o ScriptEditorPanel nele (ver Panels/ScriptEditorPanel.h).
                if (ImGui::Button("Novo...")) {
                    m_NewScriptNameBuffer[0] = '\0';
                    m_ShowNewScriptPopup = true;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(script.ScriptPath.empty());
                if (ImGui::Button("Editar")) {
                    auto project = Prism::Project::GetActive();
                    if (project)
                        m_ScriptEditor.Open(project->GetScriptDirectory() / script.ScriptPath);
                }
                ImGui::EndDisabled();

                if (script.ScriptPath.empty()) {
                    ImGui::TextDisabled("Nenhum arquivo escolhido ainda.");
                }
                else if (m_PlayWindow.IsOpen()) {
                    // A PlayWindow roda uma COPIA clonada da Scene (ver
                    // Play/PlayWindow.h) - m_SelectedEntity pertence a
                    // Scene de EDICAO, uma entidade DIFERENTE (ainda que
                    // correspondente) da que esta rodando de verdade la
                    // dentro. Nao ha como "recarregar" um script individual
                    // remotamente na PlayWindow a partir daqui - o jeito de
                    // aplicar uma mudanca no arquivo .lua e Parar e apertar
                    // Play de novo (que clona a Scene do zero, incluindo o
                    // arquivo .lua atualizado do disco).
                    ImGui::TextDisabled("Play em andamento - Pare e aperte Play de novo para recarregar.");
                }
                else {
                    ImGui::TextDisabled("Aperte Play (menu bar) para rodar este script.");
                }
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ScriptComponent>>(m_SelectedEntity, "Script"));
        }

        ImGui::Dummy(ImVec2(0, 8));
        RenderAddComponentButton();

        ImGui::Separator();
        ImGui::TextDisabled("Camera do editor (livre)");
        ImGui::Text("Posicao: (%.2f, %.2f, %.2f)", m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z);
        ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_CameraYaw, m_CameraPitch);
        ImGui::Text("Velocidade: %.2f u/s", m_CameraMoveSpeed);
        if (m_CameraLookActive)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "MODO VOAR ATIVO (solte o RMB para sair)");
        else
            ImGui::TextDisabled("RMB: modo voar (WASD/QE, Shift=boost, Alt=slow, scroll=velocidade)");
        ImGui::TextDisabled("Fora do voo: scroll sobre a viewport = dolly (avanca/recua)");

        ImGui::End();
    }

    // Instancia o AddComponentCommand<T> certo a partir do DisplayName
    // registrado em ComponentRegistry (ver ComponentRegistration.cpp) e
    // executa via m_CommandHistory (com Undo/Redo).
    //
    // POR QUE ISTO NAO VIVE DENTRO DE ComponentRegistry (Prism::): porque
    // AddComponentCommand<T> e Command (Prism::Core::Command) sao dois
    // conceitos DIFERENTES - Command fica em Prism (o motor), mas
    // AddComponentCommand/RemoveComponentCommand especificamente ficam em
    // PrismEditor (EditorCommands.h), ja que "ter undo/redo ao adicionar
    // um component" e uma preocupacao do EDITOR, nao do motor em si (um
    // jogo em modo Runtime, sem editor, nunca precisaria disso). Colocar
    // esta comparacao de string aqui (em vez de um std::function dentro
    // de ComponentTypeInfo) evita que Prism::ComponentRegistry precise
    // #include EditorCommands.h - Prism nunca deveria depender de codigo
    // do Editor.
    //
    // Isto tambem e o UNICO lugar que ainda precisa saber, um por um,
    // quais Components existem - mas apenas para a etapa de "criar o
    // Command certo", nao mais para decidir SE o component deve aparecer
    // no menu ou como serializa-lo (isso already vem do registro). Um
    // Component novo que nao seja adicionado aqui simplesmente nao tera
    // Undo ao ser adicionado - ainda funciona (AddDefault do registro e
    // usado como fallback abaixo), so sem desfazer.
    void EditorLayer::AddComponentByRegistryName(const std::string& displayName) {
        Prism::Entity entity = m_SelectedEntity;

        if (displayName == "Mesh Renderer")
            m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::MeshRendererComponent>>(entity, displayName));
        else if (displayName == "Light")
            m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::LightComponent>>(entity, displayName));
        else if (displayName == "Collider")
            m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::ColliderComponent>>(entity, displayName));
        else if (displayName == "Rigid Body")
            m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::RigidBodyComponent>>(entity, displayName));
        else if (displayName == "Raycast")
            m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::RaycastComponent>>(entity, displayName));
        else if (displayName == "Script")
            m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::ScriptComponent>>(entity, displayName));
        else if (displayName == "Camera")
            m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::CameraComponent>>(entity, displayName));
        else if (displayName == "Material")
            m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::MaterialComponent>>(entity, displayName));
        else {
            // Fallback generico SEM Undo - usado so se um Component for
            // registrado em ComponentRegistration.cpp mas esquecido aqui
            // (ver comentario grande acima) - melhor funcionar sem
            // desfazer do que nao adicionar nada.
            PRISM_CORE_WARN("EditorLayer::AddComponentByRegistryName: '", displayName, "' nao tem um AddComponentCommand mapeado - adicionando sem Undo.");
            for (auto& info : Prism::ComponentRegistry::GetAll()) {
                if (info.DisplayName == displayName) {
                    info.AddDefault(entity);
                    break;
                }
            }
        }

        // OnAfterAddInEditor (ver ComponentRegistry.h) - cobre ajustes de
        // consistencia que dependem do RESTO da cena, como o caso de
        // CameraComponent::Primary (ver ComponentRegistration.cpp) - FORA
        // do historico de Undo.
        for (auto& info : Prism::ComponentRegistry::GetAll()) {
            if (info.DisplayName == displayName && info.OnAfterAddInEditor) {
                info.OnAfterAddInEditor(entity, *m_ActiveScene);
                break;
            }
        }
    }

    void EditorLayer::RenderAddComponentButton() {
        // Garante que o registro esta populado - ver comentario
        // equivalente em SceneSerializer::Serialize/Deserialize
        // (ComponentRegistry::RegisterAll e idempotente).
        Prism::ComponentRegistry::RegisterAll();
        const auto& registry = Prism::ComponentRegistry::GetAll();

        // So mostra o botao se sobrar pelo menos um component que a
        // entidade ainda nao tem - evita um popup vazio (a entidade ja
        // tem TransformComponent sempre, que nunca esta neste registro -
        // ver comentario em ComponentRegistration.cpp sobre o motivo).
        bool hasAnyMissing = false;
        for (auto& info : registry) {
            if (!info.Has(m_SelectedEntity)) {
                hasAnyMissing = true;
                break;
            }
        }

        if (!hasAnyMissing) {
            ImGui::TextDisabled("(todos os components ja adicionados)");
            return;
        }

        if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup")) {
            // Loop generico sobre TODO Component registrado (ver
            // ComponentRegistry.h/ComponentRegistration.cpp). O Command real (com Undo/Redo, ver
            // AddComponentCommand<T>/EditorCommands.h) ainda e criado por
            // TIPO (nao generico) via AddComponentByRegistryName abaixo,
            // ja que o registro em si (Prism::ComponentRegistry) nao
            // conhece EditorCommands (que vive em PrismEditor, nao em
            // Prism) - ver comentario la sobre essa separacao proposital.
            for (auto& info : registry) {
                if (!info.Has(m_SelectedEntity) && ImGui::MenuItem(info.DisplayName.c_str())) {
                    AddComponentByRegistryName(info.DisplayName);
                    ImGui::CloseCurrentPopup();
                }
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

    void EditorLayer::RenderScriptEditorPanel() {
        m_ScriptEditor.OnImGuiRender();
    }

}