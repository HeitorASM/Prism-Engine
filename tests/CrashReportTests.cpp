// CrashReportTests.cpp - relatorio de texto do CrashHandler (Core/CrashHandler.cpp). Usa WriteCrashReport,
// que gera o mesmo crash.log do crash real sem derrubar o processo. Compila so CrashHandler.cpp + LogBuffer.cpp.
#include <Prism/Core/CrashHandler.h>
#include <Prism/Core/LogBuffer.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace Prism;
namespace fs = std::filesystem;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    const fs::path workDir = fs::temp_directory_path() / "prism_crash_report_tests";
    std::error_code ec;
    fs::remove_all(workDir, ec);
    fs::create_directories(workDir);
    const fs::path previous = fs::current_path();
    fs::current_path(workDir); // o relatorio e criado em ./CrashReports

    LogBuffer::Install(100);
    PRISM_CORE_WARN("mensagem de teste antes do crash");
    SetCrashContext("GPU", "Placa de Teste 9000");
    SetCrashContext("GPU", "Placa de Teste 9001"); // repetir a chave substitui

    const std::string directory = WriteCrashReport("falha simulada", "detalhe simulado");
    CHECK(!directory.empty());

    std::ifstream in(fs::path(directory) / "crash.log");
    CHECK((bool)in);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("falha simulada") != std::string::npos);
    CHECK(text.find("detalhe simulado") != std::string::npos);
    CHECK(text.find("Placa de Teste 9001") != std::string::npos);
    CHECK(text.find("Placa de Teste 9000") == std::string::npos);
    CHECK(text.find("mensagem de teste antes do crash") != std::string::npos);
    CHECK(text.find("Engine:") != std::string::npos);

    Log::SetSink(nullptr);
    fs::current_path(previous);
    fs::remove_all(workDir, ec);
    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
