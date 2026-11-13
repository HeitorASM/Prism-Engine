#include <PrismEditor/Core/VisualStudioSolution.h>
#include <cstdio>
#include <fstream>

using namespace PrismEditor::VisualStudio;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Trecho real de `cmake --help` (CMake 3.31, Windows) com o VS 2022 como padrao.
static const char* kHelpVsDefault =
    "Usage\n\n  cmake [options] <path-to-source>\n\n"
    "Generators\n\n"
    "The following generators are available on this platform (* marks default):\n"
    "* Visual Studio 17 2022        = Generates Visual Studio 2022 project files.\n"
    "                                 Use -A option to specify architecture.\n"
    "  Visual Studio 16 2019        = Generates Visual Studio 2019 project files.\n"
    "                                 Use -A option to specify architecture.\n"
    "  Visual Studio 15 2017 [arch] = Deprecated.  Generates Visual Studio 2017\n"
    "                                 project files.  Optional [arch] can be\n"
    "                                 \"Win64\" or \"ARM\".\n"
    "  Visual Studio 14 2015 [arch] = Generates Visual Studio 2015 project files.\n"
    "  Borland Makefiles            = Generates Borland makefiles.\n"
    "  Ninja                        = Generates build.ninja files.\n"
    "  Ninja Multi-Config           = Generates build-<Config>.ninja files.\n"
    "  NMake Makefiles              = Generates NMake makefiles.\n"
    "  Unix Makefiles               = Generates standard UNIX makefiles.\n\n"
    "Extra generators\n";

// Padrao trocado por CMAKE_GENERATOR=Ninja: a lista traz VS 18/17/16, so alguns instalados.
static const char* kHelpNinjaDefault =
    "Generators\n\n"
    "The following generators are available on this platform (* marks default):\n"
    "  Visual Studio 16 2019        = Generates Visual Studio 2019 project files.\n"
    "  Visual Studio 18 2026        = Generates Visual Studio 2026 project files.\n"
    "  Visual Studio 17 2022        = Generates Visual Studio 2022 project files.\n"
    "  Visual Studio 15 2017        = Deprecated.\n"
    "* Ninja                        = Generates build.ninja files.\n"
    "  Unix Makefiles               = Generates standard UNIX makefiles.\r\n";

static const char* kHelpLinux =
    "The following generators are available on this platform (* marks default):\n"
    "* Unix Makefiles               = Generates standard UNIX makefiles.\n"
    "  Ninja                        = Generates build.ninja files.\n";

int main() {
    // --- ParseGeneratorList ---------------------------------------------------
    {
        const auto list = detail::ParseGeneratorList(kHelpVsDefault);
        CHECK(list.size() >= 9);
        CHECK(list[0].Name == "Visual Studio 17 2022" && list[0].IsDefault);
        CHECK(list[1].Name == "Visual Studio 16 2019" && !list[1].IsDefault);
        CHECK(list[2].Name == "Visual Studio 15 2017"); // "[arch]" removido, linhas de continuacao ignoradas
        CHECK(list[3].Name == "Visual Studio 14 2015");
        bool sawExtraHeader = false;
        for (const auto& g : list) if (g.Name.find("Extra") != std::string::npos) sawExtraHeader = true;
        CHECK(!sawExtraHeader); // para ao sair da secao
        CHECK(detail::ParseGeneratorList("").empty());
        CHECK(detail::ParseGeneratorList("sem secao de geradores\n  Visual Studio 17 2022 = x\n").empty());
    }

    // --- VisualStudioMajor ----------------------------------------------------
    CHECK(detail::VisualStudioMajor("Visual Studio 17 2022") == 17);
    CHECK(detail::VisualStudioMajor("Visual Studio 9 2008") == 9);
    CHECK(detail::VisualStudioMajor("Visual Studio") == -1);
    CHECK(detail::VisualStudioMajor("Visual Studio X 2022") == -1);
    CHECK(detail::VisualStudioMajor("Ninja") == -1);
    CHECK(detail::VisualStudioMajor("Visual Studio 17") == -1);

    // --- ChooseGenerators -----------------------------------------------------
    {
        const GeneratorChoice c = ChooseGenerators(kHelpVsDefault);
        CHECK(c.Error.empty());
        CHECK(c.Candidates.size() == 1 && c.Candidates[0].empty()); // padrao ja e VS: nao passa -G
    }
    {
        const GeneratorChoice c = ChooseGenerators(kHelpNinjaDefault);
        CHECK(c.Error.empty());
        CHECK(c.Candidates.size() == 3); // 2017 fica de fora (abaixo do minimo)
        CHECK(c.Candidates[0] == "Visual Studio 18 2026"); // mais novo primeiro
        CHECK(c.Candidates[1] == "Visual Studio 17 2022");
        CHECK(c.Candidates[2] == "Visual Studio 16 2019");
    }
    {
        const GeneratorChoice c = ChooseGenerators(kHelpLinux);
        CHECK(c.Candidates.empty());
        CHECK(!c.Error.empty());
    }
    {
        const GeneratorChoice c = ChooseGenerators("");
        CHECK(c.Candidates.empty() && !c.Error.empty());
    }
    {
        // Padrao e um VS antigo (2017): nao serve; sem alternativas -> erro.
        const char* oldDefault = "The following generators are available on this platform (* marks default):\n"
                                 "* Visual Studio 15 2017        = Deprecated.\n"
                                 "  Ninja                        = Generates build.ninja files.\n";
        const GeneratorChoice c = ChooseGenerators(oldDefault);
        CHECK(c.Candidates.empty() && !c.Error.empty());
    }

    // --- FindSolutionFile -----------------------------------------------------
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "prism_vs_solution_test";
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "sub", ec);
        CHECK(FindSolutionFile(dir).empty());
        CHECK(FindSolutionFile(dir / "inexistente").empty());
        { std::ofstream(dir / "sub" / "Escondida.sln") << "x"; }
        CHECK(FindSolutionFile(dir).empty()); // nao recursivo
        { std::ofstream(dir / "B.SLN") << "x"; }
        { std::ofstream(dir / "A.sln") << "x"; }
        { std::ofstream(dir / "leia.txt") << "x"; }
        { std::ofstream(dir / "0.slnx") << "x"; }
        CHECK(FindSolutionFile(dir).filename() == "A.sln"); // .sln tem preferencia sobre .slnx
        { std::filesystem::remove(dir / "A.sln"); std::filesystem::remove(dir / "B.SLN"); }
        CHECK(FindSolutionFile(dir).filename() == "0.slnx"); // VS 2026: so .slnx
        { std::ofstream(dir / "A.sln") << "x"; std::ofstream(dir / "B.SLN") << "x"; std::filesystem::remove(dir / "0.slnx"); }
        CHECK(FindSolutionFile(dir).filename() == "A.sln"); // determinista; extensao sem diferenciar maiusculas
        fs::remove_all(dir, ec);
    }

    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
