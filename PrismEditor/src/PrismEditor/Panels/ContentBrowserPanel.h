#pragma once

// ============================================================================
// ContentBrowserPanel.h
// Painel de navegacao pelos arquivos do projeto ativo (Assets/Maps/Scripts/
// Cache) - ate agora nao havia NENHUMA forma de ver o que existe dentro de
// um projeto sem sair do editor e abrir o explorador de arquivos do SO.
//
// Navega a partir de Project::GetProjectDirectory() (a raiz do projeto
// inteiro, nao so a pasta Assets) porque o usuario tambem precisa ver Maps/
// e Scripts/ - e exatamente o motivo deste painel existir agora, antes do
// sistema de "Salvar Como"/multiplos mapas (proximo passo do roadmap), que
// vai precisar de um jeito de mostrar/escolher arquivos .prismmap.
//
// Escopo desta primeira versao, deliberadamente:
//   - navegar por pastas (duplo-clique entra, breadcrumb/botao voltar sai)
//   - selecionar um arquivo (fica destacado, mostra o caminho completo)
//   - duplo-clique num .prismmap dispara um callback opcional (ver
//     SetOnMapDoubleClicked) - o EditorLayer se inscreve nisso para
//     efetivamente abrir o mapa
//   - NAO tem criar/renomear/excluir/arrastar arquivos ainda - fica para
//     quando o fluxo de assets (importar FBX/OBJ/etc, ver guia do
//     prototipo) precisar disso de verdade
// ============================================================================

#include <Prism.h>
#include <filesystem>
#include <functional>
#include <vector>
#include <string>

namespace PrismEditor {

    class ContentBrowserPanel {
    public:
        // Callback opcional chamado quando o usuario da duplo-clique num
        // arquivo .prismmap - o EditorLayer se inscreve nisso para abrir o
        // mapa clicado. Sem callback registrado, duplo-clique num .prismmap
        // so seleciona (comportamento seguro por padrao).
        using MapDoubleClickedFn = std::function<void(const std::filesystem::path&)>;
        void SetOnMapDoubleClicked(MapDoubleClickedFn fn) { m_OnMapDoubleClicked = std::move(fn); }

        // Reseta a pasta atual para a raiz do projeto ativo - chamado
        // quando um projeto novo/diferente e aberto (ver EditorLayer::OnAttach),
        // para nao continuar mostrando a pasta do projeto anterior.
        void ResetToProjectRoot();

        void OnImGuiRender();

        // Releva 'm_CurrentDirectory' do disco (reconciliando .meta novos/
        // orfaos - ver Assets/AssetRegistry.h - fica a cargo do CHAMADOR:
        // esta funcao so relista os arquivos, nao mexe no AssetRegistry).
        // Publico porque outros paineis podem alterar o conteudo desta
        // mesma pasta por fora deste painel - ex: EditorLayer::
        // RenderSaveMaterialPopup grava um novo .prismmat dentro de
        // Assets/Materials e chama isto para o arquivo aparecer aqui sem
        // esperar o usuario clicar "Atualizar" manualmente. O botao
        // "Atualizar" do proprio painel (ver RenderToolbar) chama o
        // mesmo metodo.
        void RefreshEntries();

    private:
        void RenderToolbar();
        void RenderGrid();

        void NavigateTo(const std::filesystem::path& directory);

    private:
        std::filesystem::path m_CurrentDirectory;
        std::filesystem::path m_SelectedPath;

        MapDoubleClickedFn m_OnMapDoubleClicked;

        // Evita reconstruir a lista de entradas do diretorio a cada frame -
        // so relemos o disco quando a pasta atual muda (NavigateTo) ou
        // quando o usuario pede um refresh explicito.
        struct Entry {
            std::filesystem::path Path;
            std::string Name;
            bool IsDirectory;
        };
        std::vector<Entry> m_CachedEntries;
    };

}
