#include "NativeBuilder.h"
#include "BuildOutputAnalysis.h"

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <thread>

#if defined(_WIN32)
    #define PRISM_POPEN _popen
    #define PRISM_PCLOSE _pclose
#else
    #define PRISM_POPEN popen
    #define PRISM_PCLOSE pclose
#endif

namespace PrismEditor {

    NativeBuilder::~NativeBuilder() {
        if (m_Thread.joinable())
            m_Thread.join(); // espera o cmake terminar (poucos segundos a minutos)
    }

    bool NativeBuilder::Start(const Prism::Project& project, std::string& error) {
        if (m_Running.load()) {
            error = "Ja existe uma compilacao em andamento.";
            return false;
        }

        std::filesystem::path sourceDirectory = Prism::NativeScripting::GetSourceDirectory(project);
        if (!std::filesystem::exists(sourceDirectory / "CMakeLists.txt")) {
            error = "Source/CMakeLists.txt nao existe. Gere o projeto de scripts C++ primeiro.";
            return false;
        }

        if (m_Thread.joinable())
            m_Thread.join(); // thread da compilacao anterior, ja terminada

        // Mesma configuracao do editor: o ABI da STL difere entre Debug e Release (MSVC).
#ifdef NDEBUG
        const char* config = "Release";
#else
        const char* config = "Debug";
#endif
        std::filesystem::path buildDirectory = project.GetProjectDirectory() / "Cache" / "NativeBuild";
        m_SourceDir = sourceDirectory.generic_string();
        m_BuildDir = buildDirectory.generic_string();
        m_Config = config;
        m_BinariesDir = project.GetProjectDirectory() / "Binaries";

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Lines.clear();
            m_Finished = false;
            m_Success = false;
        }
        m_Running.store(true);
        m_Thread = std::thread(&NativeBuilder::Run, this);
        return true;
    }

    void NativeBuilder::Push(std::string text, bool isError) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Lines.push_back({ std::move(text), isError });
    }

    // Cada tentativa recebe um PRISM_BUILD_ID novo (ver prism_configure_module em NativeScripting.cpp):
    // o .pdb do modulo muda de nome e nao colide com um .pdb antigo ainda travado.
    std::string NativeBuilder::MakeCommand() const {
        static uint64_t s_LastId = 0;
        uint64_t id = (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (id <= s_LastId)
            id = s_LastId + 1;
        s_LastId = id;

        std::string command =
            "cmake -S \"" + m_SourceDir + "\" -B \"" + m_BuildDir + "\" -DCMAKE_BUILD_TYPE=" + m_Config +
            " -DPRISM_BUILD_ID=" + std::to_string(id) +
            " 2>&1 && cmake --build \"" + m_BuildDir + "\" --config " + m_Config + " 2>&1";
#if defined(_WIN32)
        command = "\"" + command + "\""; // cmd /c remove o primeiro e o ultimo '"'
#endif
        return command;
    }

    void NativeBuilder::Run() {
        constexpr int kMaxAttempts = 2; // 1 repeticao automatica se o erro for de arquivo travado
        bool success = false;

        for (int attempt = 1; attempt <= kMaxAttempts && !success; attempt++) {
            // .pdb de compilacoes anteriores: apaga os que nao estao travados (os travados ficam para a proxima).
            std::error_code ec;
            for (auto& entry : std::filesystem::directory_iterator(m_BinariesDir, ec)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("NativeScripts_", 0) == 0 && entry.path().extension() == ".pdb") {
                    std::error_code ignored;
                    std::filesystem::remove(entry.path(), ignored);
                }
            }

            std::vector<std::string> outputLines;
            FILE* pipe = PRISM_POPEN(MakeCommand().c_str(), "r");
            if (!pipe) {
                Push("Nao foi possivel executar o cmake (esta instalado e no PATH?).", true);
                break;
            }
            char buffer[2048];
            std::string pending;
            auto flush = [&](bool terminated) {
                while (!pending.empty() && (pending.back() == '\n' || pending.back() == '\r'))
                    pending.pop_back();
                bool isError = pending.find("error") != std::string::npos || pending.find("Error") != std::string::npos
                    || pending.find("erro") != std::string::npos;
                outputLines.push_back(pending);
                Push(pending, isError);
                pending.clear();
                (void)terminated;
            };
            while (std::fgets(buffer, sizeof(buffer), pipe)) {
                pending += buffer;
                if (!pending.empty() && pending.back() == '\n')
                    flush(true);
            }
            if (!pending.empty())
                flush(false);
            success = PRISM_PCLOSE(pipe) == 0;
            if (success)
                break;

            const BuildAnalysis::Failure failure = BuildAnalysis::Classify(outputLines);
            if (failure == BuildAnalysis::Failure::FileLocked && attempt < kMaxAttempts) {
                Push("Arquivo do modulo travado por outro processo; tentando de novo em 2 s (com um .pdb novo)...", false);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            const std::string hint = BuildAnalysis::Hint(failure);
            if (!hint.empty())
                Push(hint, true);
            break;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Success = success;
        m_Finished = true;
        m_Running.store(false);
    }

    void NativeBuilder::Poll(std::vector<Line>& lines, bool& justFinished, bool& success) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        lines.insert(lines.end(), m_Lines.begin(), m_Lines.end());
        m_Lines.clear();
        justFinished = m_Finished;
        success = m_Success;
        m_Finished = false;
    }

}
