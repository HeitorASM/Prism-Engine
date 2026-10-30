#pragma once

// ============================================================================
// GameExportRunner.h
// Roda Prism::ExportGame numa thread de fundo (copiar centenas de MB trava o
// editor se for feito no frame). Mesmo padrao do NativeBuilder: a thread so
// empilha linhas de texto; quem le (Poll) e a thread principal, que as escreve
// na aba "Saida de Build". A thread de fundo nunca chama o Log da engine.
// ============================================================================

#include <Prism/Project/GameExporter.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace PrismEditor {

    class GameExportRunner {
    public:
        struct Line {
            std::string Text;
            bool IsError = false;
        };

        GameExportRunner() = default;
        ~GameExportRunner();
        GameExportRunner(const GameExportRunner&) = delete;
        GameExportRunner& operator=(const GameExportRunner&) = delete;

        // Inicia a exportacao. false (com 'error') se ja ha uma em andamento.
        bool Start(Prism::GameExportOptions options, std::string& error);

        bool IsRunning() const { return m_Running.load(); }

        // Chamar todo frame na thread principal: recolhe as linhas novas e diz se a
        // exportacao terminou desde o ultimo Poll (e se deu certo). 'outputDirectory'
        // so vale quando 'justFinished' e true.
        void Poll(std::vector<Line>& lines, bool& justFinished, bool& success, std::filesystem::path& outputDirectory);

    private:
        void Run(Prism::GameExportOptions options);
        void Push(std::string text, bool isError);

        std::thread m_Thread;
        std::atomic<bool> m_Running{ false };
        std::mutex m_Mutex;
        std::vector<Line> m_Lines;
        bool m_Finished = false;
        bool m_Success = false;
        std::filesystem::path m_OutputDirectory;
    };

}
