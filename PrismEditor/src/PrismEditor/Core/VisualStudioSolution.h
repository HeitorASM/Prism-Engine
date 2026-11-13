#pragma once

// ============================================================================
// VisualStudioSolution.h
// Apoio a "Gerar solucao do Visual Studio" (scripts C++ e extensoes): escolhe o gerador do
// CMake a usar e acha a .sln gerada. So usa a std (testavel sem engine, ver
// tests/VisualStudioSolutionTests.cpp).
//
// A solucao e gerada pelo proprio CMake (cmake -S Source -B <pasta> -G "Visual Studio ..."):
// o projeto de scripts continua sendo o CMakeLists.txt de Source/, a .sln e so uma "vista" dele
// (descartavel; gerar de novo a recria). Por isso ela mora em <Projeto>/Intermediate/ e nao
// em Source/.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace PrismEditor::VisualStudio {

    // Primeiro versao do Visual Studio que a engine aceita como gerador (VS 2019 = "Visual Studio 16 2019"):
    // C++20 e a opcao -A x64 pedem isso. Geradores mais antigos da lista do CMake sao ignorados.
    constexpr int kMinimumGeneratorMajor = 16;

    struct GeneratorChoice {
        // Geradores a tentar, em ordem. "" = o gerador padrao do CMake (que ja e o Visual Studio mais
        // novo instalado). Vazio quando 'Error' esta preenchido.
        std::vector<std::string> Candidates;
        std::string Error;
    };

    namespace detail {
        inline std::string Trim(const std::string& s) {
            size_t b = 0, e = s.size();
            while (b < e && std::isspace((unsigned char)s[b])) b++;
            while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
            return s.substr(b, e - b);
        }

        // "Visual Studio 17 2022" -> 17; -1 se a linha nao e de um gerador do VS ("Visual Studio <N> <AAAA>").
        inline int VisualStudioMajor(const std::string& name) {
            const std::string prefix = "Visual Studio ";
            if (name.compare(0, prefix.size(), prefix) != 0) return -1;
            size_t i = prefix.size();
            int major = 0;
            size_t digits = 0;
            while (i < name.size() && std::isdigit((unsigned char)name[i])) { major = major * 10 + (name[i] - '0'); i++; digits++; }
            if (digits == 0 || major > 1000) return -1;
            if (i >= name.size() || name[i] != ' ') return -1;
            return major;
        }

        struct ListedGenerator {
            std::string Name;
            bool IsDefault = false;
        };

        // Linhas da secao "Generators" de `cmake --help`:
        //   * Visual Studio 17 2022        = Generates Visual Studio 2022 project files.
        //     Visual Studio 16 2019        = Generates Visual Studio 2019 project files.
        //     Ninja                        = Generates build.ninja files.
        // O '*' marca o padrao. Linhas de continuacao (mais indentadas) e o texto depois do '=' sao ignorados.
        inline std::vector<ListedGenerator> ParseGeneratorList(const std::string& helpText) {
            std::vector<ListedGenerator> result;
            std::istringstream in(helpText);
            std::string line;
            bool inSection = false;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!inSection) {
                    // "The following generators are available on this platform (* marks default):"
                    if (line.find("generators are available") != std::string::npos) inSection = true;
                    continue;
                }
                if (line.empty()) continue;
                // Entradas: 2 colunas de marca ("* " = padrao, "  " = outro) e o nome. Continuacoes
                // tem indentacao bem maior (coluna 2 vazia). Uma linha que comeca no texto (ex:
                // "Extra generators") encerra a secao.
                const bool isDefault = line[0] == '*';
                if (line[0] != '*' && line[0] != ' ')
                    break;
                if (line.size() < 3 || line[1] != ' ' || line[2] == ' ')
                    continue; // continuacao (ou linha curta demais)
                std::string rest = line.substr(2);
                const size_t eq = rest.find('=');
                std::string name = Trim(eq == std::string::npos ? rest : rest.substr(0, eq));
                // "Visual Studio 14 2015 [arch]" -> sem o sufixo.
                const size_t bracket = name.find(" [");
                if (bracket != std::string::npos) name = Trim(name.substr(0, bracket));
                if (name.empty()) continue;
                result.push_back({ name, isDefault });
            }
            return result;
        }
    }

    // Decide quais geradores tentar para gerar a .sln a partir da saida de `cmake --help`.
    //  - o padrao do CMake ja e um Visual Studio (o caso normal no Windows): so ele ("" = nao passar -G);
    //  - senao (ex: variavel CMAKE_GENERATOR=Ninja): os Visual Studio da lista, do mais novo para o mais
    //    antigo, ate um funcionar (a lista do CMake traz tambem versoes que nao estao instaladas).
    inline GeneratorChoice ChooseGenerators(const std::string& cmakeHelpText) {
        GeneratorChoice choice;
        const auto listed = detail::ParseGeneratorList(cmakeHelpText);

        for (const auto& g : listed) {
            if (g.IsDefault && detail::VisualStudioMajor(g.Name) >= kMinimumGeneratorMajor) {
                choice.Candidates.push_back(""); // usa o padrao
                return choice;
            }
        }

        std::vector<std::pair<int, std::string>> found;
        for (const auto& g : listed) {
            const int major = detail::VisualStudioMajor(g.Name);
            if (major >= kMinimumGeneratorMajor) found.emplace_back(major, g.Name);
        }
        std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (auto& f : found) choice.Candidates.push_back(f.second);

        if (choice.Candidates.empty())
            choice.Error = "Este CMake nao tem gerador do Visual Studio 2019 ou mais novo (cmake --help). "
                           "Gerar a solucao so funciona no Windows com o CMake do Visual Studio ou o CMake oficial.";
        return choice;
    }

    // Primeira solucao direta em 'directory' (o CMake gera uma por projeto raiz). Vazio se nao houver.
    // Aceita .sln e .slnx (o Visual Studio 2026 / CMake novo gera .slnx); .sln tem preferencia.
    inline std::filesystem::path FindSolutionFile(const std::filesystem::path& directory) {
        std::error_code ec;
        std::vector<std::filesystem::path> found;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            std::error_code fileError;
            if (!entry.is_regular_file(fileError)) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (ext == ".sln" || ext == ".slnx") found.push_back(entry.path());
        }
        if (found.empty()) return {};
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
            const bool aNew = a.extension() != ".sln" && a.extension() != ".SLN"; // .slnx depois de .sln
            const bool bNew = b.extension() != ".sln" && b.extension() != ".SLN";
            if (aNew != bNew) return !aNew;
            return a < b;
        });
        return found.front();
    }

}
