#include "ExtensionUi.h"

#include <algorithm>
#include <exception>

namespace Prism {

    namespace {
        std::vector<ExtensionPanelState> s_Panels;
        std::vector<ExtensionMenuItemDesc> s_MenuItems;

        // Chamadas a codigo da extensao nunca propagam excecao (a dll pode lancar qualquer coisa).
        void SafeCall(const std::function<void()>& fn) {
            if (!fn) return;
            try { fn(); } catch (...) {}
        }

        ExtensionPanelState* Find(const std::string& source, const std::string& id) {
            for (ExtensionPanelState& p : s_Panels)
                if (p.Desc.Source == source && p.Desc.Id == id) return &p;
            return nullptr;
        }
    }

    bool ExtensionUiRegistry::RegisterPanel(ExtensionPanelDesc desc, const std::string& source) {
        if (desc.Id.empty() || desc.Title.empty() || !desc.Draw) return false;
        if (desc.Id.find('/') != std::string::npos || desc.Id.find('#') != std::string::npos) return false; // '#' tem significado no ID do ImGui
        if (Find(source, desc.Id)) return false;
        desc.Source = source;
        ExtensionPanelState state;
        state.Open = desc.OpenByDefault;
        state.Desc = std::move(desc);
        s_Panels.push_back(std::move(state));
        return true;
    }

    std::vector<std::string> ExtensionUiRegistry::SplitMenuPath(const std::string& path) {
        std::vector<std::string> parts;
        std::string current;
        for (char c : path) {
            if (c == '/') {
                if (current.empty()) return {};
                parts.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (current.empty()) return {};
        parts.push_back(current);
        return parts;
    }

    bool ExtensionUiRegistry::RegisterMenuItem(ExtensionMenuItemDesc desc, const std::string& source) {
        if (!desc.Action || SplitMenuPath(desc.Path).empty()) return false;
        desc.Source = source;
        s_MenuItems.push_back(std::move(desc));
        return true;
    }

    bool ExtensionUiRegistry::SetPanelOpen(const std::string& source, const std::string& id, bool open) {
        ExtensionPanelState* p = Find(source, id);
        if (!p) return false;
        if (p->Open == open) return true;
        p->Open = open;
        SafeCall(open ? p->Desc.OnOpen : p->Desc.OnClose);
        return true;
    }

    bool ExtensionUiRegistry::IsPanelOpen(const std::string& source, const std::string& id) {
        const ExtensionPanelState* p = Find(source, id);
        return p && p->Open;
    }

    void ExtensionUiRegistry::UnregisterSource(const std::string& source) {
        s_Panels.erase(std::remove_if(s_Panels.begin(), s_Panels.end(),
            [&](const ExtensionPanelState& p) { return p.Desc.Source == source; }), s_Panels.end());
        s_MenuItems.erase(std::remove_if(s_MenuItems.begin(), s_MenuItems.end(),
            [&](const ExtensionMenuItemDesc& m) { return m.Source == source; }), s_MenuItems.end());
    }

    std::vector<ExtensionPanelState>& ExtensionUiRegistry::GetPanels() { return s_Panels; }
    const std::vector<ExtensionMenuItemDesc>& ExtensionUiRegistry::GetMenuItems() { return s_MenuItems; }

    std::vector<std::string> ExtensionUiRegistry::GetExtraMenus(const std::vector<std::string>& builtinMenus) {
        std::vector<std::string> extra;
        for (const ExtensionMenuItemDesc& item : s_MenuItems) {
            const std::string top = SplitMenuPath(item.Path).front();
            if (std::find(builtinMenus.begin(), builtinMenus.end(), top) != builtinMenus.end()) continue;
            if (std::find(extra.begin(), extra.end(), top) == extra.end()) extra.push_back(top);
        }
        return extra;
    }

    void ExtensionUiRegistry::Clear() {
        s_Panels.clear();
        s_MenuItems.clear();
    }

} // namespace Prism
