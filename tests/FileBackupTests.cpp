// Testes dos backups com rotacao (Core/FileBackup.h, so std).
#include "Prism/Core/FileBackup.h"
#include <cstdio>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
using namespace Prism;
using Clock = std::chrono::system_clock;

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static fs::path Fresh(const char* name) {
    fs::path d = fs::temp_directory_path() / "prism_backup_tests" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

static void WriteText(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

static std::string ReadText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main() {
    const auto t0 = Clock::now();
    using std::chrono::seconds;

    // --- arquivo inexistente ou vazio: nao cria backup ---
    {
        fs::path d = Fresh("missing");
        CHECK(FileBackup::Create(d / "naoexiste.prismmap", d / "bk").empty());
        WriteText(d / "vazio.prismmap", "");
        CHECK(FileBackup::Create(d / "vazio.prismmap", d / "bk").empty());
        CHECK(!fs::exists(d / "bk"));
        CHECK(FileBackup::List(d / "vazio.prismmap", d / "bk").empty());
    }

    // --- cria uma copia fiel, com o nome esperado ---
    {
        fs::path d = Fresh("basic");
        WriteText(d / "Fase1.prismmap", "conteudo-v1");
        fs::path b = FileBackup::Create(d / "Fase1.prismmap", d / "bk", 20, 0, t0);
        CHECK(!b.empty());
        CHECK(fs::exists(b));
        CHECK(ReadText(b) == "conteudo-v1");
        CHECK(b.extension() == ".prismmap");
        CHECK(b.filename().string().rfind("Fase1_", 0) == 0);
        CHECK(b.filename().string().size() == std::string("Fase1_20260101-000000.prismmap").size());
        CHECK(ReadText(d / "Fase1.prismmap") == "conteudo-v1"); // o original nao e tocado
        CHECK(FileBackup::List(d / "Fase1.prismmap", d / "bk").size() == 1);
    }

    // --- dois backups no mesmo segundo nao se sobrescrevem ---
    {
        fs::path d = Fresh("samesecond");
        WriteText(d / "A.prismmap", "um");
        fs::path b1 = FileBackup::Create(d / "A.prismmap", d / "bk", 20, 0, t0);
        WriteText(d / "A.prismmap", "dois");
        fs::path b2 = FileBackup::Create(d / "A.prismmap", d / "bk", 20, 0, t0);
        CHECK(!b1.empty() && !b2.empty() && b1 != b2);
        CHECK(ReadText(b1) == "um");
        CHECK(ReadText(b2) == "dois");
        auto list = FileBackup::List(d / "A.prismmap", d / "bk");
        CHECK(list.size() == 2);
        CHECK(!list.empty() && list.front() == b2); // o mais novo primeiro
    }

    // --- rotacao: guarda so os N mais novos ---
    {
        fs::path d = Fresh("rotation");
        std::vector<fs::path> made;
        for (int i = 0; i < 6; i++) {
            WriteText(d / "Mapa.prismmap", "v" + std::to_string(i));
            made.push_back(FileBackup::Create(d / "Mapa.prismmap", d / "bk", 3, 0, t0 + seconds(i * 10)));
        }
        auto list = FileBackup::List(d / "Mapa.prismmap", d / "bk");
        CHECK(list.size() == 3);
        CHECK(list.size() == 3 && ReadText(list[0]) == "v5"); // mais novo
        CHECK(list.size() == 3 && ReadText(list[2]) == "v3"); // v0..v2 foram apagados
        CHECK(!fs::exists(made[0]) && !fs::exists(made[2]) && fs::exists(made[5]));
    }

    // --- "Fase" nao mexe nos backups de "Fase_2" (prefixo parecido) ---
    {
        fs::path d = Fresh("prefix");
        WriteText(d / "Fase.prismmap", "a");
        WriteText(d / "Fase_2.prismmap", "b");
        for (int i = 0; i < 4; i++) {
            FileBackup::Create(d / "Fase.prismmap", d / "bk", 2, 0, t0 + seconds(i * 10));
            FileBackup::Create(d / "Fase_2.prismmap", d / "bk", 2, 0, t0 + seconds(i * 10));
        }
        CHECK(FileBackup::List(d / "Fase.prismmap", d / "bk").size() == 2);
        CHECK(FileBackup::List(d / "Fase_2.prismmap", d / "bk").size() == 2);
        WriteText(d / "bk" / "Fase_nota.txt", "arquivo estranho"); // nao e backup: nunca listado nem apagado
        FileBackup::Create(d / "Fase.prismmap", d / "bk", 1, 0, t0 + seconds(100));
        CHECK(fs::exists(d / "bk" / "Fase_nota.txt"));
        CHECK(FileBackup::List(d / "Fase.prismmap", d / "bk").size() == 1);
        CHECK(FileBackup::List(d / "Fase_2.prismmap", d / "bk").size() == 2);
    }

    // --- extensoes diferentes com o mesmo nome nao se misturam ---
    {
        fs::path d = Fresh("ext");
        WriteText(d / "X.prismmap", "m");
        WriteText(d / "X.prismprefab", "p");
        FileBackup::Create(d / "X.prismmap", d / "bk", 5, 0, t0);
        FileBackup::Create(d / "X.prismprefab", d / "bk", 5, 0, t0);
        CHECK(FileBackup::List(d / "X.prismmap", d / "bk").size() == 1);
        CHECK(FileBackup::List(d / "X.prismprefab", d / "bk").size() == 1);
    }

    // --- intervalo minimo ---
    {
        fs::path d = Fresh("throttle");
        WriteText(d / "T.prismmap", "t");
        CHECK(!FileBackup::Create(d / "T.prismmap", d / "bk", 20, 300, t0).empty());            // primeiro: sempre cria
        CHECK(FileBackup::Create(d / "T.prismmap", d / "bk", 20, 300, t0 + seconds(60)).empty());  // 60s < 300s
        CHECK(FileBackup::Create(d / "T.prismmap", d / "bk", 20, 300, t0 + seconds(299)).empty());
        CHECK(!FileBackup::Create(d / "T.prismmap", d / "bk", 20, 300, t0 + seconds(301)).empty()); // passou
        CHECK(!FileBackup::Create(d / "T.prismmap", d / "bk", 20, 0, t0 + seconds(302)).empty());   // intervalo 0 = sempre
        CHECK(FileBackup::List(d / "T.prismmap", d / "bk").size() == 3);
    }

    // --- keep invalido vira 1 (nunca apaga o backup recem-criado) ---
    {
        fs::path d = Fresh("keep");
        WriteText(d / "K.prismmap", "k");
        fs::path b = FileBackup::Create(d / "K.prismmap", d / "bk", 0, 0, t0);
        CHECK(!b.empty() && fs::exists(b));
    }

    std::printf("FileBackupTests: %d verificacoes, %d falha(s)\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
