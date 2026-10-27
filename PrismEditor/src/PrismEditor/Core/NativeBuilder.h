#pragma once

// ============================================================================
// NativeBuilder.h
// Compila o modulo de scripts C++ do projeto (<Projeto>/Source) chamando o
// CMake numa thread de fundo, sem travar o editor. A saida do compilador e
// entregue linha a linha para a thread principal (Poll), que a escreve no
// Console - o Log da engine nao e thread-safe, entao a thread de fundo nunca
// chama o Log.
// ============================================================================

#include <Prism.h>
#include <Prism/Scripting/NativeScripting.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace PrismEditor {

    class NativeBuilder {
    public:
        struct Line {
            std::string Text;
            bool IsError = false;
        };

        NativeBuilder() = default;
        ~NativeBuilder();
        NativeBuilder(const NativeBuilder&) = delete;
        NativeBuilder& operator=(const NativeBuilder&) = delete;

        // Inicia "cmake configure + build". false (com 'error') se ja ha uma
        // compilacao em andamento ou se Source/CMakeLists.txt nao existe.
        bool Start(const Prism::Project& project, std::string& error);

        bool IsRunning() const { return m_Running.load(); }

        // Chamar todo frame na thread principal: recolhe as linhas novas e diz
        // se a compilacao terminou desde o ultimo Poll (e se deu certo).
        void Poll(std::vector<Line>& lines, bool& justFinished, bool& success);

    private:
        void Run();
        std::string MakeCommand() const; // um PRISM_BUILD_ID novo a cada chamada
        void Push(std::string text, bool isError);

        std::thread m_Thread;
        std::string m_SourceDir, m_BuildDir, m_Config;
        std::filesystem::path m_BinariesDir;
        std::atomic<bool> m_Running{ false };
        std::mutex m_Mutex;
        std::vector<Line> m_Lines;
        bool m_Finished = false;
        bool m_Success = false;
    };

}
