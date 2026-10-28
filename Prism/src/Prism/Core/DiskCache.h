#pragma once

// ============================================================================
// DiskCache.h
// Cache em disco de dados DERIVADOS (bytecode do Lua, miniaturas, ...): uma
// pasta de arquivos nomeados pela chave (hash do conteudo/origem + versao de
// quem gera). Sempre reconstruivel: apagar a pasta nunca perde dados, so tempo.
//
//   * Formato: "PDC1", versao do formato, tamanho e hash do payload, payload.
//     Leitura valida tudo; arquivo truncado/corrompido e APAGADO e vira miss.
//   * Escrita atomica: grava em arquivo temporario e renomeia, entao dois
//     processos (editor + jogo) ou uma queda de energia nunca deixam um
//     arquivo pela metade com o nome final.
//   * Prune(): mantem a pasta abaixo de um limite apagando os mais antigos.
//   * Falhas de E/S (disco cheio, somente leitura) nunca sao fatais: Write
//     devolve false e quem chamou segue sem cache.
//
// Hash: FNV-1a de 64 bits - nao criptografico, so para chave e deteccao de
// corrupcao acidental. So std: compila e e testado isoladamente (tests/).
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Prism::DiskCache {

    constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
    constexpr uint64_t kFnvPrime = 0x100000001b3ull;

    inline uint64_t Hash64(const void* data, size_t size, uint64_t seed = kFnvOffset) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        uint64_t hash = seed;
        for (size_t i = 0; i < size; i++) {
            hash ^= bytes[i];
            hash *= kFnvPrime;
        }
        return hash;
    }

    inline uint64_t Hash64(const std::string& text, uint64_t seed = kFnvOffset) {
        return Hash64(text.data(), text.size(), seed);
    }

    // Nome de arquivo (16 hex) da chave composta por varias partes (tipo, versao, conteudo...).
    inline std::string KeyName(std::initializer_list<uint64_t> parts) {
        uint64_t hash = kFnvOffset;
        for (uint64_t part : parts)
            hash = Hash64(&part, sizeof(part), hash);
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%016llx", (unsigned long long)hash);
        return buffer;
    }

    constexpr uint32_t kFormatVersion = 1;
    constexpr size_t kHeaderSize = 4 + 4 + 8 + 8;

    inline std::filesystem::path EntryPath(const std::filesystem::path& directory, const std::string& key) {
        return directory / (key + ".pdc");
    }

    // Le a entrada 'key'. Miss = false (inexistente, versao do formato diferente ou corrompida - esta ultima e apagada).
    inline bool Read(const std::filesystem::path& directory, const std::string& key, std::vector<uint8_t>& out) {
        const std::filesystem::path file = EntryPath(directory, key);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec))
            return false;

        auto discard = [&]() { std::error_code ignore; std::filesystem::remove(file, ignore); return false; };

        std::ifstream in(file, std::ios::binary);
        if (!in.is_open())
            return false;
        unsigned char header[kHeaderSize];
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!in || std::memcmp(header, "PDC1", 4) != 0)
            return discard();
        uint32_t version = 0;
        uint64_t size = 0, hash = 0;
        std::memcpy(&version, header + 4, 4);
        std::memcpy(&size, header + 8, 8);
        std::memcpy(&hash, header + 16, 8);
        if (version != kFormatVersion)
            return discard();

        const uintmax_t fileSize = std::filesystem::file_size(file, ec);
        if (ec || fileSize != kHeaderSize + size)
            return discard();

        std::vector<uint8_t> payload((size_t)size);
        if (size > 0) {
            in.read(reinterpret_cast<char*>(payload.data()), (std::streamsize)size);
            if (!in)
                return discard();
        }
        if (Hash64(payload.data(), payload.size()) != hash)
            return discard();

        // "Usado recentemente": o Prune apaga primeiro o que nao e lido ha mais tempo (no maximo 1 toque por dia).
        auto modified = std::filesystem::last_write_time(file, ec);
        if (!ec && std::filesystem::file_time_type::clock::now() - modified > std::chrono::hours(24))
            std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ec);

        out = std::move(payload);
        return true;
    }

    inline bool Write(const std::filesystem::path& directory, const std::string& key, const void* data, size_t size) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec)
            return false;

        const std::filesystem::path file = EntryPath(directory, key);
        static uint64_t s_Counter = 0;
        const std::filesystem::path temp = directory / (key + ".tmp" + std::to_string(++s_Counter)
            + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
                return false;
            const uint32_t version = kFormatVersion;
            const uint64_t payloadSize = size, hash = Hash64(data, size);
            out.write("PDC1", 4);
            out.write(reinterpret_cast<const char*>(&version), 4);
            out.write(reinterpret_cast<const char*>(&payloadSize), 8);
            out.write(reinterpret_cast<const char*>(&hash), 8);
            if (size > 0)
                out.write(static_cast<const char*>(data), (std::streamsize)size);
            out.flush();
            if (!out) {
                out.close();
                std::filesystem::remove(temp, ec);
                return false;
            }
        }
        std::filesystem::rename(temp, file, ec);
        if (ec) {
            std::error_code ignore;
            std::filesystem::remove(temp, ignore);
            return false;
        }
        return true;
    }

    inline bool Write(const std::filesystem::path& directory, const std::string& key, const std::vector<uint8_t>& data) {
        return Write(directory, key, data.data(), data.size());
    }

    // Mantem a pasta com no maximo 'maxBytes': apaga os arquivos menos recentes primeiro. Tambem
    // limpa temporarios (.tmp*) abandonados ha mais de 1 hora. Devolve quantos arquivos apagou.
    inline size_t Prune(const std::filesystem::path& directory, uint64_t maxBytes) {
        struct Item { std::filesystem::path Path; uint64_t Size; std::filesystem::file_time_type Time; };
        std::vector<Item> items;
        uint64_t total = 0;
        size_t removed = 0;
        std::error_code ec;
        const auto now = std::filesystem::file_time_type::clock::now();
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileError;
            if (!it->is_regular_file(fileError))
                continue;
            const std::string name = it->path().filename().string();
            const auto time = it->last_write_time(fileError);
            const uint64_t size = (uint64_t)it->file_size(fileError);
            if (fileError)
                continue;
            if (name.find(".tmp") != std::string::npos) {
                if (now - time > std::chrono::hours(1)) {
                    std::error_code ignore;
                    if (std::filesystem::remove(it->path(), ignore)) removed++;
                }
                continue;
            }
            if (it->path().extension() != ".pdc")
                continue;
            items.push_back({ it->path(), size, time });
            total += size;
        }
        if (total <= maxBytes)
            return removed;
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.Time < b.Time; });
        for (const Item& item : items) {
            if (total <= maxBytes)
                break;
            std::error_code ignore;
            if (std::filesystem::remove(item.Path, ignore)) {
                total -= item.Size;
                removed++;
            }
        }
        return removed;
    }

}
