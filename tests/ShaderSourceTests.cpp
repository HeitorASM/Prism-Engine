// ============================================================================
// ShaderSourceTests.cpp
// Testes do carregador de shaders (#include, defines, #line). Sem GPU: so o
// preprocessamento de texto. Mesmo estilo de AssetTests.cpp (CHECK simples).
// ============================================================================
#include "Prism/Renderer/ShaderSource.h"

#include <cstdio>
#include <fstream>

namespace fs = std::filesystem;
using namespace Prism;

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void Write(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}
static fs::path FreshDir(const char* name) {
    fs::path d = fs::temp_directory_path() / "prism_shader_tests" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
static bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
static size_t Count(const std::string& hay, const std::string& needle) {
    size_t n = 0, p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) { n++; p += needle.size(); }
    return n;
}

static void TestPlainFile() {
    std::puts("[arquivo simples]");
    fs::path d = FreshDir("plain");
    Write(d / "a.frag", "#version 450 core\nvoid main() {}\n");
    ShaderSourceResult r = ShaderSource::Load(d / "a.frag");
    CHECK(r.Ok);
    CHECK(r.Source == "#version 450 core\nvoid main() {}\n");
    CHECK(r.Files.size() == 1);

    ShaderSourceResult missing = ShaderSource::Load(d / "nao_existe.frag");
    CHECK(!missing.Ok);
    CHECK(Contains(missing.Error, "nao_existe.frag"));
    CHECK(missing.Files.size() == 1); // o hot reload precisa observar o arquivo mesmo faltando
}

static void TestLineEndingsAndBom() {
    std::puts("[CRLF e BOM]");
    fs::path d = FreshDir("eol");
    Write(d / "a.frag", "\xEF\xBB\xBF#version 450 core\r\nvoid main() {}\r\n");
    ShaderSourceResult r = ShaderSource::Load(d / "a.frag");
    CHECK(r.Ok);
    CHECK(r.Source == "#version 450 core\nvoid main() {}\n");

    Write(d / "b.frag", "#version 450 core\nvoid main() {}"); // sem '\n' final
    ShaderSourceResult r2 = ShaderSource::Load(d / "b.frag");
    CHECK(r2.Ok);
    CHECK(r2.Source == "#version 450 core\nvoid main() {}\n");
}

static void TestDefines() {
    std::puts("[defines]");
    fs::path d = FreshDir("defines");
    Write(d / "a.frag", "// comentario antes do version e valido\n#version 450 core\nint x = MAX_LIGHTS;\n");
    ShaderSourceResult r = ShaderSource::Load(d / "a.frag", { {"MAX_LIGHTS", "16"}, {"FOO", "1"} });
    CHECK(r.Ok);
    CHECK(r.Source ==
        "// comentario antes do version e valido\n"
        "#version 450 core\n"
        "#define MAX_LIGHTS 16\n"
        "#define FOO 1\n"
        "#line 3 0\n"
        "int x = MAX_LIGHTS;\n");

    Write(d / "b.frag", "#versionX\n"); // '#versionX' nao e '#version'
    CHECK(!ShaderSource::Load(d / "b.frag", { {"A", "1"} }).Ok);

    Write(d / "c.frag", "void main() {}\n"); // defines sem #version: erro claro
    ShaderSourceResult r3 = ShaderSource::Load(d / "c.frag", { {"A", "1"} });
    CHECK(!r3.Ok);
    CHECK(Contains(r3.Error, "#version"));
    CHECK(ShaderSource::Load(d / "c.frag").Ok); // sem defines, nao exige #version
}

static void TestInclude() {
    std::puts("[include]");
    fs::path d = FreshDir("include");
    Write(d / "common.glsl", "float Half(float v) { return v * 0.5; }\n");
    Write(d / "main.frag", "#version 450 core\n#include \"common.glsl\"\nvoid main() { Half(1.0); }\n");

    ShaderSourceResult r = ShaderSource::Load(d / "main.frag");
    CHECK(r.Ok);
    CHECK(r.Files.size() == 2);
    CHECK(r.Files.size() == 2 && r.Files[1].filename() == "common.glsl");
    CHECK(r.Source ==
        "#version 450 core\n"
        "#line 1 1\n"
        "float Half(float v) { return v * 0.5; }\n"
        "#line 3 0\n"
        "void main() { Half(1.0); }\n");

    Write(d / "spaced.frag", "#version 450 core\n   #  include   \"common.glsl\"   // helper\nvoid main() {}\n");
    CHECK(ShaderSource::Load(d / "spaced.frag").Ok);

    Write(d / "lib/inner.glsl", "// inner\n");
    Write(d / "lib/outer.glsl", "#include \"inner.glsl\"\n"); // relativo ao arquivo que inclui
    Write(d / "deep.frag", "#version 450 core\n#include \"lib/outer.glsl\"\nvoid main() {}\n");
    ShaderSourceResult deep = ShaderSource::Load(d / "deep.frag");
    CHECK(deep.Ok);
    CHECK(deep.Files.size() == 3);

    Write(d / "include/shared.glsl", "// shared\n"); // fallback: pasta do arquivo PRINCIPAL
    Write(d / "sub/user.glsl", "#include \"include/shared.glsl\"\n");
    Write(d / "root.frag", "#version 450 core\n#include \"sub/user.glsl\"\nvoid main() {}\n");
    CHECK(ShaderSource::Load(d / "root.frag").Ok);
}

