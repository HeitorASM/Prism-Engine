#pragma once

// ============================================================================
// ScriptEditorPanel.h
// Painel de edicao de texto para scripts Lua, com syntax highlight via
// ImGuiColorTextEdit (fork goossens - ver vendor/CMakeLists.txt para o
// porque dessa lib especifica). A classe TextEditor da lib cuida
// internamente do buffer de texto e do redimensionamento.
//
// Fluxo tipico: Properties panel (ScriptComponent) tem um botao "Editar"
// que chama Open(caminho) aqui - ver EditorLayer::RenderPropertiesPanel().
// O painel entao aparece dockado no editor (ImGui::Begin de janela normal,
// mesmo padrao de ConsolePanel/ContentBrowserPanel) mostrando o conteudo
// daquele arquivo com highlight de Lua.
//
// Salvamento: so grava de volta no disco quando o usuario pede
// explicitamente (Ctrl+S ou botao Salvar), nunca a cada tecla - m_Dirty
// rastreia mudancas nao salvas (mostrado no titulo da janela).
// ============================================================================

#include <TextEditor.h>
#include <filesystem>
#include <string>

namespace PrismEditor {

    class ScriptEditorPanel {
    public:
        // Abre 'scriptAbsolutePath' para edicao: le o conteudo do disco e
        // chama m_Editor.SetText() com ele, tornando este o arquivo ativo
        // do painel. Se o arquivo nao existir ainda (ex: acabou de ser
        // criado por EditorLayer::CreateNewScript), abre com texto vazio -
        // o primeiro Salvar cria o arquivo.
        //
        // Se ja havia um arquivo DIFERENTE aberto com mudancas nao salvas
        // (m_Dirty == true), a troca acontece mesmo assim (a UI que chama
        // Open, no EditorLayer, e responsavel por perguntar antes se quiser
        // - esta primeira versao nao pergunta sozinha, para manter o painel
        // simples).
        void Open(const std::filesystem::path& scriptAbsolutePath);

        // true se ha um arquivo aberto no momento (m_CurrentPath nao vazio) -
        // usado por EditorLayer para decidir se abre/foca a janela do painel
        // ao clicar "Editar" (ver RenderPropertiesPanel).
        bool IsOpen() const { return !m_CurrentPath.empty(); }

        // Caminho do arquivo atualmente aberto (vazio se IsOpen() == false).
        const std::filesystem::path& GetCurrentPath() const { return m_CurrentPath; }

        // Grava o texto atual do m_Editor de volta em m_CurrentPath.
        // Retorna false se IsOpen() == false ou a escrita falhar (ex: sem
        // permissao) - erro concreto vai para PRISM_CORE_ERROR/Console,
        // mesmo padrao do resto do editor.
        bool Save();

        void OnImGuiRender();

    private:
        std::filesystem::path m_CurrentPath;

        // TextEditor gerencia o texto/buffer internamente (undo/redo,
        // highlight, selecao etc) - nao precisamos mais de um buffer
        // proprio como a primeira versao deste painel tinha.
        TextEditor m_Editor;

        bool m_Dirty = false; // true desde a ultima edicao ate o proximo Save() bem-sucedido
    };

}
