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

        // Compila uma EXTENSAO (pasta com extension.ini + CMakeLists.txt, ver Prism/Extensions): mesmo
        // cmake em segundo plano, saida pelo mesmo Poll. 'sdkFile' = PrismSDK.cmake do editor.
        // O resultado (Binaries/ da extensao) so e carregado ao reiniciar o editor.
        bool StartExtension(const std::filesystem::path& extensionDirectory, const std::filesystem::path& sdkFile, std::string& error);

        // Gera (SO configura, nao compila) a solucao do Visual Studio dos scripts C++ em
        // <Projeto>/Intermediate/VisualStudio. A .sln e uma "vista" descartavel do Source/CMakeLists.txt:
        // regerar a recria. Usa o gerador do Visual Studio mais novo instalado e so a configuracao do
        // proprio editor (Debug ou Release: o ABI da STL difere entre elas). Saida pelo mesmo Poll;
        // depois de terminar com sucesso, GetSolutionPath() diz onde esta a .sln.
        bool StartSolution(const Prism::Project& project, std::string& error);

        // Mesma coisa para uma EXTENSAO (<extensao>/build/VisualStudio; essa pasta ja fica fora da exportacao).
        bool StartExtensionSolution(const std::filesystem::path& extensionDirectory, const std::filesystem::path& sdkFile, std::string& error);

        // .sln da ultima geracao bem-sucedida (vazio se nao houve). Chame depois de Poll() avisar que terminou.
        std::filesystem::path GetSolutionPath();

        // Onde StartSolution() gera/gerou a .sln de 'project' (a .sln pode ainda nao existir).
        static std::filesystem::path GetSolutionDirectory(const Prism::Project& project);

        bool IsRunning() const { return m_Running.load(); }

        // Chamar todo frame na thread principal: recolhe as linhas novas e diz
        // se a compilacao terminou desde o ultimo Poll (e se deu certo).
        void Poll(std::vector<Line>& lines, bool& justFinished, bool& success);

    private:
        enum class CommandResult { Succeeded, Failed, CouldNotStart };

        void Run();
        void RunSolution();
        std::string MakeCommand() const; // um PRISM_BUILD_ID novo a cada chamada
        std::string MakeSolutionCommand(const std::string& generator) const;
        // Roda 'command' (stderr junto) mandando cada linha para o Poll; 'outputLines' recebe as linhas.
        CommandResult RunCommand(const std::string& command, std::vector<std::string>& outputLines);
        // Roda 'command' e devolve a saida inteira (sem mandar para o Poll).
        static std::string CaptureCommand(const std::string& command);
        void Push(std::string text, bool isError);
        bool BeginJob(std::string& error);

        std::thread m_Thread;
        std::string m_SourceDir, m_BuildDir, m_Config;
        std::filesystem::path m_BinariesDir;
        std::string m_ExtraArgs;        // argumentos -D extras do cmake configure (extensoes: PRISM_SDK_FILE)
        bool m_IsExtension = false;     // compilando uma extensao (sem PRISM_BUILD_ID nem limpeza de .pdb)
        bool m_ConfigureOnly = false;   // gerar a solucao do Visual Studio (so cmake configure, sem build)
        std::filesystem::path m_SolutionPath; // protegido por m_Mutex
        std::atomic<bool> m_Running{ false };
        std::mutex m_Mutex;
        std::vector<Line> m_Lines;
        bool m_Finished = false;
        bool m_Success = false;
    };

}
