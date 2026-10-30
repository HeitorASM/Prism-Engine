// ============================================================================
// ExportTests.cpp
// Testes do exportador de jogos (Project/GameExporter). Sem framework externo
// (mesmo CHECK de AssetTests.cpp). Compila SO o GameExporter.cpp: sem
// OpenGL/GLFW/EnTT. Usa pastas temporarias com arquivos de mentira.
// ============================================================================
#include "Prism/Project/GameExporter.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
using namespace Prism;

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

#if defined(_WIN32)
static const char* kExe = ".exe";
static const char* kModule = ".dll";
#elif defined(__APPLE__)
static const char* kExe = "";
static const char* kModule = ".dylib";
#else
static const char* kExe = "";
static const char* kModule = ".so";
#endif

static void Touch(const fs::path& p, const std::string& content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}
static fs::path Root() { return fs::temp_directory_path() / "prism_export_tests"; }
static fs::path FreshDir(const char* name) {
    fs::path d = Root() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// Um projeto e um Runtime de mentira, com de tudo um pouco (inclusive o que NAO deve ir).
struct Fixture {
    fs::path Project, Runtime, Output;
    GameExportOptions Options;
};

static Fixture MakeFixture(const char* name, bool withModule = true) {
    Fixture f;
    fs::path base = FreshDir(name);
    f.Project = base / "Aventura";
    f.Runtime = base / "Runtime";
    f.Output = base / "Saida";

    Touch(f.Project / "Aventura.prismproj", "name=Aventura");
    Touch(f.Project / "Maps" / "Fase1.prismmap");
    Touch(f.Project / "Assets" / "Models" / "nave.glb");
    Touch(f.Project / "Assets" / "Models" / "nave.glb.meta");
    Touch(f.Project / "Assets" / "Textures" / "arte.psd");          // fonte de arte: nao vai
    Touch(f.Project / "Scripts" / "player.lua");
    Touch(f.Project / "Source" / "Spinner.cpp");                    // codigo C++: nao vai
    Touch(f.Project / "Cache" / "NativeBuild" / "lixo.o");          // cache: nao vai
    Touch(f.Project / ".git" / "HEAD");
    Touch(f.Project / ".gitignore");
    Touch(f.Project / "Maps" / "Fase1.prismmap.bak");               // backup: nao vai
    if (withModule) {
        Touch(f.Project / "Binaries" / (std::string("NativeScripts") + kModule));
        Touch(f.Project / "Binaries" / "NativeScripts.pdb");        // so o modulo vai
    }

    Touch(f.Runtime / (std::string("PrismRuntime") + kExe), "EXE");
    Touch(f.Runtime / "PrismRuntime.pdb");                          // nao vai
    Touch(f.Runtime / "assimp.dll", "DLL");
    Touch(f.Runtime / "shaders" / "basic.frag");
    Touch(f.Runtime / "fonts" / "DejaVuSans.ttf");

    f.Options.ProjectDirectory = f.Project;
    f.Options.StartMap = "Fase1.prismmap";
    f.Options.RuntimeDirectory = f.Runtime;
    f.Options.OutputDirectory = f.Output;
    f.Options.GameName = "Meu Jogo";
    f.Options.EngineVersion = "0.1.0";
    return f;
}

static bool Exists(const fs::path& p) { std::error_code ec; return fs::exists(p, ec); }

static void TestSanitize() {
    std::puts("[SanitizeGameName]");
    CHECK(SanitizeGameName("Meu Jogo") == "Meu Jogo");
    CHECK(SanitizeGameName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
    CHECK(SanitizeGameName("") == "Game");
    CHECK(SanitizeGameName("   ...  ") == "Game");
    CHECK(SanitizeGameName("  jogo. ") == "jogo");
    CHECK(SanitizeGameName("CON") == "CON_");
    CHECK(SanitizeGameName("nul.txt") == "nul.txt_");
    CHECK(SanitizeGameName("COM3") == "COM3_");
    CHECK(SanitizeGameName("COM10") == "COM10");
    CHECK(SanitizeGameName("Acao") == "Acao");

    // Limite de 64 bytes sem cortar um caractere UTF-8 ao meio: 'ã' tem 2 bytes.
    std::string longName;
    for (int i = 0; i < 40; i++) longName += "\xC3\xA3";           // 80 bytes
    std::string cut = SanitizeGameName(longName);
    CHECK(cut.size() <= 64);
    CHECK(cut.size() % 2 == 0);                                     // so caracteres inteiros
    CHECK(!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80); // termina no 2o byte de um 'ã'
    std::string odd = "x" + longName;                               // desalinha o corte
    std::string cutOdd = SanitizeGameName(odd);
    CHECK(cutOdd.size() <= 64 && ((unsigned char)cutOdd.back() & 0xC0) == 0x80);
}

static void TestHappyPath() {
    std::puts("[Exportacao basica]");
    Fixture f = MakeFixture("basico");
    std::vector<std::string> lines;
    GameExportResult r = ExportGame(f.Options, [&](const std::string& s) { lines.push_back(s); });
    CHECK(r.Success);
    CHECK(r.Error.empty());
    CHECK(r.Warnings.empty());
    CHECK(!lines.empty());

    // Runtime renomeado, sem .pdb; shaders/fonts/DLL no lugar.
    CHECK(Exists(f.Output / (std::string("Meu Jogo") + kExe)));
    CHECK(!Exists(f.Output / (std::string("PrismRuntime") + kExe)));
    CHECK(!Exists(f.Output / "PrismRuntime.pdb"));
    CHECK(Exists(f.Output / "assimp.dll"));
    CHECK(Exists(f.Output / "shaders" / "basic.frag"));
    CHECK(Exists(f.Output / "fonts" / "DejaVuSans.ttf"));
    CHECK(r.Executable == f.Output / (std::string("Meu Jogo") + kExe));

    // Projeto em Game/.
    CHECK(Exists(f.Output / "Game" / "Aventura.prismproj"));
    CHECK(Exists(f.Output / "Game" / "Maps" / "Fase1.prismmap"));
    CHECK(Exists(f.Output / "Game" / "Assets" / "Models" / "nave.glb"));
    CHECK(Exists(f.Output / "Game" / "Assets" / "Models" / "nave.glb.meta")); // .meta e necessario (AssetRegistry)
    CHECK(Exists(f.Output / "Game" / "Scripts" / "player.lua"));
    CHECK(Exists(f.Output / "Game" / "Binaries" / (std::string("NativeScripts") + kModule)));

    // O que nao deve ir.
    CHECK(!Exists(f.Output / "Game" / "Source"));
    CHECK(!Exists(f.Output / "Game" / "Cache"));
    CHECK(!Exists(f.Output / "Game" / ".git"));
    CHECK(!Exists(f.Output / "Game" / ".gitignore"));
    CHECK(!Exists(f.Output / "Game" / "Maps" / "Fase1.prismmap.bak"));
    CHECK(!Exists(f.Output / "Game" / "Assets" / "Textures" / "arte.psd"));
    CHECK(!Exists(f.Output / "Game" / "Binaries" / "NativeScripts.pdb"));

    CHECK(Exists(f.Output / ".prism-export"));
    CHECK(r.FilesCopied > 0 && r.BytesCopied > 0);
    // O projeto de origem nao foi tocado.
    CHECK(Exists(f.Project / "Source" / "Spinner.cpp"));
    CHECK(Exists(f.Project / "Cache" / "NativeBuild" / "lixo.o"));
}

static void TestReexport() {
    std::puts("[Reexportar na mesma pasta]");
    Fixture f = MakeFixture("reexport");
    CHECK(ExportGame(f.Options).Success);

    // Algo que o usuario pos na pasta de saida, e um arquivo antigo do jogo que sumiu do projeto.
    Touch(f.Output / "LEIAME.txt", "meu texto");
    Touch(f.Output / "Game" / "Scripts" / "velho.lua");
    // 'velho.lua' nao esta no manifesto (foi posto depois), entao tambem tem que sobreviver.
    // Ja um arquivo que a exportacao criou e que saiu do projeto tem que sumir:
    fs::remove(f.Project / "Scripts" / "player.lua");
    Touch(f.Project / "Scripts" / "novo.lua");

    GameExportResult r = ExportGame(f.Options);
    CHECK(r.Success);
    CHECK(Exists(f.Output / "LEIAME.txt"));                          // nunca apaga o que nao criou
    CHECK(Exists(f.Output / "Game" / "Scripts" / "velho.lua"));
    CHECK(!Exists(f.Output / "Game" / "Scripts" / "player.lua"));    // saiu do projeto: some
    CHECK(Exists(f.Output / "Game" / "Scripts" / "novo.lua"));

    // Renomear o jogo remove o executavel antigo.
    f.Options.GameName = "Outro";
    CHECK(ExportGame(f.Options).Success);
    CHECK(Exists(f.Output / (std::string("Outro") + kExe)));
    CHECK(!Exists(f.Output / (std::string("Meu Jogo") + kExe)));
}

static void TestRefusals() {
    std::puts("[Recusas]");
    {   // Pasta de destino com coisas que nao sao nossas
        Fixture f = MakeFixture("foreign");
        Touch(f.Output / "documentos" / "importante.docx");
        GameExportResult r = ExportGame(f.Options);
        CHECK(!r.Success && !r.Error.empty());
        CHECK(Exists(f.Output / "documentos" / "importante.docx"));
        CHECK(!Exists(f.Output / "Game"));
    }
    {   // Destino vazio ja existente: ok
        Fixture f = MakeFixture("emptyout");
        fs::create_directories(f.Output);
        CHECK(ExportGame(f.Options).Success);
    }
    {   // Destino dentro do projeto
        Fixture f = MakeFixture("insideproject");
        f.Options.OutputDirectory = f.Project / "Export";
        CHECK(!ExportGame(f.Options).Success);
        CHECK(!Exists(f.Project / "Export" / "Game"));
    }
    {   // Destino dentro do Runtime
        Fixture f = MakeFixture("insideruntime");
        f.Options.OutputDirectory = f.Runtime / "out";
        CHECK(!ExportGame(f.Options).Success);
    }
    {   // Destino que CONTEM o projeto (apagaria/duplicaria o proprio projeto)
        Fixture f = MakeFixture("containsproject");
        f.Options.OutputDirectory = f.Project.parent_path();
        GameExportResult r = ExportGame(f.Options);
        CHECK(!r.Success);
        CHECK(Exists(f.Project / "Aventura.prismproj"));
    }
    {   // Sem mapa inicial / mapa inexistente
        Fixture f = MakeFixture("nomap");
        f.Options.StartMap.clear();
        CHECK(!ExportGame(f.Options).Success);
        f.Options.StartMap = "NaoExiste.prismmap";
        CHECK(!ExportGame(f.Options).Success);
        CHECK(!Exists(f.Output / "Game"));
    }
    {   // Runtime nao compilado
        Fixture f = MakeFixture("noruntime");
        fs::remove(f.Runtime / (std::string("PrismRuntime") + kExe));
        GameExportResult r = ExportGame(f.Options);
        CHECK(!r.Success);
        CHECK(r.Error.find("PrismRuntime") != std::string::npos);
    }
    {   // Sem projeto
        Fixture f = MakeFixture("noproject");
        fs::remove(f.Project / "Aventura.prismproj");
        CHECK(!ExportGame(f.Options).Success);
    }
    {   // Manifesto adulterado nao pode fazer a limpeza sair da pasta de saida
        Fixture f = MakeFixture("tamper");
        CHECK(ExportGame(f.Options).Success);
        Touch(f.Project.parent_path() / "fora.txt", "nao apague");
        std::ofstream(f.Output / ".prism-export", std::ios::binary | std::ios::trunc)
            << "PrismExport 1\n../fora.txt\n";
        CHECK(ExportGame(f.Options).Success);
        CHECK(Exists(f.Project.parent_path() / "fora.txt"));
    }
}

static void TestModuleWarnings() {
    std::puts("[Avisos do modulo C++]");
    {   // Projeto com Source/ mas sem modulo compilado
        Fixture f = MakeFixture("nomodule", /*withModule=*/false);
        GameExportResult r = ExportGame(f.Options);
        CHECK(r.Success);
        CHECK(r.Warnings.size() == 1);
    }
    {   // Modulo mais velho que o codigo
        Fixture f = MakeFixture("stale");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fs::last_write_time(f.Project / "Source" / "Spinner.cpp", fs::file_time_type::clock::now() + std::chrono::seconds(5));
        GameExportResult r = ExportGame(f.Options);
        CHECK(r.Success);
        CHECK(r.Warnings.size() == 1);
    }
    {   // PrismGenerated.cmake e regravado sempre: nao pode contar como "codigo novo"
        Fixture f = MakeFixture("generated");
        Touch(f.Project / "Source" / "PrismGenerated.cmake");
        fs::last_write_time(f.Project / "Source" / "PrismGenerated.cmake", fs::file_time_type::clock::now() + std::chrono::seconds(5));
        GameExportResult r = ExportGame(f.Options);
        CHECK(r.Success);
        CHECK(r.Warnings.empty());
    }
    {   // Projeto sem scripts C++ (sem Source/): sem aviso
        Fixture f = MakeFixture("nocpp", /*withModule=*/false);
        fs::remove_all(f.Project / "Source");
        GameExportResult r = ExportGame(f.Options);
        CHECK(r.Success);
        CHECK(r.Warnings.empty());
    }
}

int main() {
    fs::remove_all(Root());
    TestSanitize();
    TestHappyPath();
    TestReexport();
    TestRefusals();
    TestModuleWarnings();

    std::printf("\n%d verificacoes, %d falha(s).\n", g_Checks, g_Failures);
    fs::remove_all(Root());
    return g_Failures == 0 ? 0 : 1;
}
