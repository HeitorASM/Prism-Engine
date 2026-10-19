#pragma once

// ============================================================================
// BuildOutputPanel.h
// Painel "Saida de Build": mostra a saida da compilacao dos scripts C++ (o
// que o NativeBuilder le do cmake/compilador), separada do Console geral.
//
// Cada linha e classificada como erro, aviso ou texto normal. Linhas de
// diagnostico do compilador com arquivo e linha ("foo.cpp(12,5): error C2065"
// do MSVC ou "foo.cpp:12:5: error: ..." do GCC/Clang) viram links: clicar
// abre o arquivo no Editor de Codigo (via o callback de EditorLayer).
//
// Observacao: ainda nao pula para a LINHA do erro - o ScriptEditorPanel nao
// tem uma API de "ir para a linha" ainda. A linha aparece no tooltip.
// ============================================================================

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace PrismEditor {

    class BuildOutputPanel {
    public:
        // Chamado ao clicar num erro/aviso com arquivo existente.
        void SetOnOpenFile(std::function<void(const std::filesystem::path&)> callback) { m_OnOpenFile = std::move(callback); }

        // Pasta a que caminhos RELATIVOS dos diagnosticos se referem (Source/ do projeto).
        void SetBaseDirectory(const std::filesystem::path& directory) { m_BaseDirectory = directory; }

        void BeginBuild();                      // limpa, marca "compilando" e traz o painel para a frente
        void AddLine(const std::string& text);
        void EndBuild(bool success);            // em falha, traz o painel para a frente

        void Clear();

        void OnImGuiRender();

    private:
        enum class Severity { Normal, Warning, Error };

        struct Entry {
            std::string Text;
            Severity Level = Severity::Normal;
            std::filesystem::path File;  // vazio = sem link
            int Line = 0;
        };

        static Entry Classify(const std::string& text);
        std::filesystem::path ResolveFile(const std::filesystem::path& file) const;

        std::vector<Entry> m_Entries;
        int m_ErrorCount = 0;
        int m_WarningCount = 0;

        bool m_Building = false;
        bool m_HasResult = false;
        bool m_LastSuccess = false;
        bool m_FocusRequested = false;

        bool m_OnlyProblems = false;
        bool m_AutoScroll = true;

        std::filesystem::path m_BaseDirectory;
        std::function<void(const std::filesystem::path&)> m_OnOpenFile;
    };

}
