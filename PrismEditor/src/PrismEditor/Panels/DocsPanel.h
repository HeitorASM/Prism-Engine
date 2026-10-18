#pragma once

// ============================================================================
// DocsPanel.h
// Painel "Documentacao": mostra os arquivos .md da pasta docs/ dentro do
// editor (lista de documentos a esquerda, conteudo a direita), com busca,
// navegacao por links entre documentos e botao Voltar.
//
// Duas fontes de documentos:
//   - "Engine" (padrao): docs/inengine/*.md - guias para quem USA a engine
//     (visao geral, editor, scripting, fisica...). A primeira pagina e a
//     "Visao geral". Ordem = ordem alfabetica do nome do arquivo (00-, 01-...).
//   - "GitHub": docs/*.md - a documentacao tecnica do repositorio.
// Um botao pequeno no fim da lista alterna entre as duas.
//
// Renderizador de Markdown simples (o suficiente para os docs da engine):
// titulos, paragrafos, listas, tabelas, blocos de codigo, citacoes e
// separadores. Formatacao em linha (**negrito**, `codigo`) e removida; links
// [texto](arquivo.md) para outros docs viram botoes clicaveis abaixo do bloco.
//
// Onde fica a pasta docs/: PRISM_DOCS_SOURCE_DIR (pasta do codigo-fonte, definida
// pelo CMake), <pasta do executavel>/docs (copiada no build) ou
// <diretorio de trabalho>/docs.
// ============================================================================

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace PrismEditor {

    class DocsPanel {
    public:
        // Mostra o painel (e o traz para frente). Com 'fileName' (ex:
        // "04-scripting-lua.md"), abre esse doc da documentacao da engine.
        void Open(const std::string& fileName = std::string());
        bool IsOpen() const { return m_Open; }

        void OnImGuiRender();

    private:
        struct Block {
            enum class Type { Heading, Paragraph, Bullet, Code, Table, Quote, Rule };
            Type Kind = Type::Paragraph;
            int Level = 0;                  // Heading: 1..6; Bullet: nivel de indentacao
            std::string Marker;             // Bullet: "-" ou "1."
            std::string Text;               // Heading/Paragraph/Bullet/Quote (ja limpo)
            std::vector<std::string> Lines; // Code
            std::vector<std::vector<std::string>> Rows; // Table (linha 0 = cabecalho)
            std::vector<std::pair<std::string, std::string>> Links; // (rotulo, destino)
            std::string Lower;              // texto minusculo para a busca
        };

        struct Doc {
            std::string FileName;
            std::string Title;
            std::vector<Block> Blocks;
            std::string Lower;              // documento inteiro, minusculo
        };

        enum class Source { InEngine, GitHub };

        void LoadDocs();
        void SetSource(Source source);
        static void ParseMarkdown(const std::string& text, Doc& doc);
        void SelectDoc(int index, bool pushHistory);
        int FindDoc(const std::string& fileName) const;
        void RenderSidebar();
        void RenderContent();
        void RenderBlock(const Block& block, int id, bool highlighted);

        bool m_Open = false;
        bool m_FocusRequested = false;
        bool m_Loaded = false;
        std::filesystem::path m_Directory;   // pasta docs/ (raiz)
        Source m_Source = Source::InEngine;
        std::vector<Doc> m_Docs;
        int m_Current = -1;
        std::vector<int> m_History;
        char m_Search[128] = {};
        bool m_ScrollToTop = false;
    };

}
