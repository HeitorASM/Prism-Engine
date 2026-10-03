#include "HierarchyPanel.h"
#include "ContentBrowserPanel.h"
#include "../Core/EntityOps.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>
#include <cstring>
#include <string>
#include <vector>

namespace PrismEditor {

    HierarchyPanel::HierarchyPanel(EditorContext& context) : m_Ctx(context) {
        // Liga o menu de contexto de entidade (ver Panels/EntityContextMenuPanel.h)
        // as funcoes deste EditorLayer que de fato tem acesso a
        // CommandHistory/Scene/popups - o painel em si nao conhece nada
        // disso, so avisa QUAL acao foi escolhida e sobre QUAL entidade.
        m_EntityContextMenu.SetOnDuplicate([this](Prism::Entity entity) {
            EntityOps::DuplicateEntity(m_Ctx, entity);
            });
        m_EntityContextMenu.SetOnDelete([this](Prism::Entity entity) {
            EntityOps::DeleteEntity(m_Ctx, entity);
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
    }

    void HierarchyPanel::OnImGuiRender() {
        ImGui::Begin("Hierarquia");

        // So desenha as RAIZES (entidades sem pai) - cada uma desenha seus
        // proprios filhos recursivamente por dentro de RenderHierarchyNode.
        // Isso substitui a lista plana antiga por uma arvore de verdade
        // (ver RelationshipComponent em Components.h).
        m_Ctx.ActiveScene->ForEachRootEntity([&](entt::entity handle, Prism::TagComponent&) {
            RenderHierarchyNode(Prism::Entity(handle, m_Ctx.ActiveScene.get()));
            });

        // Area vazia do painel tambem e um alvo de drop valido - soltar
        // uma entidade aqui a torna raiz de novo (reparentar para
        // "nenhum pai"). Precisa vir DEPOIS do loop acima para cobrir o
        // espaco em branco abaixo da arvore inteira.
        ImGui::Dummy(ImGui::GetContentRegionAvail());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRISM_ENTITY_HANDLE")) {
                entt::entity draggedHandle = *(const entt::entity*)payload->Data;
                Prism::Entity dragged(draggedHandle, m_Ctx.ActiveScene.get());
                if (dragged) {
                    Prism::Entity oldParent;
                    if (auto* rel = m_Ctx.ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(draggedHandle)) {
                        if (rel->Parent != entt::null)
                            oldParent = Prism::Entity(rel->Parent, m_Ctx.ActiveScene.get());
                    }
                    if (oldParent) // so gera comando se realmente tinha pai (senao ja era raiz, nada muda)
                        m_Ctx.History.Execute(Prism::CreateScope<SetParentCommand>(m_Ctx.ActiveScene, dragged, oldParent, Prism::Entity{}));
                }
            }
            // Soltar um .prismprefab do Content Browser aqui instancia ele
            // como uma nova RAIZ da cena (mesmo comportamento de arrastar
            // um prefab para a arvore de cena na Unity/Godot) - ver
            // ContentBrowserPanel::RenderGrid, fonte deste payload.
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_PREFAB_PATH")) {
                std::string pathString((const char*)payload->Data, payload->DataSize - 1); // -1: string no payload inclui o terminador nulo
                EntityOps::InstantiatePrefab(m_Ctx, pathString);
            }
            ImGui::EndDragDropTarget();
        }

        // Clicar em area vazia do painel desseleciona - convencao comum em
        // editores (Unity/Godot fazem o mesmo). IsAnyItemHovered ja e
        // false aqui porque o Dummy acima nao conta como "item" para fins
        // de clique (so como alvo de drag-and-drop).
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
            m_Ctx.SelectedEntity = {};

