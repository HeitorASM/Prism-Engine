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
//   - criar pasta, renomear (F2), excluir (Del, com confirmacao) e mover
//     (arrastar para uma pasta ou para o botao Voltar). O .meta acompanha
//     o arquivo, entao o AssetID - e todas as referencias a ele - sobrevive.
//     As pastas/arquivos da raiz do projeto sao protegidos.
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

        // Duplo-clique num arquivo "generico" (nao e pasta, mapa, projeto,
        // prefab, material, modelo, audio nem imagem): o EditorLayer abre no
        // Editor de Codigo, que recusa binarios.
        using TextFileDoubleClickedFn = std::function<void(const std::filesystem::path&)>;
        void SetOnTextFileDoubleClicked(TextFileDoubleClickedFn fn) { m_OnTextFileDoubleClicked = std::move(fn); }

        // Reseta a pasta atual para a raiz do projeto ativo - chamado
        // quando um projeto novo/diferente e aberto (ver EditorLayer::OnAttach),
        // para nao continuar mostrando a pasta do projeto anterior.
        void ResetToProjectRoot();

        // Avisados depois que um arquivo/pasta foi movido/renomeado ou excluido
        // pelo painel - o EditorLayer ajusta o que aponta para o caminho antigo
        // (ex: EditorContext::CurrentMapPath).
        using PathMovedFn = std::function<void(const std::filesystem::path& from, const std::filesystem::path& to)>;
        using PathDeletedFn = std::function<void(const std::filesystem::path& path)>;
        void SetOnPathMoved(PathMovedFn fn) { m_OnPathMoved = std::move(fn); }
        void SetOnPathDeleted(PathDeletedFn fn) { m_OnPathDeleted = std::move(fn); }

        void OnImGuiRender();

        // Releva 'm_CurrentDirectory' do disco (reconciliando .meta novos/
        // orfaos - ver Assets/AssetRegistry.h - fica a cargo do CHAMADOR:
        // esta funcao so relista os arquivos, nao mexe no AssetRegistry).
        // Publico porque outros paineis podem alterar o conteudo desta
        // mesma pasta por fora deste painel - ex: PropertiesPanel::
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

        // Operacoes de arquivo (todas validam, movem o .meta junto, reconciliam
        // o AssetRegistry e relistam a pasta). 'error' recebe a mensagem.
        bool MoveEntry(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error);
        bool DeleteEntry(const std::filesystem::path& path, std::string& error);
        bool CreateFolder(const std::string& name, std::string& error);
        bool IsProtected(const std::filesystem::path& path) const;
        static bool ValidName(const std::string& name, std::string& error);

        void RenderPopups();
        void BeginRename(const std::filesystem::path& path);
        void BeginDelete(const std::filesystem::path& path);

    private:
        std::filesystem::path m_CurrentDirectory;
        std::filesystem::path m_SelectedPath;

        PathMovedFn m_OnPathMoved;
        PathDeletedFn m_OnPathDeleted;

        // Estado dos popups (abrir/renomear/excluir/nova pasta).
        std::filesystem::path m_PopupTarget;
        char m_NameBuffer[256] = {};
        std::string m_PopupError;
        bool m_OpenRenamePopup = false;
        bool m_OpenDeletePopup = false;
        bool m_OpenNewFolderPopup = false;

        float m_CellSize = 88.0f; // tamanho dos icones (slider na barra)

        MapDoubleClickedFn m_OnMapDoubleClicked;
        TextFileDoubleClickedFn m_OnTextFileDoubleClicked;

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
