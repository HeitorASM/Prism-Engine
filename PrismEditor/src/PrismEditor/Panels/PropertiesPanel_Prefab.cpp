// ============================================================================
// PropertiesPanel_Prefab.cpp
// Secao "Prefab" do painel Propriedades (divergencias, Aplicar, Reverter,
// Sincronizar). Em arquivo proprio so por tamanho (~220 linhas): continua
// sendo um metodo de PropertiesPanel.
// ============================================================================
#include "PropertiesPanel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "ContentBrowserPanel.h"
#include "ScriptEditorPanel.h"
#include "../Play/PlayWindow.h"
#include "../Commands/EditorCommands.h"
#include "../Core/EntityOps.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace PrismEditor {

    void PropertiesPanel::RenderPrefabInstanceSection() {
        // Acha a RAIZ da instancia a partir da entidade selecionada: se a
        // propria selecao e a raiz, usa ela; se for um FILHO (ou neto...)
        // de uma instancia, sobe pelos pais ate achar quem tem
        // PrefabInstanceRootComponent. Antes disto, a secao so aparecia
        // com a RAIZ selecionada - editar uma peca (filho) do prefab nao
        // mostrava divergencia nem o botao "Aplicar ao Prefab", entao a
        // edicao ficava so na cena e nunca chegava ao arquivo .prismprefab
        // (outras cenas instanciavam o prefab "default" de novo).
        Prism::Entity instanceRoot = m_Ctx.SelectedEntity;
        while (instanceRoot && !instanceRoot.HasComponent<Prism::PrefabInstanceRootComponent>())
            instanceRoot = instanceRoot.GetParent();
        if (!instanceRoot)
            return;

        auto& root = instanceRoot.GetComponent<Prism::PrefabInstanceRootComponent>();
        auto project = Prism::Project::GetActive();
        std::filesystem::path prefabPath = project ? project->GetAssetRegistry().AbsolutePath(root.SourceAsset) : std::filesystem::path{};
        bool linkBroken = prefabPath.empty();

        if (ImGui::CollapsingHeader("Prefab", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (linkBroken) {
                ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "[Vinculo quebrado]");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("O .prismprefab de origem nao foi encontrado (apagado ou movido sem o .meta - ver Assets/AssetRegistry.h).\nEsta instancia continua funcionando normalmente, mas nao pode mais ser sincronizada.");
            }
            else {
                // Cache por mtime (mesmo espirito do cache de Material -
                // ver MaterialLinkSync::m_FileTimes/ReconcileLinkedMaterial):
                // Diff() rele o .prismprefab inteiro numa Scene temporaria
                // e compara byte a byte (ver PrefabSyncer::
                // LoadPrefabForComparison), caro para chamar TODO FRAME
                // enquanto o painel fica aberto. So recalcula quando a
                // raiz selecionada muda OU o arquivo no disco mudou desde
                // o ultimo calculo - qualquer clique nos botoes abaixo
                // (Sync/Revert/Apply/Recriar) altera a instancia e/ou o
                // arquivo, entao invalidamos o cache no fim de cada um
                // deles tambem (ver comentario junto a cada botao).
                std::error_code fileTimeEc;
                std::filesystem::file_time_type currentFileTime = std::filesystem::last_write_time(prefabPath, fileTimeEc);
                bool cacheValid = !fileTimeEc
                    && m_PrefabDiffCachedRoot == instanceRoot
                    && m_PrefabDiffCachedFileTime == currentFileTime;

                if (!cacheValid) {
                    m_PrefabDiffCache = Prism::PrefabSyncer::Diff(instanceRoot, prefabPath);
                    m_PrefabDiffCachedRoot = instanceRoot;
                    if (!fileTimeEc)
                        m_PrefabDiffCachedFileTime = currentFileTime;
                }
                Prism::PrefabDiffResult& diff = m_PrefabDiffCache;

                if (!diff.Valid) {
                    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Nao foi possivel comparar com o prefab.");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", diff.ErrorMessage.c_str());
                }
                else {
                    int overrideCount = diff.OverrideCount();
                    ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "[Instancia de Prefab]");
                    if (instanceRoot != m_Ctx.SelectedEntity && instanceRoot.HasComponent<Prism::TagComponent>()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(raiz: %s)", instanceRoot.GetComponent<Prism::TagComponent>().Tag.c_str());
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Origem: %s", prefabPath.filename().string().c_str());

                    // Estrutura: compara quantas entidades a instancia tem
                    // (raiz + subarvore) com quantas o prefab tem no arquivo.
                    // Um filho ADICIONADO a instancia nao tem
                    // PrefabInstanceMemberComponent, entao o Diff o ignora
                    // (StructureMatches continua true) - por isso contamos
                    // aqui, para o usuario ver que ha estrutura nova a
                    // aplicar. "Sincronizar" nunca cria/remove entidades.
                    size_t instanceEntityCount = 0;
                    size_t instanceMemberCount = 0;
                    {
                        std::vector<Prism::Entity> stack{ instanceRoot };
                        while (!stack.empty()) {
                            Prism::Entity e = stack.back();
                            stack.pop_back();
                            instanceEntityCount++;
                            if (e.HasComponent<Prism::PrefabInstanceMemberComponent>())
                                instanceMemberCount++;
                            for (size_t ci = 0; ci < e.GetChildCount(); ci++) {
                                Prism::Entity child = e.GetChildAt(ci);
                                if (child)
                                    stack.push_back(child);
                            }
                        }
                    }
                    const bool hasNewEntities = instanceEntityCount > instanceMemberCount;

                    if (!diff.StructureMatches) {
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "A estrutura do prefab mudou (entidades adicionadas/removidas).");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Sincronizar atualiza os Components das entidades correspondentes, mas nao traz entidades novas nem remove as que sumiram do prefab.\nUse \"Recriar Instancia\" para trazer a estrutura NOVA do prefab para ca (perde os overrides desta instancia)\nou \"Aplicar Estrutura ao Prefab\" para gravar a estrutura DESTA instancia no arquivo (efeito oposto).");

                        ImGui::SameLine();
                        if (ImGui::SmallButton("Recriar Instancia")) {
                            // 'instanceRoot' e destruida dentro deste
                            // Execute() (ver RecreateInstanceCommand) - a
                            // entidade selecionada muda para a raiz NOVA e
                            // saimos da funcao imediatamente, antes que
                            // qualquer codigo abaixo (Sincronizar/
                            // Divergencias/etc) tente reusar 'instanceRoot'
                            // ou 'diff' (que referenciam a subarvore
                            // ANTIGA, agora destruida) neste mesmo frame.
                            auto command = Prism::CreateScope<RecreateInstanceCommand>(m_Ctx.ActiveScene, instanceRoot, prefabPath);
                            RecreateInstanceCommand* raw = command.get();
                            m_Ctx.History.Execute(std::move(command));
                            m_Ctx.SelectedEntity = raw->GetInstantiatedEntity();
                            return;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Destroi esta instancia e a recria do zero a partir do prefab ATUAL - a nova estrutura (filhos adicionados/removidos no arquivo) passa a valer aqui.\nPerde os overrides locais desta instancia (Transform/Components que voce mudou so nela). Posicao e pai na cena sao preservados. Ctrl+Z desfaz.");
                    }
                    if (hasNewEntities) {
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "%d entidade(s) nova(s) nesta instancia (nao estao no prefab).", (int)(instanceEntityCount - instanceMemberCount));
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Filhos adicionados a instancia ainda nao fazem parte do arquivo do prefab.\n\"Sincronizar\" nunca os grava - use \"Aplicar Estrutura ao Prefab\".");
                    }

                    if (overrideCount == 0)
                        ImGui::TextDisabled("Sem divergencias do prefab.");
                    else
                        ImGui::Text("%d component(s) divergem do prefab.", overrideCount);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("O Transform da RAIZ da instancia nunca entra nesta comparacao (reposicionar a instancia na cena e sempre livre).\nO Transform dos FILHOS entra: se voce mover/girar/escalar uma peca dentro da instancia, ele aparece em Divergencias e pode ser aplicado ao prefab.");

                    if (ImGui::Button("Sincronizar com o Prefab")) {
                        // SyncPrefabInstanceCommand ja usa PrefabSyncer::
                        // UpdateAll por baixo (preserva os overrides - ver
                        // comentario grande em PrefabSyncer.h) - o botao
                        // fica sempre habilitado (mesmo com
                        // overrideCount==0 ou StructureMatches==false)
                        // porque sincronizar "sem nada para atualizar" e
                        // inofensivo (UpdateAll so aplica RevertComponent
                        // aos NAO overridados, e se nao ha nenhum, o
                        // Command so nao atualiza nada) - simplifica a
                        // logica de habilitar/desabilitar em troca de um
                        // clique ocasionalmente redundante.
                        m_Ctx.History.Execute(Prism::CreateScope<SyncPrefabInstanceCommand>(instanceRoot, prefabPath));
                        m_PrefabDiffCachedRoot = {}; // instancia mudou - forca recalcular o Diff no proximo frame (ver cache acima)
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Atualiza todos os Components NAO divergentes desta instancia (e de seus filhos) com o valor atual do prefab.\nComponents que ja divergem (overridados) nao sao tocados.");

                    ImGui::SameLine();
                    if (ImGui::Button("Aplicar Estrutura ao Prefab")) {
                        m_Ctx.History.Execute(Prism::CreateScope<ApplyPrefabStructureCommand>(instanceRoot, prefabPath));
                        m_PrefabDiffCachedRoot = {}; // arquivo mudou - forca recalcular o Diff no proximo frame (ver cache acima)
                        if (project) {
                            project->GetAssetRegistry().Refresh();
                            m_Ctx.ContentBrowser->RefreshEntries();
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Regrava o arquivo do prefab com a estrutura DESTA instancia: filhos adicionados passam a existir no prefab, filhos removidos deixam de existir.\nO Transform da raiz (posicao na cena) nao e gravado.\nNao afeta outras instancias ja existentes em outras cenas (sincronize-as depois). Ctrl+Z desfaz.");

                    if (overrideCount > 0 && ImGui::TreeNodeEx("Divergencias", ImGuiTreeNodeFlags_None)) {
                        // 'd' NAO e const pelo mesmo motivo de
                        // PrefabSyncer::UpdateAll (ver comentario la):
                        // Entity::HasComponent/GetComponent nao sao
                        // const-qualificados, e "const PrefabComponentDiff&"
                        // tornaria d.InstanceEntity implicitamente const
                        // (MSVC C2662). So leitura aqui, seguro remover o const.
                        for (Prism::PrefabComponentDiff& d : diff.Components) {
                            if (!d.Overridden)
                                continue;

                            ImGui::PushID(&d);
                            std::string entityName = d.InstanceEntity.HasComponent<Prism::TagComponent>()
                                ? d.InstanceEntity.GetComponent<Prism::TagComponent>().Tag : std::string("Entidade");
                            ImGui::BulletText("%s - %s", entityName.c_str(), d.ComponentName.c_str());

                            ImGui::SameLine();
                            if (ImGui::SmallButton("Reverter")) {
                                uint32_t index = d.InstanceEntity.HasComponent<Prism::PrefabInstanceMemberComponent>()
                                    ? d.InstanceEntity.GetComponent<Prism::PrefabInstanceMemberComponent>().IndexInPrefab : 0;
                                m_Ctx.History.Execute(Prism::CreateScope<RevertPrefabComponentCommand>(d.InstanceEntity, prefabPath, index, d.ComponentName));
                                m_PrefabDiffCachedRoot = {}; // instancia mudou - forca recalcular o Diff no proximo frame (ver cache acima)
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Descarta o valor atual deste Component na instancia e usa o do prefab.");

                            // "Aplicar ao Prefab" so faz sentido quando a
                            // instancia TEM o Component (senao nao ha
                            // valor nenhum para subir ao arquivo - ver
                            // PrefabSyncer::ApplyComponentToPrefab, que ja
                            // trata o caso de remover do prefab, mas so
                            // exponho o botao quando ha algo concreto para
                            // "aplicar").
                            if (d.PresentInInstance) {
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Aplicar ao Prefab")) {
                                    m_Ctx.History.Execute(Prism::CreateScope<ApplyPrefabComponentCommand>(d.InstanceEntity, prefabPath, d.ComponentName));
                                    m_PrefabDiffCachedRoot = {}; // arquivo mudou - forca recalcular o Diff no proximo frame (ver cache acima)
                                    // O arquivo mudou - mesma necessidade de
                                    // reconciliar o AssetRegistry/Content
                                    // Browser que RenderSaveMaterialPopup ja
                                    // trata (ver comentario la) - aqui o
                                    // arquivo ja existia e so o CONTEUDO
                                    // mudou, entao nao ha entrada nova para
                                    // aparecer, mas mantemos a chamada por
                                    // clareza e para o dia em que o Content
                                    // Browser passar a mostrar mtime/preview.
                                    if (project) {
                                        project->GetAssetRegistry().Refresh();
                                        m_Ctx.ContentBrowser->RefreshEntries();
                                    }
                                }
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip("Grava o valor ATUAL deste Component (da instancia) de volta no arquivo do prefab.\nNao afeta outras instancias deste prefab automaticamente - sincronize-as depois. Ctrl+Z desfaz.");
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                }
            }
        }
    }

}
