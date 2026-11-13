#include <Prism/Extensions/ExtensionUi.h>
#include <cstdio>

using namespace Prism;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static ExtensionPanelDesc Panel(const char* id, const char* title = "T") {
    ExtensionPanelDesc d; d.Id = id; d.Title = title; d.Draw = [] {}; return d;
}
static ExtensionMenuItemDesc Item(const char* path) {
    ExtensionMenuItemDesc d; d.Path = path; d.Action = [] {}; return d;
}

int main() {
    // --- paineis ---
    CHECK(ExtensionUiRegistry::RegisterPanel(Panel("a"), "ExtA"));
    CHECK(!ExtensionUiRegistry::RegisterPanel(Panel("a"), "ExtA"));      // Id repetido na mesma extensao
    CHECK(ExtensionUiRegistry::RegisterPanel(Panel("a"), "ExtB"));       // mesma Id em outra extensao: ok
    CHECK(!ExtensionUiRegistry::RegisterPanel(Panel(""), "ExtA"));
    CHECK(!ExtensionUiRegistry::RegisterPanel(Panel("x", ""), "ExtA"));
    CHECK(!ExtensionUiRegistry::RegisterPanel(Panel("a/b"), "ExtA"));    // '/' reservado na chave
    CHECK(!ExtensionUiRegistry::RegisterPanel(Panel("a#b"), "ExtA"));    // '#' reservado no ImGui
    { ExtensionPanelDesc d = Panel("semdraw"); d.Draw = nullptr; CHECK(!ExtensionUiRegistry::RegisterPanel(d, "ExtA")); }
    CHECK(ExtensionUiRegistry::GetPanels().size() == 2);
    CHECK(ExtensionUiRegistry::GetPanels()[0].Desc.Source == "ExtA");
    CHECK(!ExtensionUiRegistry::IsPanelOpen("ExtA", "a"));

    // --- abrir/fechar chama OnOpen/OnClose so quando muda ---
    int opened = 0, closed = 0;
    { ExtensionPanelDesc d = Panel("cb"); d.OnOpen = [&] { opened++; }; d.OnClose = [&] { closed++; };
      CHECK(ExtensionUiRegistry::RegisterPanel(d, "ExtA")); }
    CHECK(ExtensionUiRegistry::SetPanelOpen("ExtA", "cb", true));
    CHECK(ExtensionUiRegistry::SetPanelOpen("ExtA", "cb", true));        // sem mudanca
    CHECK(opened == 1 && closed == 0 && ExtensionUiRegistry::IsPanelOpen("ExtA", "cb"));
    CHECK(ExtensionUiRegistry::SetPanelOpen("ExtA", "cb", false));
    CHECK(opened == 1 && closed == 1);
    CHECK(!ExtensionUiRegistry::SetPanelOpen("ExtA", "naoexiste", true));
    // excecao em OnOpen nao escapa
    { ExtensionPanelDesc d = Panel("boom"); d.OnOpen = [] { throw 1; };
      CHECK(ExtensionUiRegistry::RegisterPanel(d, "ExtA"));
      CHECK(ExtensionUiRegistry::SetPanelOpen("ExtA", "boom", true)); }
    // OpenByDefault
    { ExtensionPanelDesc d = Panel("aberto"); d.OpenByDefault = true; CHECK(ExtensionUiRegistry::RegisterPanel(d, "ExtA")); }
    CHECK(ExtensionUiRegistry::IsPanelOpen("ExtA", "aberto"));

    // --- menus ---
    CHECK(ExtensionUiRegistry::SplitMenuPath("A/B/C").size() == 3);
    CHECK(ExtensionUiRegistry::SplitMenuPath("A").size() == 1);
    CHECK(ExtensionUiRegistry::SplitMenuPath("").empty());
    CHECK(ExtensionUiRegistry::SplitMenuPath("/A").empty());
    CHECK(ExtensionUiRegistry::SplitMenuPath("A/").empty());
    CHECK(ExtensionUiRegistry::SplitMenuPath("A//B").empty());
    CHECK(!ExtensionUiRegistry::RegisterMenuItem(Item("A//B"), "ExtA"));
    { ExtensionMenuItemDesc d = Item("Ok/Item"); d.Action = nullptr; CHECK(!ExtensionUiRegistry::RegisterMenuItem(d, "ExtA")); }
    CHECK(ExtensionUiRegistry::RegisterMenuItem(Item("Janela/Meu painel"), "ExtA"));
    CHECK(ExtensionUiRegistry::RegisterMenuItem(Item("Ferramentas/Sub/Fazer"), "ExtA"));
    CHECK(ExtensionUiRegistry::RegisterMenuItem(Item("Ferramentas/Outro"), "ExtB"));
    CHECK(ExtensionUiRegistry::RegisterMenuItem(Item("Plugins/X"), "ExtB"));
    auto extra = ExtensionUiRegistry::GetExtraMenus({ "Arquivo", "Editar", "Janela", "Ajuda" });
    CHECK(extra.size() == 2 && extra[0] == "Ferramentas" && extra[1] == "Plugins"); // sem repeticao, ordem de registro

    // --- remover por extensao ---
    ExtensionUiRegistry::UnregisterSource("ExtA");
    CHECK(ExtensionUiRegistry::GetPanels().size() == 1 && ExtensionUiRegistry::GetPanels()[0].Desc.Source == "ExtB");
    CHECK(ExtensionUiRegistry::GetMenuItems().size() == 2);
    CHECK(ExtensionUiRegistry::PanelKey("E", "p") == "E/p");
    ExtensionUiRegistry::Clear();
    CHECK(ExtensionUiRegistry::GetPanels().empty() && ExtensionUiRegistry::GetMenuItems().empty());

    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
