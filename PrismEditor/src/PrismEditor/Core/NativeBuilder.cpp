#include "NativeBuilder.h"
#include "BuildOutputAnalysis.h"
#include "VisualStudioSolution.h"

#include <Prism/Core/Paths.h>

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
        m_ExtraArgs.clear();
        m_IsExtension = false;
        m_ConfigureOnly = false;

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

    bool NativeBuilder::StartExtension(const std::filesystem::path& extensionDirectory, const std::filesystem::path& sdkFile, std::string& error) {
        if (m_Running.load()) {
            error = "Ja existe uma compilacao em andamento.";
            return false;
        }
        std::error_code ec;
        if (!std::filesystem::exists(extensionDirectory / "CMakeLists.txt", ec)) {
            error = "A extensao nao tem CMakeLists.txt (so extensoes com codigo-fonte podem ser compiladas aqui).";
            return false;
        }
        if (!std::filesystem::exists(sdkFile, ec)) {
            error = "PrismSDK.cmake nao encontrado ao lado do editor (" + sdkFile.generic_string() + "). Recompile a engine.";
            return false;
        }
        if (m_Thread.joinable())
            m_Thread.join();

#ifdef NDEBUG
        const char* config = "Release";
#else
        const char* config = "Debug";
#endif
        m_SourceDir = extensionDirectory.generic_string();
        m_BuildDir = (extensionDirectory / "build").generic_string();
        m_Config = config;
        m_BinariesDir.clear();
        m_ExtraArgs = " -DPRISM_SDK_FILE=\"" + sdkFile.generic_string() + "\"";
        m_IsExtension = true;
        m_ConfigureOnly = false;
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

    // Configuracao do editor: o ABI da STL difere entre Debug e Release (MSVC), e a solucao so oferece essa.
    static const char* EditorConfiguration() {
#ifdef NDEBUG
        return "Release";
#else
        return "Debug";
#endif
    }

    bool NativeBuilder::BeginJob(std::string& error) {
        if (m_Running.load()) {
            error = "Ja existe uma compilacao em andamento.";
            return false;
        }
        if (m_Thread.joinable())
            m_Thread.join(); // thread do trabalho anterior, ja terminada
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Lines.clear();
            m_Finished = false;
            m_Success = false;
            m_SolutionPath.clear();
        }
        return true;
    }

    std::filesystem::path NativeBuilder::GetSolutionDirectory(const Prism::Project& project) {
        return project.GetProjectDirectory() / "Intermediate" / "VisualStudio";
    }

    std::filesystem::path NativeBuilder::GetSolutionPath() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_SolutionPath;
    }

    bool NativeBuilder::StartSolution(const Prism::Project& project, std::string& error) {
        if (m_Running.load()) {
            error = "Ja existe uma compilacao em andamento.";
            return false;
        }
        const std::filesystem::path sourceDirectory = Prism::NativeScripting::GetSourceDirectory(project);
        std::error_code ec;
        if (!std::filesystem::exists(sourceDirectory / "CMakeLists.txt", ec)) {
            error = "Source/CMakeLists.txt nao existe. Gere o projeto de scripts C++ primeiro.";
            return false;
        }
        if (!BeginJob(error))
            return false;

        m_SourceDir = sourceDirectory.generic_string();
        m_BuildDir = GetSolutionDirectory(project).generic_string();
        m_Config = EditorConfiguration();
        m_BinariesDir = project.GetProjectDirectory() / "Binaries";
        m_ExtraArgs.clear();
        m_IsExtension = false;
        m_ConfigureOnly = true;
        m_Running.store(true);
        m_Thread = std::thread(&NativeBuilder::Run, this);
        return true;
    }

    bool NativeBuilder::StartExtensionSolution(const std::filesystem::path& extensionDirectory, const std::filesystem::path& sdkFile, std::string& error) {
        if (m_Running.load()) {
            error = "Ja existe uma compilacao em andamento.";
            return false;
        }
        std::error_code ec;
        if (!std::filesystem::exists(extensionDirectory / "CMakeLists.txt", ec)) {
            error = "A extensao nao tem CMakeLists.txt (so extensoes com codigo-fonte tem solucao).";
            return false;
        }
        if (!std::filesystem::exists(sdkFile, ec)) {
            error = "PrismSDK.cmake nao encontrado ao lado do editor (" + sdkFile.generic_string() + "). Recompile a engine.";
            return false;
        }
        if (!BeginJob(error))
            return false;

        m_SourceDir = extensionDirectory.generic_string();
        // Dentro de build/: pasta que a exportacao do jogo ja ignora e que o .gitignore das extensoes costuma cobrir.
        m_BuildDir = (extensionDirectory / "build" / "VisualStudio").generic_string();
        m_Config = EditorConfiguration();
        m_BinariesDir.clear();
        m_ExtraArgs = " -DPRISM_SDK_FILE=\"" + sdkFile.generic_string() + "\"";
        // Para o F5 do Visual Studio (ver o CMakeLists.txt do modelo de extensao): este editor e o projeto aberto.
        const std::filesystem::path editorExe = Prism::GetExecutablePath();
        if (!editorExe.empty()) {
            m_ExtraArgs += " -DPRISM_EDITOR_EXE=\"" + editorExe.generic_string() + "\""
                         + " -DPRISM_EDITOR_DIR=\"" + editorExe.parent_path().generic_string() + "\"";
        }
        if (auto project = Prism::Project::GetActive())
            m_ExtraArgs += " -DPRISM_PROJECT_FILE=\"" + project->GetProjectFilePath().generic_string() + "\"";
        m_IsExtension = true;
        m_ConfigureOnly = true;
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
            (m_IsExtension ? std::string() : " -DPRISM_BUILD_ID=" + std::to_string(id)) + m_ExtraArgs +
            " 2>&1 && cmake --build \"" + m_BuildDir + "\" --config " + m_Config + " 2>&1";
#if defined(_WIN32)
        command = "\"" + command + "\""; // cmd /c remove o primeiro e o ultimo '"'
#endif
        return command;
    }

    std::string NativeBuilder::MakeSolutionCommand(const std::string& generator) const {
        // -A x64: a engine e 64 bits. CMAKE_CONFIGURATION_TYPES: so a configuracao do editor (ver EditorConfiguration).
        std::string command =
            "cmake -S \"" + m_SourceDir + "\" -B \"" + m_BuildDir + "\"" +
            (generator.empty() ? std::string() : " -G \"" + generator + "\"") +
            " -A x64 -DCMAKE_CONFIGURATION_TYPES=" + m_Config + m_ExtraArgs + " 2>&1";
#if defined(_WIN32)
        command = "\"" + command + "\""; // cmd /c remove o primeiro e o ultimo '"'
#endif
        return command;
    }

    std::string NativeBuilder::CaptureCommand(const std::string& command) {
        std::string output;
        FILE* pipe = PRISM_POPEN(command.c_str(), "r");
        if (!pipe)
            return output;
        char buffer[2048];
        while (std::fgets(buffer, sizeof(buffer), pipe))
            output += buffer;
        PRISM_PCLOSE(pipe);
        return output;
    }

    NativeBuilder::CommandResult NativeBuilder::RunCommand(const std::string& command, std::vector<std::string>& outputLines) {
        FILE* pipe = PRISM_POPEN(command.c_str(), "r");
        if (!pipe) {
            Push("Nao foi possivel executar o cmake (esta instalado e no PATH?).", true);
            return CommandResult::CouldNotStart;
        }
        char buffer[2048];
        std::string pending;
        auto flush = [&]() {
            while (!pending.empty() && (pending.back() == '\n' || pending.back() == '\r'))
                pending.pop_back();
            bool isError = pending.find("error") != std::string::npos || pending.find("Error") != std::string::npos
                || pending.find("erro") != std::string::npos;
            outputLines.push_back(pending);
            Push(pending, isError);
            pending.clear();
        };
        while (std::fgets(buffer, sizeof(buffer), pipe)) {
            pending += buffer;
            if (!pending.empty() && pending.back() == '\n')
                flush();
        }
        if (!pending.empty())
            flush();
        return PRISM_PCLOSE(pipe) == 0 ? CommandResult::Succeeded : CommandResult::Failed;
    }

    // Gera a .sln: so "cmake configure" com o gerador do Visual Studio. A pasta de saida e so da solucao
    // (nada mais mora nela), entao o CMakeCache.txt e apagado antes de cada tentativa: um cache de outro
    // gerador faria o CMake recusar ("does not match the generator used previously").
    void NativeBuilder::RunSolution() {
        namespace fs = std::filesystem;
        bool success = false;

        const VisualStudio::GeneratorChoice choice = VisualStudio::ChooseGenerators(CaptureCommand("cmake --help 2>&1"));
        if (!choice.Error.empty()) {
            Push(choice.Error, true);
        }
        else {
            for (size_t i = 0; i < choice.Candidates.size() && !success; ++i) {
                const std::string& generator = choice.Candidates[i];
                std::error_code ec;
                fs::create_directories(fs::path(m_BuildDir), ec);
                fs::remove(fs::path(m_BuildDir) / "CMakeCache.txt", ec);
                fs::remove_all(fs::path(m_BuildDir) / "CMakeFiles", ec);

                Push(generator.empty() ? "Gerando a solucao do Visual Studio (gerador padrao do CMake)..."
                                       : "Gerando a solucao com '" + generator + "'...", false);
                std::vector<std::string> output;
                const CommandResult result = RunCommand(MakeSolutionCommand(generator), output);
                if (result == CommandResult::Succeeded)
                    success = true;
                else if (result == CommandResult::CouldNotStart)
                    break;
                else if (i + 1 < choice.Candidates.size())
                    Push("Falhou (esse Visual Studio provavelmente nao esta instalado); tentando o proximo...", false);
            }
        }

        fs::path solution;
        if (success) {
            solution = VisualStudio::FindSolutionFile(fs::path(m_BuildDir));
            if (solution.empty()) {
                Push("O CMake terminou, mas nenhuma .sln/.slnx apareceu em " + m_BuildDir + ".", true);
                success = false;
            }
            else {
                Push("Solucao gerada: " + solution.generic_string(), false);
                Push(std::string("Use a configuracao '") + m_Config + "' (a mesma deste editor).", false);
                if (!m_IsExtension)
                    Push("Dica: F5 no Visual Studio abre o editor com este projeto, e os breakpoints dos scripts funcionam.", false);
            }
        }

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_SolutionPath = success ? solution : fs::path();
        m_Success = success;
        m_Finished = true;
        m_Running.store(false);
    }

    void NativeBuilder::Run() {
        if (m_ConfigureOnly) {
            RunSolution();
            return;
        }
        constexpr int kMaxAttempts = 2; // 1 repeticao automatica se o erro for de arquivo travado
        bool success = false;

        for (int attempt = 1; attempt <= kMaxAttempts && !success; attempt++) {
            // .pdb de compilacoes anteriores: apaga os que nao estao travados (os travados ficam para a proxima).
            std::error_code ec;
            if (!m_IsExtension)
            for (auto& entry : std::filesystem::directory_iterator(m_BinariesDir, ec)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("NativeScripts_", 0) == 0 && entry.path().extension() == ".pdb") {
                    std::error_code ignored;
                    std::filesystem::remove(entry.path(), ignored);
                }
            }

            std::vector<std::string> outputLines;
            const CommandResult result = RunCommand(MakeCommand(), outputLines);
            if (result == CommandResult::CouldNotStart)
                break;
            success = result == CommandResult::Succeeded;
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
