#pragma once

// ============================================================================
// ExtensionPanelHost
// Desenha no editor a interface que as extensoes registraram (Extensions/ExtensionUi.h):
// os paineis abertos e os itens de menu. Fica no editor (e nao na engine) porque e UI do editor.
// ============================================================================

#include <Prism/Extensions/ExtensionUi.h>
#include <string>
#include <vector>

namespace PrismEditor {

    class ExtensionPanelHost {
    public:
        // Chame todo frame, junto dos outros paineis (fora de qualquer Begin/End).
        void OnImGuiRender();

        // Dentro de BeginMenu("Janela"): submenu "Extensoes (paineis)" com um item marcavel por painel.
        void DrawWindowMenuEntries();
        // Dentro do menu do editor 'menuName' ("Arquivo", "Janela"...): itens das extensoes com esse primeiro trecho.
        // Devolve true se desenhou algo (o chamador pode por um Separator ANTES, ver uso).
        bool HasMenuItems(const std::string& menuName) const;
        void DrawMenuItems(const std::string& menuName);
        // Depois dos menus do editor: um menu por nome novo usado pelas extensoes.
        void DrawExtraMenus(const std::vector<std::string>& builtinMenus);

        // Fecha todos (ao fechar o projeto/editor a lista de paineis some junto com as extensoes).
        static bool AnyOpen();
    };

} // namespace PrismEditor
