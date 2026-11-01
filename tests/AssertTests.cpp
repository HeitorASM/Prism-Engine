// AssertTests.cpp - formato da mensagem do PRISM_ASSERT (Core/Base.h). So headers (Base.h + Log.h),
// sem linkar a engine. Nao dispara o break de verdade: chama Detail::ReportAssertFailure direto e
// verifica as macros apenas com condicoes verdadeiras.
#include <Prism/Core/Base.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace Prism;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    std::vector<std::pair<LogLevel, std::string>> logged;
    Log::SetSink([&](LogLevel level, const std::string&, const std::string& message) { logged.emplace_back(level, message); });

    // com mensagem: expressao, arquivo:linha e as partes da mensagem concatenadas
    Detail::ReportAssertFailure("x == 1", "Arquivo.cpp", 42, std::forward_as_tuple("valor inesperado: ", 7, "!"));
    CHECK(logged.size() == 1);
    if (logged.size() == 1) {
        CHECK(logged[0].first == LogLevel::Error);
        CHECK(logged[0].second.find("x == 1") != std::string::npos);
        CHECK(logged[0].second.find("Arquivo.cpp:42") != std::string::npos);
        CHECK(logged[0].second.find("valor inesperado: 7!") != std::string::npos);
    }

    // sem mensagem: nao deixa " - " sobrando
    logged.clear();
    Detail::ReportAssertFailure("ponteiro", "B.cpp", 1, std::forward_as_tuple());
    CHECK(logged.size() == 1);
    if (logged.size() == 1)
        CHECK(logged[0].second.find(" - ") == std::string::npos);

    // as macros compilam nas duas formas e NAO logam quando a condicao e verdadeira
    logged.clear();
    PRISM_ASSERT(1 + 1 == 2);
    PRISM_ASSERT(1 + 1 == 2, "nao deve aparecer ", 123);
    CHECK(logged.empty());

    Log::SetSink(nullptr);
    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
