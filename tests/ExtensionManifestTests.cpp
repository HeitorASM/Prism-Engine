// Testes do manifesto de extensoes (extension.ini) e da ordem de carga por dependencia.
// So usa a std + Extensions/ExtensionManifest.cpp.
#include <Prism/Extensions/ExtensionManifest.h>
#include <Prism/Extensions/ExtensionTemplate.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Prism;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static ExtensionManifest M(const std::string& name, std::vector<std::string> deps = {}) {
    ExtensionManifest m;
    m.Name = name;
    m.Library = name;
    m.Dependencies = std::move(deps);
    return m;
}
static bool Before(const std::vector<std::string>& v, const std::string& a, const std::string& b) {
    auto ia = std::find(v.begin(), v.end(), a), ib = std::find(v.begin(), v.end(), b);
    return ia != v.end() && ib != v.end() && ia < ib;
}

static void TestParseOk() {
    auto r = ParseExtensionManifest(
        "\xEF\xBB\xBF# comentario\r\n"
        "name = Health\r\n"
        "API=1\r\n"
        "version = \"1.2.0\"\r\n"
        "description = Vida e dano\r\n"
        "target = Runtime\r\n"
        "depends = Core.Utils, Outra ,, Core.Utils\r\n"
        "enabled = nao\r\n"
        "future_key = x\r\n");
    CHECK(r.Ok);
    CHECK(r.Manifest.Name == "Health");
    CHECK(r.Manifest.ApiVersion == 1);
    CHECK(r.Manifest.Version == "1.2.0");
    CHECK(r.Manifest.Library == "Health");           // padrao = name
    CHECK(r.Manifest.Target == ExtensionTarget::Runtime);
    CHECK(r.Manifest.Dependencies.size() == 2);       // vazias e repetidas somem
    CHECK(!r.Manifest.Enabled);
    CHECK(r.Warnings.size() == 1);                    // chave desconhecida so avisa
}

static void TestParseErrors() {
    CHECK(!ParseExtensionManifest("").Ok);                               // sem name/api
    CHECK(!ParseExtensionManifest("name = A\n").Ok);                     // sem api
    CHECK(!ParseExtensionManifest("api = 1\n").Ok);                      // sem name
    CHECK(!ParseExtensionManifest("name = A\napi = 0\n").Ok);
    CHECK(!ParseExtensionManifest("name = A\napi = x\n").Ok);
    CHECK(!ParseExtensionManifest("name = A B\napi = 1\n").Ok);          // espaco no nome
    CHECK(!ParseExtensionManifest("name = A\napi = 1\nlibrary = ../x\n").Ok);
    CHECK(!ParseExtensionManifest("name = A\napi = 1\nlibrary = a/b\n").Ok);
    CHECK(!ParseExtensionManifest("name = A\napi = 1\ntarget = tudo\n").Ok);
    CHECK(!ParseExtensionManifest("name = A\napi = 1\nenabled = talvez\n").Ok);
    CHECK(!ParseExtensionManifest("name = A\napi = 1\ndepends = A\n").Ok); // depende de si mesma
    CHECK(!ParseExtensionManifest("name = A\napi = 1\nlixo sem igual\n").Ok);
    CHECK(!ParseExtensionManifest("name = " + std::string(65, 'a') + "\napi = 1\n").Ok);
    CHECK(IsValidExtensionName("a.b-c_1"));
    CHECK(!IsValidExtensionName(""));
}

static void TestLoadOrder() {
    std::map<std::string, std::string> failures;
    // c depende de b, b depende de a; declarados fora de ordem
    auto order = ResolveLoadOrder({ M("c", { "b" }), M("b", { "a" }), M("a"), M("z") }, {}, failures);
    CHECK(failures.empty());
    CHECK(order.size() == 4);
    CHECK(Before(order, "a", "b") && Before(order, "b", "c"));

    // dependencia ausente derruba quem depende, em cascata, mas nao os outros
    failures.clear();
    order = ResolveLoadOrder({ M("x", { "falta" }), M("y", { "x" }), M("ok") }, {}, failures);
    CHECK(order.size() == 1 && order[0] == "ok");
    CHECK(failures.count("x") && failures.count("y"));

    // satisfeita por uma extensao ja carregada
    failures.clear();
    order = ResolveLoadOrder({ M("n", { "antiga" }) }, { "antiga" }, failures);
    CHECK(order.size() == 1 && failures.empty());

    // ciclo: ninguem do ciclo carrega, os independentes sim
    failures.clear();
    order = ResolveLoadOrder({ M("p", { "q" }), M("q", { "p" }), M("free") }, {}, failures);
    CHECK(order.size() == 1 && order[0] == "free");
    CHECK(failures.count("p") && failures.count("q"));
}

static void TestSetManifestValue() {
    // troca a linha existente, preservando comentarios e CRLF
    std::string text = "# topo\r\nname = A\r\n# enabled = false\r\nenabled = true\r\napi = 1\r\n";
    std::string out = SetManifestValue(text, "enabled", "false");
    CHECK(out == "# topo\r\nname = A\r\n# enabled = false\r\nenabled = false\r\napi = 1\r\n");
    // chave ausente: acrescenta ao fim (mesmo sem quebra de linha final)
    CHECK(SetManifestValue("name = A\napi = 1", "enabled", "false") == "name = A\napi = 1\nenabled = false\n");
    // chave em outra caixa, com espacos
    CHECK(SetManifestValue("  Enabled=true\n", "enabled", "false") == "enabled = false\n");
    // o resultado continua sendo um manifesto valido com o valor novo
    auto parsed = ParseExtensionManifest(SetManifestValue("name = A\napi = 1\n", "enabled", "false"));
    CHECK(parsed.Ok && !parsed.Manifest.Enabled);
}

static void TestTemplate() {
    CHECK(ExtensionIdentifier("My-Ext.v2") == "My_Ext_v2");
    CHECK(ExtensionIdentifier("3d") == "_3d");

    auto files = GenerateExtensionTemplate("Minha-Ext");
    std::string manifest, source;
    for (auto& [path, content] : files) {
        if (path == "extension.ini") manifest = content;
        if (path == "Minha-Ext.cpp") source = content;
        CHECK(content.find("@NAME@") == std::string::npos && content.find("@IDENT@") == std::string::npos);
    }
    auto parsed = ParseExtensionManifest(manifest); // o manifesto gerado precisa ser valido
    CHECK(parsed.Ok && parsed.Manifest.Name == "Minha-Ext" && parsed.Manifest.ApiVersion == 1);
    CHECK(source.find("struct Minha_Ext ") != std::string::npos);

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "prism_ext_template_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::path dir;
    std::string error;
    CHECK(WriteExtensionTemplate(root, "Teste", dir, error));
    CHECK(fs::exists(dir / "extension.ini") && fs::exists(dir / "CMakeLists.txt") && fs::exists(dir / "Teste.cpp"));
    CHECK(!WriteExtensionTemplate(root, "Teste", dir, error)); // nao sobrescreve
    CHECK(!WriteExtensionTemplate(root, "../fora", dir, error));
    CHECK(!WriteExtensionTemplate(root, "", dir, error));
    CHECK(!WriteExtensionTemplate(root, "..", dir, error));
    fs::remove_all(root, ec);
}

int main() {
    TestParseOk();
    TestParseErrors();
    TestLoadOrder();
    TestSetManifestValue();
    TestTemplate();
    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
