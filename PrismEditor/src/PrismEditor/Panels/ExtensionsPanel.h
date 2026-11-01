#pragma once

// ============================================================================
// ExtensionsPanel.h
// Aba "Extensoes" (menu Janela): lista as extensoes (globais, ao lado do editor, e do projeto),
// com estado e motivo da falha, liga/desliga (enabled no extension.ini), compila pelo editor
// (usa a aba Saida de Build), cria uma extensao nova a partir de um modelo e mostra a pasta.
//
// Extensoes sao carregadas UMA vez, ao abrir o editor: ligar/desligar, compilar ou criar uma
// extensao so passa a valer depois de REINICIAR o editor (o painel avisa). O editor abre uma
// COPIA da biblioteca (ver ExtensionLoader.cpp), entao compilar com o editor aberto nao e
// bloqueado pelo Windows.
// ============================================================================

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace PrismEditor {

    class ExtensionsPanel {
    public:
        using CompileFn = std::function<void(const std::filesystem::path& extensionDirectory)>;
        using OpenFileFn = std::function<void(const std::filesystem::path& file)>;

        void SetOnCompile(CompileFn fn) { m_OnCompile = std::move(fn); }
        void SetOnOpenFile(OpenFileFn fn) { m_OnOpenFile = std::move(fn); }
        // true enquanto uma compilacao esta rodando (desabilita os botoes Compilar).
        void SetBusy(bool busy) { m_Busy = busy; }
        // Chamado pelo EditorLayer quando a compilacao de uma extensao termina.
        void NotifyCompiled(const std::filesystem::path& extensionDirectory, bool success);

        void Toggle() { m_Open = !m_Open; m_NeedsRescan = true; }
        bool IsOpen() const { return m_Open; }

        void OnImGuiRender();

    private:
        enum class Status { Loaded, Failed, Skipped, NotLoaded };

        struct Entry {
            std::string Name;
            std::filesystem::path Directory;
            bool FromProject = false;
            Status State = Status::NotLoaded;
            std::string Message;
            std::string Version, Author, Description, Target;
            bool Enabled = true;
            bool ManifestValid = false;
            bool HasCMake = false;
            bool HasBinary = false;
        };

        void Rescan();
        bool SetEnabled(const Entry& entry, bool enabled);
        void RenderEntry(Entry& entry, int index);
        void RenderCreatePopup();

        std::vector<Entry> m_Entries;
        bool m_Open = false;
        bool m_NeedsRescan = true;
        bool m_Busy = false;
        bool m_RestartNeeded = false;
        bool m_OpenCreatePopup = false;
        char m_NewName[64] = {};
        std::string m_PopupError;
        std::string m_LastBuildMessage;
        bool m_LastBuildOk = true;

        CompileFn m_OnCompile;
        OpenFileFn m_OnOpenFile;
    };

}
