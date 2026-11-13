#pragma once

// ============================================================================
// ExtensionUi.h
// Interface que uma extensao pode ADICIONAR ao editor (API v2): paineis (acoplados ao layout,
// flutuantes ou em janela propria do sistema) e itens de menu. Aqui ficam os descritores
// (compartilhados com a dll) e o registro (lado da engine). Quem DESENHA e o editor
// (PrismEditor/Panels/ExtensionPanelHost) - o registro nao depende do ImGui e e testavel
// sozinho (tests/ExtensionUiTests.cpp).
//
// Painel: a engine abre ImGui::Begin(titulo) e chama Draw() la dentro; a extensao so desenha o
// conteudo (host.UiEx tem os widgets basicos sem linkar o ImGui). Exceto 'OnOpen/OnClose',
// tudo roda na thread principal. Uma excecao em Draw() desliga o painel (mostra o erro) em vez
// de derrubar o editor.
// ============================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Prism {

    enum class ExtensionPanelMode : uint32_t {
        Docked = 0,    // painel normal: acopla no layout (o usuario pode arrastar, empilhar em abas, desacoplar)
        Floating = 1,  // janela flutuante dentro do editor (nunca acopla)
        OsWindow = 2   // janela propria do sistema operacional (fora da janela do editor, ex: em outro monitor)
    };

    // Onde um painel Docked aparece da primeira vez (depois vale o layout salvo pelo usuario).
    enum class ExtensionDockHint : uint32_t { None = 0, Left, Right, Bottom, Center };

    struct ExtensionPanelDesc {
        std::string Id;                       // unico DENTRO da extensao (ex: "inventario")
        std::string Title;                    // titulo da janela e do item no menu Janela
        ExtensionPanelMode Mode = ExtensionPanelMode::Docked;
        ExtensionDockHint Dock = ExtensionDockHint::None;
        bool OpenByDefault = false;           // aberto ao carregar a extensao (se o usuario nao o fechou - ver editor)
        float Width = 420.0f;                 // tamanho inicial (Floating/OsWindow e Docked sem layout salvo)
        float Height = 320.0f;
        std::function<void()> Draw;           // OBRIGATORIO: desenha o conteudo
        std::function<void()> OnOpen;         // opcional
        std::function<void()> OnClose;        // opcional
        std::string Source;                   // preenchido pela engine (nome da extensao)
    };

    struct ExtensionMenuItemDesc {
        // "Menu/Submenu/Item". O primeiro trecho e o menu da barra: um existente do editor
        // ("Arquivo", "Editar", "Entidade", "Janela", "Ajuda") recebe o item no fim; qualquer outro
        // nome vira um menu novo, depois dos do editor.
        std::string Path;
        std::string Shortcut;                 // so texto exibido (ex: "Ctrl+Shift+K"); NAO registra a tecla
        std::function<void()> Action;         // OBRIGATORIO
        std::function<bool()> IsEnabled;      // opcional (padrao: sempre habilitado)
        std::function<bool()> IsChecked;      // opcional: mostra a marca de selecao
        std::string Source;
    };

    struct ExtensionPanelState {
        ExtensionPanelDesc Desc;
        bool Open = false;
        bool Faulted = false;                 // Draw() lancou excecao: nao e mais chamado
        std::string FaultMessage;
    };

    class ExtensionUiRegistry {
    public:
        // false (e nada registrado) se: Id/Title/Draw faltando, ou ja existe painel com o mesmo Id nesta extensao.
        static bool RegisterPanel(ExtensionPanelDesc desc, const std::string& source);
        // false se Path vazio/malformado (trecho vazio, "//", comeca ou termina com '/') ou sem Action.
        static bool RegisterMenuItem(ExtensionMenuItemDesc desc, const std::string& source);

        // Abre/fecha um painel (chama OnOpen/OnClose se o estado mudou). false se nao existe.
        static bool SetPanelOpen(const std::string& source, const std::string& id, bool open);
        static bool IsPanelOpen(const std::string& source, const std::string& id);

        // Remove tudo que 'source' registrou (Shutdown da extensao).
        static void UnregisterSource(const std::string& source);

        static std::vector<ExtensionPanelState>& GetPanels();
        static const std::vector<ExtensionMenuItemDesc>& GetMenuItems();

        // Nomes de menu usados pelas extensoes (primeiro trecho do Path) que NAO estao em 'builtinMenus',
        // sem repeticao, na ordem de registro.
        static std::vector<std::string> GetExtraMenus(const std::vector<std::string>& builtinMenus);

        // "A/B/C" -> {"A","B","C"}; vazio se malformado.
        static std::vector<std::string> SplitMenuPath(const std::string& path);

        // Chave estavel do painel ("<extensao>/<id>"), usada como ID do ImGui e no layout salvo.
        static std::string PanelKey(const std::string& source, const std::string& id) { return source + "/" + id; }

        static void Clear(); // testes
    };

} // namespace Prism
