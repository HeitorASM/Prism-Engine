#include "GameExportRunner.h"

#include <utility>

namespace PrismEditor {

    GameExportRunner::~GameExportRunner() {
        if (m_Thread.joinable())
            m_Thread.join(); // a copia termina sozinha; esperar evita acessar membros destruidos
    }

    bool GameExportRunner::Start(Prism::GameExportOptions options, std::string& error) {
        if (m_Running.load()) {
            error = "Ja existe uma exportacao em andamento.";
            return false;
        }
        if (m_Thread.joinable())
            m_Thread.join(); // thread da exportacao anterior, ja terminada

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Lines.clear();
            m_Finished = false;
            m_Success = false;
            m_OutputDirectory = options.OutputDirectory;
        }
        m_Running.store(true);
        m_Thread = std::thread(&GameExportRunner::Run, this, std::move(options));
        return true;
    }

    void GameExportRunner::Push(std::string text, bool isError) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Lines.push_back({ std::move(text), isError });
    }

    void GameExportRunner::Run(Prism::GameExportOptions options) {
        Prism::GameExportResult result = Prism::ExportGame(options, [this](const std::string& text) {
            Push(text, text.rfind("ERRO:", 0) == 0);
        });

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Success = result.Success;
        m_Finished = true;
        m_Running.store(false);
    }

    void GameExportRunner::Poll(std::vector<Line>& lines, bool& justFinished, bool& success, std::filesystem::path& outputDirectory) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        lines.insert(lines.end(), m_Lines.begin(), m_Lines.end());
        m_Lines.clear();
        justFinished = m_Finished;
        success = m_Success;
        outputDirectory = m_OutputDirectory;
        m_Finished = false;
    }

}
