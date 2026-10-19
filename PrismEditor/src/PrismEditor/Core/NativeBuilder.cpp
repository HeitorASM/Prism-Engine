#include "NativeBuilder.h"

#include <cstdio>
#include <filesystem>

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
        std::string source = sourceDirectory.generic_string();
        std::string build = buildDirectory.generic_string();

        std::string command =
            "cmake -S \"" + source + "\" -B \"" + build + "\" -DCMAKE_BUILD_TYPE=" + config +
            " 2>&1 && cmake --build \"" + build + "\" --config " + config + " 2>&1";
#if defined(_WIN32)
        command = "\"" + command + "\""; // cmd /c remove o primeiro e o ultimo '"'
#endif

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Lines.clear();
            m_Finished = false;
            m_Success = false;
        }
        m_Running.store(true);
        m_Thread = std::thread(&NativeBuilder::Run, this, std::move(command));
        return true;
    }

    void NativeBuilder::Push(std::string text, bool isError) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Lines.push_back({ std::move(text), isError });
    }

    void NativeBuilder::Run(std::string command) {
        bool success = false;
        FILE* pipe = PRISM_POPEN(command.c_str(), "r");
        if (!pipe) {
            Push("Nao foi possivel executar o cmake (esta instalado e no PATH?).", true);
        }
        else {
            char buffer[2048];
            std::string pending;
            while (std::fgets(buffer, sizeof(buffer), pipe)) {
                pending += buffer;
                if (!pending.empty() && pending.back() == '\n') {
                    while (!pending.empty() && (pending.back() == '\n' || pending.back() == '\r'))
                        pending.pop_back();
                    bool isError = pending.find("error") != std::string::npos || pending.find("Error") != std::string::npos
                        || pending.find("erro") != std::string::npos;
                    Push(pending, isError);
                    pending.clear();
                }
            }
            if (!pending.empty())
                Push(pending, false);
            success = PRISM_PCLOSE(pipe) == 0;
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
