// Inspetor generico dos components registrados por EXTENSOES.
//
// O painel nao conhece esses tipos em C++: cada extensao fornece seu proprio
// DrawInspector (ImGui) no ComponentRegistry. O Undo/Redo e capturado aqui por
// comparacao de bytes serializados (Serialize/DeserializeComponent), agrupando
// um gesto inteiro (arrastar um slider, editar um texto) em UM comando.
#include "PropertiesPanel.h"
#include "../Commands/EditorCommands.h"

#include <imgui.h>

namespace PrismEditor {

    void PropertiesPanel::DrawExtensionComponentsUI() {
        Prism::Entity entity = m_Ctx.SelectedEntity;
        if (!entity.IsValid()) return;

        // Copia indices (nao ponteiros): o registro pode crescer durante um DrawInspector.
        const size_t count = Prism::ComponentRegistry::GetAll().size();
        for (size_t i = 0; i < count; ++i) {
            const Prism::ComponentTypeInfo& info = Prism::ComponentRegistry::GetAll()[i];
            if (info.Source.empty() || !info.Has(entity)) continue;

            const std::string name = info.DisplayName;
            bool keepOpen = true;
            ImGui::PushID(name.c_str());
            const bool open = ImGui::CollapsingHeader(name.c_str(), &keepOpen, ImGuiTreeNodeFlags_DefaultOpen);

            if (!keepOpen) {
                ImGui::PopID();
                m_ExtEdit.Valid = false;
                m_Ctx.History.Execute(Prism::CreateScope<RemoveRegisteredComponentCommand>(entity, name));
                return; // o componente saiu; evita usar info/iteracao invalidos
            }

            if (open) {
                if (info.DrawInspector) {
                    info.DrawInspector(entity);

                    // --- captura de Undo ---
                    const Prism::ComponentTypeInfo* cur = Prism::ComponentRegistry::FindByName(name);
                    if (cur && cur->Has(entity)) {
                        const std::string now = Prism::ComponentRegistry::SerializeComponent(*cur, entity);
                        if (!m_ExtEdit.Valid || !(m_ExtEdit.Entity == entity) || m_ExtEdit.Name != name) {
                            m_ExtEdit.Entity = entity;
                            m_ExtEdit.Name = name;
                            m_ExtEdit.Before = now;
                            m_ExtEdit.Dragging = false;
                            m_ExtEdit.Valid = true;
                        } else if (ImGui::IsAnyItemActive()
                                   || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                            m_ExtEdit.Dragging = true; // gesto em andamento
                        } else {
                            if (m_ExtEdit.Dragging && now != m_ExtEdit.Before)
                                m_Ctx.History.Execute(Prism::CreateScope<EditRegisteredComponentCommand>(entity, name, m_ExtEdit.Before, now));
                            m_ExtEdit.Before = now;
                            m_ExtEdit.Dragging = false;
                        }
                    }
                } else {
                    ImGui::TextDisabled("(esta extensao nao fornece inspetor)");
                }
            }
            ImGui::PopID();
        }
    }

} // namespace PrismEditor
