#include "ExtensionPanelHost.h"

#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName / DockId (acoplar ao lado de um painel do editor)
#include <algorithm>
#include <exception>
#include <map>

namespace PrismEditor {

    using namespace Prism;

    namespace {
        // Painel do editor ao lado do qual um painel Docked aparece da primeira vez (ver BuildDefaultLayout).
        const char* DockNeighbour(ExtensionDockHint hint) {
            switch (hint) {
                case ExtensionDockHint::Left:   return "Hierarquia";
                case ExtensionDockHint::Right:  return "Propriedades";
                case ExtensionDockHint::Bottom: return "Console";
                case ExtensionDockHint::Center: return "Viewport";
                default:                        return nullptr;
            }
        }

        // ID do ImGui: o titulo e o que o usuario ve; "###" fixa a identidade (e o layout salvo) na chave
        // extensao/id, entao renomear o titulo numa nova versao nao perde a posicao do painel.
        std::string WindowName(const ExtensionPanelState& panel) {
            return panel.Desc.Title + "###ext:" + ExtensionUiRegistry::PanelKey(panel.Desc.Source, panel.Desc.Id);
        }

        void DrawPanel(ExtensionPanelState& panel, bool& openFlag) {
            const ExtensionPanelDesc& d = panel.Desc;
            ImGuiWindowFlags flags = ImGuiWindowFlags_None;
            const ImVec2 size(d.Width, d.Height);

            switch (d.Mode) {
                case ExtensionPanelMode::Docked: {
                    if (const char* neighbour = DockNeighbour(d.Dock)) {
                        ImGuiWindow* other = ImGui::FindWindowByName(neighbour);
                        if (other && other->DockId != 0)
                            ImGui::SetNextWindowDockID(other->DockId, ImGuiCond_FirstUseEver);
                    }
                    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
                    break;
                }
                case ExtensionPanelMode::Floating:
                    flags |= ImGuiWindowFlags_NoDocking;
                    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
                    break;
                case ExtensionPanelMode::OsWindow: {
                    // Janela propria do sistema: com multi-viewport ligado (ImGuiLayer), uma janela sem docking
                    // que NAO cabe dentro da janela principal ganha seu proprio viewport/janela do SO. Na primeira
                    // abertura posicionamos de modo a sair da janela do editor; depois o usuario a move livremente
                    // (e o ImGui guarda onde ela ficou no imgui.ini).
                    flags |= ImGuiWindowFlags_NoDocking;
                    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
                    const ImGuiViewport* main = ImGui::GetMainViewport();
                    ImGui::SetNextWindowPos(ImVec2(main->Pos.x + main->Size.x - size.x * 0.25f, main->Pos.y + 80.0f), ImGuiCond_FirstUseEver);
                    break;
                }
            }

            const std::string name = WindowName(panel);
            const bool visible = ImGui::Begin(name.c_str(), &openFlag, flags);
            if (visible) {
                if (panel.Faulted) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Este painel falhou e foi desligado:");
                    ImGui::TextWrapped("%s", panel.FaultMessage.c_str());
                    ImGui::TextDisabled("Extensao: %s", d.Source.c_str());
                } else {
                    try {
                        d.Draw();
                    } catch (const std::exception& e) {
                        panel.Faulted = true;
                        panel.FaultMessage = e.what();
                    } catch (...) {
                        panel.Faulted = true;
                        panel.FaultMessage = "excecao desconhecida";
                    }
                }
            }
            ImGui::End();
        }
    }

    void ExtensionPanelHost::OnImGuiRender() {
        // Indice, nao iterador: Draw() de uma extensao pode registrar/abrir paineis.
        auto& panels = ExtensionUiRegistry::GetPanels();
        for (size_t i = 0; i < panels.size(); ++i) {
            if (!panels[i].Open) continue;
            bool open = true;
            // Copia da chave: 'panels' pode realocar durante Draw().
            const std::string source = panels[i].Desc.Source, id = panels[i].Desc.Id;
            DrawPanel(panels[i], open);
            if (!open) ExtensionUiRegistry::SetPanelOpen(source, id, false); // fechou pelo X: chama OnClose
        }
    }

    bool ExtensionPanelHost::AnyOpen() {
        for (const auto& p : ExtensionUiRegistry::GetPanels())
            if (p.Open) return true;
        return false;
    }

    void ExtensionPanelHost::DrawWindowMenuEntries() {
        auto& panels = ExtensionUiRegistry::GetPanels();
        if (panels.empty()) return;
        if (!ImGui::BeginMenu("Paineis de extensoes")) return;

        // Agrupa por extensao (ordem de registro).
        std::vector<std::string> sources;
        for (const auto& p : panels)
            if (std::find(sources.begin(), sources.end(), p.Desc.Source) == sources.end()) sources.push_back(p.Desc.Source);

        for (const std::string& source : sources) {
            if (!ImGui::BeginMenu(source.c_str())) continue;
            for (size_t i = 0; i < panels.size(); ++i) {
                if (panels[i].Desc.Source != source) continue;
                const std::string id = panels[i].Desc.Id;
                if (ImGui::MenuItem((panels[i].Desc.Title + "##" + id).c_str(), nullptr, panels[i].Open))
                    ExtensionUiRegistry::SetPanelOpen(source, id, !ExtensionUiRegistry::IsPanelOpen(source, id));
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    bool ExtensionPanelHost::HasMenuItems(const std::string& menuName) const {
        for (const auto& item : ExtensionUiRegistry::GetMenuItems())
            if (ExtensionUiRegistry::SplitMenuPath(item.Path).front() == menuName) return true;
        return false;
    }

    namespace {
        // Desenha os itens cujo caminho (a partir do trecho 'depth') cai em 'prefix', criando submenus.
        // 'items' = indices em GetMenuItems(), ja filtrados pelo menu de cima.
        void DrawLevel(const std::vector<size_t>& items, size_t depth) {
            const auto& all = ExtensionUiRegistry::GetMenuItems();
            std::vector<std::string> done; // submenus ja desenhados neste nivel
            for (size_t index : items) {
                const std::vector<std::string> parts = ExtensionUiRegistry::SplitMenuPath(all[index].Path);
                if (parts.size() == depth + 1) {
                    // Folha. Copia os std::function: a Action pode alterar a lista de itens.
                    const ExtensionMenuItemDesc item = all[index];
                    bool enabled = true, checked = false;
                    try {
                        if (item.IsEnabled) enabled = item.IsEnabled();
                        if (item.IsChecked) checked = item.IsChecked();
                    } catch (...) { enabled = false; }
                    if (ImGui::MenuItem((parts.back() + "##" + item.Source + std::to_string(index)).c_str(),
                                        item.Shortcut.empty() ? nullptr : item.Shortcut.c_str(), checked, enabled)) {
                        try { item.Action(); } catch (...) {}
                        return; // a lista pode ter mudado: nao continua a iterar sobre indices antigos
                    }
                } else {
                    const std::string& submenu = parts[depth];
                    if (std::find(done.begin(), done.end(), submenu) != done.end()) continue;
                    done.push_back(submenu);
                    std::vector<size_t> children;
                    for (size_t other : items) {
                        const auto p = ExtensionUiRegistry::SplitMenuPath(all[other].Path);
                        if (p.size() > depth + 1 && p[depth] == submenu) children.push_back(other);
                    }
                    if (ImGui::BeginMenu(submenu.c_str())) {
                        DrawLevel(children, depth + 1);
                        ImGui::EndMenu();
                    }
                }
            }
        }
    }

    void ExtensionPanelHost::DrawMenuItems(const std::string& menuName) {
        const auto& all = ExtensionUiRegistry::GetMenuItems();
        std::vector<size_t> items;
        for (size_t i = 0; i < all.size(); ++i)
            if (ExtensionUiRegistry::SplitMenuPath(all[i].Path).front() == menuName) items.push_back(i);
        if (items.empty()) return;
        DrawLevel(items, 1); // trecho 0 e o proprio menu
    }

    void ExtensionPanelHost::DrawExtraMenus(const std::vector<std::string>& builtinMenus) {
        for (const std::string& menu : ExtensionUiRegistry::GetExtraMenus(builtinMenus)) {
            if (ImGui::BeginMenu(menu.c_str())) {
                DrawMenuItems(menu);
                ImGui::EndMenu();
            }
        }
    }

} // namespace PrismEditor
