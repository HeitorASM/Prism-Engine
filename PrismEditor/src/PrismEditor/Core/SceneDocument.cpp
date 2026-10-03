#include "SceneDocument.h"
#include "EntityOps.h"
#include "../Play/PlayWindow.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace PrismEditor {

    bool SceneDocument::LoadScene(const std::filesystem::path& mapPath) {
        // Se a PlayWindow estiver aberta, fecha ela ANTES de trocar de
        // mapa - ela roda uma COPIA clonada de EditorContext::ActiveScene (ver
        // Play/PlayWindow.h); nao faz sentido continuar simulando essa
        // copia depois que o mapa que a originou deixou de ser o ativo no
        // editor. EditorContext::ActiveScene em si NUNCA roda scripts/fisica, entao
        // trocar de mapa e uma operacao simples, sem nada para "desfazer".
        if (m_Ctx.Play->IsOpen())
            m_Ctx.Play->Close();

        Prism::SceneSerializer serializer(Prism::Scene::Create());
        if (!serializer.Deserialize(mapPath)) {
            PRISM_ERROR("Falha ao carregar o mapa: ", mapPath.string());
            return false;
        }

        // Grava qualquer edicao de material vinculado ainda pendente
        // ANTES de trocar EditorContext::ActiveScene - depois da troca,
        // MaterialLinkSync::m_DirtyEntity apontaria para uma entidade da Scene
        // ANTIGA (m_Entity::m_Scene e um ponteiro cru - ver Entity.h -
        // que fica dangling assim que o Ref<Scene> antigo e substituido e
        // sua ultima referencia se desfaz). MaterialLinkSync::FlushSave() logo
        // depois so limparia a pendencia com seguranca (HasComponent
        // checaria um ponteiro morto), sem nunca gravar a edicao.
        m_Ctx.MaterialLinks.FlushSave();

        m_Ctx.ActiveScene = serializer.GetScene();
        m_Ctx.CurrentMapPath = mapPath;
        m_Ctx.SelectedEntity = {};
        m_Ctx.MaterialLinks.ForgetPending(); // ver comentario acima - nunca aponta para a Scene recem-substituida
        m_Ctx.History.Clear();
        EntityOps::SyncAllPrefabInstances(m_Ctx);
        MarkSceneClean();
        return true;
    }

    void SceneDocument::NewMap() {
        // NewMap() em si descarta a cena sem perguntar - quem pergunta e
        // o CHAMADOR, via RunAfterUnsavedCheck (ver RenderMenuBar). Assim
        // chamadas internas/programaticas nao ficam presas num popup.
        if (m_Ctx.Play->IsOpen())
            m_Ctx.Play->Close(); // ver comentario identico em LoadScene()

        // Ver comentario identico em LoadScene() sobre por que isto vem
        // ANTES de substituir EditorContext::ActiveScene.
        m_Ctx.MaterialLinks.FlushSave();

        m_Ctx.ActiveScene = Prism::Scene::Create("Nova Cena");
        m_Ctx.CurrentMapPath.clear(); // sem arquivo associado ainda - "Salvar Mapa" vai se comportar como "Salvar Como"
        m_Ctx.SelectedEntity = {};
        m_Ctx.MaterialLinks.ForgetPending(); // ver comentario em LoadScene()
        m_Ctx.History.Clear();

        MarkSceneClean();

        PRISM_INFO("Novo mapa criado (ainda nao salvo).");
    }

    void SceneDocument::LoadOrCreateScene() {
        auto project = Prism::Project::GetActive();
        const auto& startMap = project->GetConfig().StartMap;

        // Historico de undo/redo e por definicao amarrado a UMA Scene em
        // memoria - trocar a Scene (carregando um mapa do disco) sem
        // limpar o historico deixaria Undo() tentando desfazer acoes sobre
        // entidades que nao existem mais na nova Scene. LoadScene() ja faz
        // isso; aqui so garantimos o mesmo comportamento no caminho de
        // "criar cena de exemplo" abaixo.
        m_Ctx.History.Clear();

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
        // disco - EditorContext::CurrentMapPath fica vazio ate la.
        //
        // Sem edicao de material pendente possivel aqui na pratica (este
        // e o PRIMEIRO EditorContext::ActiveScene da sessao - OnAttach chama
        // LoadOrCreateScene uma unica vez), mas Flush+zera mesmo assim,
        // pelo MESMO motivo de seguranca de LoadScene()/NewMap() (ver
        // comentario la sobre Entity::m_Scene ser um ponteiro cru) - caso
        // este metodo um dia passe a ser chamado de novo em outro ponto.
        m_Ctx.MaterialLinks.FlushSave();
        m_Ctx.ActiveScene = Prism::Scene::Create("Cena de exemplo");
        m_Ctx.CurrentMapPath.clear();
        m_Ctx.MaterialLinks.ForgetPending();

        Prism::Entity cube = m_Ctx.ActiveScene->CreateEntity("Cubo");
        cube.GetComponent<Prism::TransformComponent>().Translation = { -1.2f, 0.0f, 0.0f };
        cube.AddComponent<Prism::MeshRendererComponent>();

        Prism::Entity cube2 = m_Ctx.ActiveScene->CreateEntity("Cubo (filho conceitual)");
        auto& t2 = cube2.GetComponent<Prism::TransformComponent>();
        t2.Translation = { 1.4f, 0.3f, 0.0f };
        t2.Scale = { 0.6f, 0.6f, 0.6f };
        auto& mesh2 = cube2.AddComponent<Prism::MeshRendererComponent>();
        mesh2.Color = { 0.3f, 0.6f, 0.9f };

        m_Ctx.SelectedEntity = cube;
        MarkSceneClean();
    }

    // Helper interno (nao declarado no .h) - escreve EditorContext::ActiveScene em
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

    void SceneDocument::SaveActiveScene() {
        TrySaveActiveScene();
    }

    bool SceneDocument::TrySaveActiveScene() {
        if (m_Ctx.CurrentMapPath.empty()) {
            // Cena sem arquivo associado ainda (nova, ou criada por
            // NewMap()) - nao ha "onde" sobrescrever, entao pedimos um
            // nome, exatamente como Salvar Como faria. O save de verdade
            // so acontece num frame futuro (RenderSaveAsPopup), entao
            // aqui a resposta e "ainda nao salvou".
            SaveActiveSceneAs();
            return false;
        }

        if (!WriteSceneFile(m_Ctx.ActiveScene, m_Ctx.CurrentMapPath))
            return false;

        MarkSceneClean();
        return true;
    }

    bool SceneDocument::HasUnsavedChanges() const {
        if (!m_Ctx.ActiveScene)
            return false;

        Prism::SceneSerializer serializer(m_Ctx.ActiveScene);
        uint64_t current = serializer.ComputeFingerprint();

        // 0 = nao foi possivel calcular (ex: sem permissao na pasta
        // temporaria). Na duvida, assume que HA alteracoes: um aviso a mais
        // e inofensivo, ja um aviso a menos perde o trabalho do usuario.
        if (current == 0 || m_SavedSceneFingerprint == 0)
            return true;

        return current != m_SavedSceneFingerprint;
    }

    void SceneDocument::MarkSceneClean() {
        if (!m_Ctx.ActiveScene) {
            m_SavedSceneFingerprint = 0;
            return;
        }
        Prism::SceneSerializer serializer(m_Ctx.ActiveScene);
        m_SavedSceneFingerprint = serializer.ComputeFingerprint();
    }

    void SceneDocument::RunAfterUnsavedCheck(std::function<void()> action) {
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

    void SceneDocument::RenderUnsavedChangesPopup() {
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
            std::string mapName = m_Ctx.CurrentMapPath.empty()
                ? m_Ctx.ActiveScene->GetName() + " (ainda nao salvo)"
                : m_Ctx.CurrentMapPath.filename().string();

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

    void SceneDocument::SaveActiveSceneAs() {
        // So abre o popup - a escrita de fato acontece em
        // RenderSaveAsPopup() quando o usuario confirma o nome, porque
        // ImGui::OpenPopup precisa ser chamado durante o ciclo normal de
        // render (ver RenderDockspace(), que chama RenderSaveAsPopup() a
        // cada frame independente do popup estar aberto ou nao).
        std::string suggested = m_Ctx.ActiveScene->GetName();
        std::snprintf(m_SaveAsNameBuffer, sizeof(m_SaveAsNameBuffer), "%s", suggested.c_str());
        m_ShowSaveAsPopup = true;
    }

    void SceneDocument::RenderSaveAsPopup() {
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
                if (WriteSceneFile(m_Ctx.ActiveScene, previewPath)) {
                    m_Ctx.CurrentMapPath = previewPath;
                    m_Ctx.ActiveScene->SetName(name);

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

        // Botao X da janela (ou Alt+F4). O callback do GLFW roda no meio de
        // glfwPollEvents, cedo demais para abrir um popup ImGui - entao o
        // gancho so decide "deixa fechar?" e, se ha alteracoes, REGISTRA o
        // pedido (m_CloseWindowRequested) para OnImGuiRender abrir o popup
        // no frame seguinte, e recusa o fechamento por ora.
    bool SceneDocument::OnCloseRequested() {
        if (m_AllowWindowClose)
            return true;
        if (!HasUnsavedChanges())
            return true;
        m_CloseWindowRequested = true;
        return false;
    }

    void SceneDocument::SetAllowWindowClose(bool allow) {
        m_AllowWindowClose = allow;
    }

}
