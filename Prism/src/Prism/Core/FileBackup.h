#pragma once

// ============================================================================
// FileBackup.h
// Backups com data/hora de um arquivo ANTES de ele ser sobrescrito, com
// rotacao (guarda so os N mais recentes). Complementa o AtomicFile.h: o save
// atomico impede um arquivo PELA METADE, mas nao protege contra um arquivo
// INTEIRO porem errado (um bug de serializacao, um component descartado por
// dado invalido na leitura e entao regravado sem ele, uma migracao de formato
// antigo, um "Salvar" por engano). O backup guarda o arquivo como estava.
//
//   <backupDir>/<nome>_AAAAMMDD-HHMMSS<ext>        (ex.: Fase1_20261101-221500.prismmap)
//   <backupDir>/<nome>_AAAAMMDD-HHMMSS_2<ext>      (2o backup no mesmo segundo)
//
// So std (sem GL/GLFW/EnTT/Log): compila e e testado isoladamente
// (tests/FileBackupTests.cpp). Nunca lanca excecao: falha de disco vira
// retorno vazio, e quem chama decide se avisa o usuario.
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Prism::FileBackup {

    namespace detail {

        inline std::tm LocalTime(std::time_t t) {
            std::tm result{};
#if defined(_WIN32)
            localtime_s(&result, &t);
#else
            localtime_r(&t, &result);
#endif
            return result;
        }

        // "AAAAMMDD-HHMMSS" (15 caracteres).
        inline std::string Stamp(std::chrono::system_clock::time_point when) {
            const std::tm tm = LocalTime(std::chrono::system_clock::to_time_t(when));
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            return buffer;
        }

        inline bool IsDigits(const std::string& s, size_t pos, size_t count) {
            if (pos + count > s.size())
                return false;
            for (size_t i = 0; i < count; i++)
                if (s[pos + i] < '0' || s[pos + i] > '9')
                    return false;
            return true;
        }

        // 'name' tem o formato exato <stem>_<AAAAMMDD-HHMMSS>[_N]<ext>? Comparacao estrita de
        // proposito: os backups de "Fase" nao podem pegar os de "Fase_2" (que tambem comeca com "Fase_").
        // Se 'stampOut' != nullptr recebe o "AAAAMMDD-HHMMSS".
        inline bool MatchesBackupName(const std::string& name, const std::string& stem, const std::string& ext, std::string* stampOut = nullptr) {
            const std::string prefix = stem + "_";
            if (name.size() < prefix.size() + 15 + ext.size())
                return false;
            if (name.compare(0, prefix.size(), prefix) != 0)
                return false;
            if (name.compare(name.size() - ext.size(), ext.size(), ext) != 0)
                return false;
            const size_t stampPos = prefix.size();
            if (!IsDigits(name, stampPos, 8) || name[stampPos + 8] != '-' || !IsDigits(name, stampPos + 9, 6))
                return false;
            size_t rest = stampPos + 15;
            const size_t end = name.size() - ext.size();
            if (rest != end) {
                // Sufixo opcional "_N" (N >= 2).
                if (name[rest] != '_' || rest + 1 >= end)
                    return false;
                for (size_t i = rest + 1; i < end; i++)
                    if (name[i] < '0' || name[i] > '9')
                        return false;
            }
            if (stampOut)
                *stampOut = name.substr(stampPos, 15);
            return true;
        }

        // Segundos desde a epoca do "AAAAMMDD-HHMMSS" (hora local). -1 se invalido.
        inline long long StampToSeconds(const std::string& stamp) {
            if (stamp.size() != 15)
                return -1;
            std::tm tm{};
            tm.tm_year = std::atoi(stamp.substr(0, 4).c_str()) - 1900;
            tm.tm_mon = std::atoi(stamp.substr(4, 2).c_str()) - 1;
            tm.tm_mday = std::atoi(stamp.substr(6, 2).c_str());
            tm.tm_hour = std::atoi(stamp.substr(9, 2).c_str());
            tm.tm_min = std::atoi(stamp.substr(11, 2).c_str());
            tm.tm_sec = std::atoi(stamp.substr(13, 2).c_str());
            tm.tm_isdst = -1;
            const std::time_t t = std::mktime(&tm);
            return t == (std::time_t)-1 ? -1 : (long long)t;
        }

    } // namespace detail

    // Backups existentes de 'target' em 'backupDir', do MAIS NOVO para o mais antigo.
    inline std::vector<std::filesystem::path> List(const std::filesystem::path& target, const std::filesystem::path& backupDir) {
        std::vector<std::filesystem::path> found;
        std::error_code ec;
        if (!std::filesystem::is_directory(backupDir, ec))
            return found;

        const std::string stem = target.stem().string();
        const std::string ext = target.extension().string();
        for (std::filesystem::directory_iterator it(backupDir, ec), endIt; !ec && it != endIt; it.increment(ec)) {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc))
                continue;
            if (detail::MatchesBackupName(it->path().filename().string(), stem, ext))
                found.push_back(it->path());
        }
        // O carimbo e zero-padded, entao a ordem alfabetica e a cronologica.
        std::sort(found.begin(), found.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return a.filename().string() > b.filename().string();
        });
        return found;
    }

    // Copia 'target' para 'backupDir' e apaga os backups mais antigos alem de 'keep'.
    //
    //  - 'minIntervalSeconds' > 0: nao cria se o backup MAIS RECENTE de 'target' for mais novo que
    //    isso (evita 1 backup por Ctrl+S). 0 = sempre cria.
    //  - 'now' existe so para os testes.
    //
    // Retorna o caminho do backup criado, ou vazio se 'target' nao existe/esta vazio, se o
    // intervalo minimo ainda nao passou ou se a copia falhou.
    inline std::filesystem::path Create(const std::filesystem::path& target,
                                        const std::filesystem::path& backupDir,
                                        int keep = 20,
                                        int minIntervalSeconds = 0,
                                        std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(target, ec))
            return {};
        const auto size = std::filesystem::file_size(target, ec);
        if (ec || size == 0)
            return {}; // nada de valor para guardar
        if (keep < 1)
            keep = 1;

        const std::string stem = target.stem().string();
        const std::string ext = target.extension().string();

        if (minIntervalSeconds > 0) {
            const auto existing = List(target, backupDir);
            if (!existing.empty()) {
                std::string stamp;
                if (detail::MatchesBackupName(existing.front().filename().string(), stem, ext, &stamp)) {
                    const long long last = detail::StampToSeconds(stamp);
                    const long long current = (long long)std::chrono::system_clock::to_time_t(now);
                    if (last >= 0 && current - last >= 0 && current - last < minIntervalSeconds)
                        return {};
                }
            }
        }

        std::filesystem::create_directories(backupDir, ec);
        if (ec)
            return {};

        const std::string stamp = detail::Stamp(now);
        std::filesystem::path destination = backupDir / (stem + "_" + stamp + ext);
        for (int n = 2; std::filesystem::exists(destination, ec) && n < 1000; n++)
            destination = backupDir / (stem + "_" + stamp + "_" + std::to_string(n) + ext);

        ec.clear();
        std::filesystem::copy_file(target, destination, std::filesystem::copy_options::none, ec);
        if (ec)
            return {};

        // Rotacao: apaga do mais antigo para tras ate sobrarem 'keep'. O novo ja conta.
        const auto all = List(target, backupDir);
        for (size_t i = (size_t)keep; i < all.size(); i++) {
            std::error_code removeEc;
            std::filesystem::remove(all[i], removeEc);
        }
        return destination;
    }

}