        ImGui::End();
    }

    void HierarchyPanel::RenderHierarchyNode(Prism::Entity entity) {
        entt::entity handle = entity.GetHandle();
        auto& tag = entity.GetComponent<Prism::TagComponent>();
        bool isSelected = (m_Ctx.SelectedEntity == entity);

        auto* rel = m_Ctx.ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(handle);
        bool hasChildren = rel && !rel->Children.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (isSelected)
            flags |= ImGuiTreeNodeFlags_Selected;

        // Sinal visual de instancia de prefab (ver Components.h e
        // Scene/PrefabSyncer.h): so a RAIZ da subarvore tem
        // PrefabInstanceRootComponent, entao so ela ganha o prefixo "[P]"
        // colorido - filhos da instancia (que so tem
        // PrefabInstanceMemberComponent) aparecem normais, o mesmo
        // espirito de so a raiz de uma Scene salva ter um icone de
        // "arquivo" em outros editores. Cor por si so nao basta (daltonismo/
        // legenda), por isso o prefixo textual "[P]" tambem - mesma
        // filosofia do selo "[Vinculado]" do painel Material (ver
        // RenderPropertiesPanel, secao Material).
        bool isPrefabInstance = entity.HasComponent<Prism::PrefabInstanceRootComponent>();
        if (isPrefabInstance)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 0.95f, 1.0f)); // azul claro - vinculo vivo de PREFAB (distinto do dourado usado para material, ver RenderPropertiesPanel)

        bool open = isPrefabInstance
            ? ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)handle, flags, "[P] %s", tag.Tag.c_str())
            : ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)handle, flags, "%s", tag.Tag.c_str());

        if (isPrefabInstance) {
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Instancia de prefab - ver o painel Prefab nas Propriedades para sincronizar, aplicar ou reverter mudancas.");
        }

        if (ImGui::IsItemClicked())
            m_Ctx.SelectedEntity = entity;

        // Menu de contexto (botao direito) - BeginPopupContextItem reage ao
        // ULTIMO item desenhado (o TreeNodeEx acima), por isso vem logo
        // depois dele. Botao direito sobre um node TAMBEM seleciona a
        // entidade (convencao normal de qualquer editor: botao direito
        // implica "isto e sobre o que eu cliquei", nao so o esquerdo) -
        // sem isso, seria possivel abrir "Excluir" sobre uma entidade
        // diferente da que estava selecionada antes.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            m_Ctx.SelectedEntity = entity;
        m_EntityContextMenu.OnImGuiRender(entity, (uint32_t)handle);

        // Origem do drag: qualquer node pode ser arrastado. So guardamos o
        // handle bruto (4 bytes) no payload - suficiente para reconstruir
        // uma Prism::Entity do lado de quem recebe (EditorContext::ActiveScene.get() e
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
                    Prism::Entity dragged(draggedHandle, m_Ctx.ActiveScene.get());
                    if (dragged) {
                        Prism::Entity oldParent;
                        if (auto* draggedRel = m_Ctx.ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(draggedHandle)) {
                            if (draggedRel->Parent != entt::null)
                                oldParent = Prism::Entity(draggedRel->Parent, m_Ctx.ActiveScene.get());
                        }
                        m_Ctx.History.Execute(Prism::CreateScope<SetParentCommand>(m_Ctx.ActiveScene, dragged, oldParent, entity));
                    }
                }
            }
            // Soltar um .prismprefab EM CIMA de um node existente instancia
            // o prefab como FILHO dele (ver comentario em
            // EntityOps::InstantiatePrefab sobre o parametro 'parent') -
            // mesmo payload que a area vazia da Hierarchy panel ja aceita
            // (ver RenderHierarchyPanel), so que aqui reparenta em seguida.
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_PREFAB_PATH")) {
                std::string pathString((const char*)payload->Data, payload->DataSize - 1);
                EntityOps::InstantiatePrefab(m_Ctx, pathString, entity);
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
                if (m_Ctx.ActiveScene->GetRegistry().valid(childHandle))
                    RenderHierarchyNode(Prism::Entity(childHandle, m_Ctx.ActiveScene.get()));
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

    void HierarchyPanel::RenderCreatePrefabPopup() {
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
                else if (project) {
                    // Mesmo motivo do popup de salvar material (ver
                    // RenderSaveMaterialPopup): o arquivo recem-criado NAO tem
                    // AssetID enquanto o AssetRegistry nao varrer de novo.
                    // Sem isto, InstantiatePrefabCommand::ResolveAssetID
                    // devolve invalido e a instancia nasce SEM vinculo
                    // (sem PrefabInstanceRootComponent) - por isso o painel
                    // "Prefab" nunca aparecia e editar a instancia nunca
                    // chegava ao arquivo.
                    project->GetAssetRegistry().Refresh();
                    m_Ctx.ContentBrowser->RefreshEntries();
                }
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

}
