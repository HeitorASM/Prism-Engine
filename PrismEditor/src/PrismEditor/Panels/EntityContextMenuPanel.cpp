#include "EntityContextMenuPanel.h"
#include <imgui.h>
#include <cstdint>

namespace PrismEditor {

    void EntityContextMenuPanel::OnImGuiRender(Prism::Entity entity, uint32_t uniqueId) {
        // PushID(uniqueId) torna o ID do popup abaixo UNICO por entidade -
        // ver comentario grande no .h sobre por que isso e obrigatorio
        // (sem isso, todo node da Hierarchy compartilha o mesmo ID de
        // popup e o ImGui acusa "2 visible items with conflicting ID").
        ImGui::PushID((int)uniqueId);
        if (ImGui::BeginPopupContextItem("EntityContextMenu")) {
            // Cabecalho com o nome, so para deixar claro SOBRE QUAL
            // entidade o menu esta agindo (util quando o clique direito
            // muda a selecao para um node diferente do que estava
            // selecionado antes, ver EditorLayer::RenderHierarchyNode).
            if (entity.HasComponent<Prism::TagComponent>()) {
                ImGui::TextDisabled("%s", entity.GetComponent<Prism::TagComponent>().Tag.c_str());
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Duplicar", "Ctrl+D")) {
                if (m_OnDuplicate)
                    m_OnDuplicate(entity);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Criar Prefab...")) {
                if (m_OnCreatePrefabRequested)
                    m_OnCreatePrefabRequested(entity);
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();

            // Cor de perigo (mesmo padrao usado no botao "Parar" da menu
            // bar do EditorLayer) - deixa claro visualmente que e uma
            // acao destrutiva antes mesmo de ler o texto.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.3f, 1.0f));
            bool clickedDelete = ImGui::MenuItem("Excluir", "Delete");
            ImGui::PopStyleColor();

            if (clickedDelete) {
                // Chamado DEPOIS de EndPopup/PopID (ver fim da funcao) -
                // 'entity' pode ficar invalida assim que o callback rodar
                // (ele normalmente destroi a entidade), entao nao a
                // tocamos de novo depois disso; so terminamos de fechar o
                // ImGui primeiro (EndPopup/PopID nao dependem de 'entity'
                // continuar valida).
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::PopID();
                if (m_OnDelete)
                    m_OnDelete(entity);
                return;
            }

            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

}
