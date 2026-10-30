// Testes do cache em disco e da matematica de miniaturas (so std).
#include "Prism/Core/DiskCache.h"
#include "Prism/Assets/ThumbnailImage.h"
#include <cstdio>

namespace fs = std::filesystem;
using namespace Prism;

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static fs::path Fresh(const char* name) {
    fs::path d = fs::temp_directory_path() / "prism_cache_tests" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

int main() {
    // --- hash (vetores conhecidos de FNV-1a 64) ---
    CHECK(DiskCache::Hash64("", 0) == 0xcbf29ce484222325ull);
    CHECK(DiskCache::Hash64(std::string("a")) == 0xaf63dc4c8601ec8cull);
    CHECK(DiskCache::Hash64(std::string("foobar")) == 0x85944171f73967e8ull);
    CHECK(DiskCache::KeyName({ 1, 2 }) != DiskCache::KeyName({ 2, 1 }));
    CHECK(DiskCache::KeyName({ 1, 2 }) == DiskCache::KeyName({ 1, 2 }));
    CHECK(DiskCache::KeyName({ 7 }).size() == 16);

    // --- ida e volta ---
    fs::path d = Fresh("roundtrip");
    std::vector<uint8_t> payload = { 1, 2, 3, 250, 0, 9 };
    std::vector<uint8_t> got;
    CHECK(!DiskCache::Read(d, "k1", got));                 // miss
    CHECK(DiskCache::Write(d, "k1", payload));
    CHECK(DiskCache::Read(d, "k1", got) && got == payload);
    CHECK(DiskCache::Write(d, "k1", std::vector<uint8_t>{ 5 })); // sobrescreve
    CHECK(DiskCache::Read(d, "k1", got) && got == std::vector<uint8_t>{ 5 });
    CHECK(DiskCache::Write(d, "vazio", std::vector<uint8_t>{}));
    CHECK(DiskCache::Read(d, "vazio", got) && got.empty());
    size_t temps = 0;
    for (auto& e : fs::directory_iterator(d)) if (e.path().string().find(".tmp") != std::string::npos) temps++;
    CHECK(temps == 0);                                     // nenhum temporario sobrando

    // --- corrupcao: byte alterado, truncado, lixo, versao ---
    {
        fs::path file = DiskCache::EntryPath(d, "k1");
        DiskCache::Write(d, "bad1", payload);
        fs::path f1 = DiskCache::EntryPath(d, "bad1");
        { std::fstream f(f1, std::ios::in | std::ios::out | std::ios::binary); f.seekp(DiskCache::kHeaderSize + 1); f.put((char)0x77); }
        CHECK(!DiskCache::Read(d, "bad1", got));
        CHECK(!fs::exists(f1));                            // corrompido e apagado

        DiskCache::Write(d, "bad2", payload);
        fs::path f2 = DiskCache::EntryPath(d, "bad2");
        fs::resize_file(f2, fs::file_size(f2) - 2);
        CHECK(!DiskCache::Read(d, "bad2", got) && !fs::exists(f2));

        { std::ofstream(DiskCache::EntryPath(d, "bad3"), std::ios::binary) << "isto nao e um cache valido de jeito nenhum"; }
        CHECK(!DiskCache::Read(d, "bad3", got) && !fs::exists(DiskCache::EntryPath(d, "bad3")));

        DiskCache::Write(d, "bad4", payload);
        fs::path f4 = DiskCache::EntryPath(d, "bad4");
        { std::fstream f(f4, std::ios::in | std::ios::out | std::ios::binary); f.seekp(4); uint32_t v = 99; f.write((char*)&v, 4); }
        CHECK(!DiskCache::Read(d, "bad4", got) && !fs::exists(f4));
        CHECK(fs::exists(file));                           // as outras entradas ficam
    }

    // --- pasta inexistente / nao gravavel ---
    CHECK(!DiskCache::Read(d / "nao" / "existe", "x", got));
    { std::ofstream(d / "arquivo", std::ios::binary) << "x"; }
    CHECK(!DiskCache::Write(d / "arquivo", "x", payload)); // 'pasta' e um arquivo: falha sem excecao

    // --- Prune ---
    fs::path p = Fresh("prune");
    for (int i = 0; i < 5; i++) {
        DiskCache::Write(p, "e" + std::to_string(i), std::vector<uint8_t>(1000, (uint8_t)i));
        fs::last_write_time(DiskCache::EntryPath(p, "e" + std::to_string(i)),
            fs::file_time_type::clock::now() - std::chrono::hours(10 * (5 - i))); // e0 mais antigo
    }
    const uint64_t each = DiskCache::kHeaderSize + 1000;
    CHECK(DiskCache::Prune(p, each * 5) == 0);             // dentro do limite
    CHECK(DiskCache::Prune(p, each * 2) == 3);
    CHECK(!fs::exists(DiskCache::EntryPath(p, "e0")) && !fs::exists(DiskCache::EntryPath(p, "e2")));
    CHECK(fs::exists(DiskCache::EntryPath(p, "e3")) && fs::exists(DiskCache::EntryPath(p, "e4")));
    { std::ofstream(p / "velho.tmp1", std::ios::binary) << "x"; }
    fs::last_write_time(p / "velho.tmp1", fs::file_time_type::clock::now() - std::chrono::hours(3));
    CHECK(DiskCache::Prune(p, each * 10) == 1 && !fs::exists(p / "velho.tmp1"));
    CHECK(DiskCache::Prune(p / "nao_existe", 10) == 0);

    // --- miniaturas ---
    uint32_t w = 0, h = 0;
    ThumbnailImage::ComputeSize(1024, 512, 128, w, h); CHECK(w == 128 && h == 64);
    ThumbnailImage::ComputeSize(512, 1024, 128, w, h); CHECK(w == 64 && h == 128);
    ThumbnailImage::ComputeSize(100, 50, 128, w, h);   CHECK(w == 100 && h == 50);   // nunca amplia
    ThumbnailImage::ComputeSize(10000, 3, 128, w, h);  CHECK(w == 128 && h == 1);    // minimo 1
    ThumbnailImage::ComputeSize(0, 5, 128, w, h);      CHECK(w == 0 && h == 0);

    // 4x4 -> 2x2 (maxSide 2): cada bloco 2x2 vira a media
    std::vector<uint8_t> img(4 * 4 * 4, 255);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        uint8_t* px = &img[(y * 4 + x) * 4];
        const bool left = x < 2;
        px[0] = left ? 200 : 0; px[1] = 0; px[2] = left ? 0 : 100; px[3] = 255;
    }
    ThumbnailPixels t = ThumbnailImage::Downscale(img.data(), 4, 4, 2);
    CHECK(t.Width == 2 && t.Height == 2 && t.RGBA.size() == 16);
    CHECK(t.RGBA[0] == 200 && t.RGBA[2] == 0 && t.RGBA[4] == 0 && t.RGBA[6] == 100);

    // pixel transparente nao escurece o vizinho opaco
    std::vector<uint8_t> half(2 * 1 * 4, 0);
    half[0] = 255; half[1] = 255; half[2] = 255; half[3] = 255; // opaco branco; o outro e (0,0,0,0)
    ThumbnailPixels one = ThumbnailImage::Downscale(half.data(), 2, 1, 1);
    CHECK(one.Width == 1 && one.RGBA[0] == 255 && one.RGBA[3] == 127);
    CHECK(ThumbnailImage::Downscale(nullptr, 4, 4, 2).RGBA.empty());

    // empacotar / desempacotar
    std::vector<uint8_t> packed = ThumbnailImage::Pack(t);
    ThumbnailPixels back;
    CHECK(ThumbnailImage::Unpack(packed, back, 128) && back.Width == 2 && back.RGBA == t.RGBA);
    CHECK(!ThumbnailImage::Unpack(packed, back, 1));                       // maior que o limite
    packed.pop_back();
    CHECK(!ThumbnailImage::Unpack(packed, back, 128));                     // truncado
    CHECK(!ThumbnailImage::Unpack({ 1, 2, 3 }, back, 128));

    // imagem grande: tempo/consistencia basica
    std::vector<uint8_t> big((size_t)2048 * 2048 * 4, 90);
    ThumbnailPixels bt = ThumbnailImage::Downscale(big.data(), 2048, 2048, 128);
    CHECK(bt.Width == 128 && bt.Height == 128 && bt.RGBA[0] == 90 && bt.RGBA[3] == 90);

    fs::remove_all(fs::temp_directory_path() / "prism_cache_tests");
    std::printf("%d verificacoes, %d falha(s).\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
