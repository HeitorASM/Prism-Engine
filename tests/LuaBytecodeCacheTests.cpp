// Testa o cache de bytecode do Lua com o Lua e o sol2 reais. O codigo testado e o MESMO do
// ScriptEngine.cpp: a build extrai o trecho entre "BEGIN LuaChunkCache" e "END LuaChunkCache"
// para LuaChunkCache.inc (ver tests/CMakeLists.txt).
#include <sol/sol.hpp>
#include "Prism/Core/DiskCache.h"
#include <cstdio>
#include <fstream>
#include <iostream>

using namespace Prism;
namespace fs = std::filesystem;

#include "LuaChunkCache.inc"

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static size_t CountFiles(const fs::path& d) {
    size_t n = 0; std::error_code ec;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) n++;
    return n;
}

int main() {
    fs::path dir = fs::temp_directory_path() / "prism_lua_cache_test";
    fs::remove_all(dir);
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

    const std::string source = "local x = 20\nfunction Soma(a) return a + x end\nreturn 7\n";

    // miss: compila e grava
    auto first = LoadLuaChunkCached(lua, source, "@teste.lua", dir);
    CHECK(first.valid());
    sol::protected_function f1 = first;
    auto r1 = f1();
    CHECK(r1.valid() && r1.get<int>() == 7);
    CHECK(CountFiles(dir) == 1);
    CHECK((int)lua["Soma"](1) == 21);

    // hit: mesmo resultado, sem criar outro arquivo
    lua["Soma"] = sol::nil;
    auto second = LoadLuaChunkCached(lua, source, "@teste.lua", dir);
    CHECK(second.valid());
    sol::protected_function f2 = second;
    CHECK(f2().get<int>() == 7 && (int)lua["Soma"](2) == 22);
    CHECK(CountFiles(dir) == 1);

    // texto diferente / chunk diferente = outra entrada
    CHECK(LoadLuaChunkCached(lua, source + "-- x\n", "@teste.lua", dir).valid());
    CHECK(LoadLuaChunkCached(lua, source, "@outro.lua", dir).valid());
    CHECK(CountFiles(dir) == 3);

    // erro de sintaxe: continua sendo erro, com arquivo:linha, e nada e gravado
    auto bad = LoadLuaChunkCached(lua, "local = = 1\n", "@quebrado.lua", dir);
    CHECK(!bad.valid());
    { sol::error err = bad; CHECK(std::string(err.what()).find("quebrado.lua:1") != std::string::npos); }
    CHECK(CountFiles(dir) == 3);

    // erro de execucao vindo do bytecode do cache ainda mostra arquivo e linha
    const std::string runtimeError = "local a = 1\nerror('falhou aqui')\n";
    LoadLuaChunkCached(lua, runtimeError, "@erro.lua", dir);
    auto cachedError = LoadLuaChunkCached(lua, runtimeError, "@erro.lua", dir);
    sol::protected_function fe = cachedError;
    auto re = fe();
    CHECK(!re.valid());
    { sol::error err = re; CHECK(std::string(err.what()).find("erro.lua:2") != std::string::npos); }

    // bytecode invalido com chave correta (checksum ok, mas Lua rejeita): refaz pelo texto
    {
        fs::path d2 = dir / "invalido";
        // descobre a chave gravando uma entrada e trocando o payload por lixo valido para o DiskCache
        LoadLuaChunkCached(lua, "return 1", "@lixo.lua", d2);
        std::string name;
        for (auto& e : fs::directory_iterator(d2)) name = e.path().stem().string();
        CHECK(DiskCache::Write(d2, name, std::vector<uint8_t>{ 0x1b, 'L', 'u', 'a', 0, 1, 2 }));
        auto recovered = LoadLuaChunkCached(lua, "return 1", "@lixo.lua", d2);
        CHECK(recovered.valid());
        sol::protected_function fr = recovered;
        CHECK(fr().get<int>() == 1);
        std::vector<uint8_t> rewritten;
        CHECK(DiskCache::Read(d2, name, rewritten) && rewritten.size() > 7); // sobrescrito com bytecode bom
    }

    // sem pasta (sem projeto): so compila
    CHECK(LoadLuaChunkCached(lua, source, "@teste.lua", fs::path()).valid());

    // texto com bytecode "disfarcado" nao e aceito como texto pelo modo binario do cache
    // (o cache so le o que ele mesmo gravou, com checksum)

    fs::remove_all(dir);
    std::printf("%d verificacoes, %d falha(s).\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