static void TestIncludeOnce() {
    std::puts("[include uma vez por shader]");
    fs::path d = FreshDir("once");
    Write(d / "common.glsl", "struct S { int a; };\n");
    Write(d / "a.glsl", "#include \"common.glsl\"\nint fa;\n");
    Write(d / "b.glsl", "#include \"common.glsl\"\nint fb;\n");
    Write(d / "main.frag", "#version 450 core\n#include \"a.glsl\"\n#include \"b.glsl\"\n#include \"common.glsl\"\nvoid main() {}\n");
    ShaderSourceResult r = ShaderSource::Load(d / "main.frag");
    CHECK(r.Ok);
    CHECK(Count(r.Source, "struct S") == 1); // diamante: sem redefinicao
    CHECK(Contains(r.Source, "int fa;") && Contains(r.Source, "int fb;"));
}

static void TestErrors() {
    std::puts("[erros]");
    fs::path d = FreshDir("errors");

    Write(d / "a.frag", "#version 450 core\n\n#include \"fantasma.glsl\"\n");
    ShaderSourceResult r = ShaderSource::Load(d / "a.frag");
    CHECK(!r.Ok);
    CHECK(Contains(r.Error, "a.frag:3"));
    CHECK(Contains(r.Error, "fantasma.glsl"));
    CHECK(r.Files.size() == 1);

    Write(d / "x.glsl", "#include \"y.glsl\"\n");
    Write(d / "y.glsl", "#include \"x.glsl\"\n");
    Write(d / "cycle.frag", "#version 450 core\n#include \"x.glsl\"\n");
    ShaderSourceResult c = ShaderSource::Load(d / "cycle.frag");
    CHECK(!c.Ok);
    CHECK(Contains(c.Error, "circular"));

    Write(d / "self.frag", "#version 450 core\n#include \"self.frag\"\n");
    CHECK(!ShaderSource::Load(d / "self.frag").Ok);

    Write(d / "m1.frag", "#version 450 core\n#include <algo.glsl>\n");
    CHECK(!ShaderSource::Load(d / "m1.frag").Ok);
    Write(d / "m2.frag", "#version 450 core\n#include \"aberto.glsl\n");
    CHECK(!ShaderSource::Load(d / "m2.frag").Ok);
    Write(d / "m3.frag", "#version 450 core\n#include \"x.glsl\" lixo\n");
    CHECK(!ShaderSource::Load(d / "m3.frag").Ok);
    Write(d / "m4.frag", "#version 450 core\n#include\n");
    CHECK(!ShaderSource::Load(d / "m4.frag").Ok);

    for (int i = 0; i < 20; i++)
        Write(d / ("chain" + std::to_string(i) + ".glsl"), "#include \"chain" + std::to_string(i + 1) + ".glsl\"\n");
    Write(d / "chain20.glsl", "// fim\n");
    Write(d / "deepchain.frag", "#version 450 core\n#include \"chain0.glsl\"\n");
    ShaderSourceResult deep = ShaderSource::Load(d / "deepchain.frag");
    CHECK(!deep.Ok);
    CHECK(Contains(deep.Error, "aninhados"));
}

// Todo shader REAL do repositorio tem que expandir sem erro: pega include com
// nome errado, arquivo renomeado e '#version' faltando antes do driver.
#ifdef PRISM_TEST_SHADER_DIR
static void TestRealShaders() {
    std::puts("[shaders reais]");
    const fs::path dir = PRISM_TEST_SHADER_DIR;
    CHECK(fs::is_directory(dir));
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string ext = entry.path().extension().string();
        if (ext != ".vert" && ext != ".frag")
            continue;
        count++;
        ShaderSourceResult r = ShaderSource::Load(entry.path(), { {"MAX_LIGHTS", "16"} });
        if (!r.Ok)
            std::printf("  %s: %s\n", entry.path().filename().string().c_str(), r.Error.c_str());
        CHECK(r.Ok);
        CHECK(Contains(r.Source, "#version 450 core"));
        CHECK(Contains(r.Source, "#define MAX_LIGHTS 16"));
    }
    CHECK(count >= 11); // avisa se alguem apagar shaders sem querer
}
#endif

int main() {
    TestPlainFile();
    TestLineEndingsAndBom();
    TestDefines();
    TestInclude();
    TestIncludeOnce();
    TestErrors();
#ifdef PRISM_TEST_SHADER_DIR
    TestRealShaders();
#endif
    std::printf("\n%d verificacoes, %d falha(s).\n", g_Checks, g_Failures);
    fs::remove_all(fs::temp_directory_path() / "prism_shader_tests");
    return g_Failures == 0 ? 0 : 1;
}
