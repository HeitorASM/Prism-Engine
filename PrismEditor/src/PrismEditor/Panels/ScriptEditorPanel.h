#pragma once

// ============================================================================
// ScriptEditorPanel.h
// "Editor de Codigo": painel de edicao de texto com VARIOS arquivos abertos ao
// mesmo tempo. Uma lateral a esquerda lista os arquivos abertos (clique troca,
// "x" fecha); o texto do arquivo ativo fica a direita.
//
// Abre scripts Lua, C++ e qualquer outro arquivo de texto (.md, .txt...).
// Syntax highlight so para Lua e C/C++ (por extensao) via ImGuiColorTextEdit
// (fork goossens - ver vendor/CMakeLists.txt); o resto abre como texto puro.
//
// Fluxo tipico: duplo clique num arquivo do Conteudo do Projeto, ou o botao
// "Editar" do component Script (ver PropertiesPanel_Script.cpp), chamam
// Open(caminho). Abrir um arquivo que ja esta aberto so o ativa.
//
// Salvamento: so grava no disco quando o usuario pede (Ctrl+S, "Salvar" ou
// "Salvar tudo"), nunca a cada tecla. Cada arquivo rastreia suas alteracoes
// nao salvas (marcadas com "*"); fechar um arquivo com alteracoes pergunta
// antes (Salvar / Descartar / Cancelar).
// ============================================================================

#include <TextEditor.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace PrismEditor {

    class ScriptEditorPanel {
    public:
        // Abre 'path' (ou ativa, se ja estiver aberto). Se o arquivo nao existir
        // ainda (ex: script recem criado), abre vazio - o primeiro Salvar cria.
        // Retorna false se foi recusado (parece binario ou passa de 4 MB).
        bool Open(const std::filesystem::path& path);

        // Heuristica: tamanho razoavel e sem bytes NUL no inicio do arquivo.
        static bool LooksLikeTextFile(const std::filesystem::path& path);

        // true se ha algum arquivo aberto.
        bool IsOpen() const { return !m_Documents.empty(); }

        // Caminho do arquivo ativo (vazio se nao ha nenhum).
        const std::filesystem::path& GetCurrentPath() const;

        // Grava o arquivo ativo / todos os que tem alteracoes. Erros vao para o Console.
        bool Save();
        bool SaveAll();

        void OnImGuiRender();

        // true se o painel tinha o foco no ultimo frame - o EditorLayer usa para nao
        // tratar Ctrl+Z/Ctrl+S/Delete como atalhos da CENA enquanto se digita codigo.
        bool IsFocused() const { return m_Focused; }

    private:
        bool m_Focused = false;

        struct Document {
            std::filesystem::path Path;
            TextEditor Editor; // guarda texto, undo/redo, selecao e rolagem deste arquivo
            bool Dirty = false;
            // O TextEditor guarda so '\n': arquivos CRLF sao convertidos de volta ao salvar.
            bool Crlf = false;
        };

        static void LoadInto(Document& document, const std::filesystem::path& path);
        static bool SaveDocument(Document& document);
        int FindDocument(const std::filesystem::path& path) const;
        void CloseDocument(int index);
        bool AnyDirty() const;

        std::vector<std::unique_ptr<Document>> m_Documents;
        int m_Active = -1;

        // Fechamento pendente de um arquivo com alteracoes (popup de confirmacao).
        int m_PendingClose = -1;
        bool m_OpenClosePopup = false;
    };

}
