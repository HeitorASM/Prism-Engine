#include <glad/gl.h>
#include "ThumbnailCache.h"
#include "../Assets/ThumbnailImage.h"
#include "../Core/DiskCache.h"
#include "../Core/Log.h"
#include "../Project/Project.h"

#include <stb_image.h> // so as declaracoes: STB_IMAGE_IMPLEMENTATION vive em Texture.cpp

#include <chrono>
#include <unordered_map>

namespace Prism {

    namespace {

        constexpr uint64_t kThumbnailFormat = 1;               // sobe se o conteudo da miniatura mudar de significado
        constexpr uint64_t kMaxCacheBytes = 256ull * 1024 * 1024;
        constexpr uint64_t kMaxSourcePixels = 160ull * 1000 * 1000; // evita decodificar imagens absurdas inteiras na memoria
        constexpr double kRecheckSeconds = 1.0;                // quanto tempo confiar no tamanho/data ja lidos do arquivo

        struct Entry {
            uint64_t Stamp = 0;                                // tamanho + data de modificacao
            double CheckedAt = -1e9;
            Scope<Texture2D> Texture;                          // null = falhou
        };

        std::unordered_map<std::string, Entry> s_Entries;
        uint32_t s_BuildsThisFrame = 0;
        bool s_Pruned = false;

        double Now() {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        std::filesystem::path CacheDirectory() {
            if (auto project = Project::GetActive())
                return project->GetProjectDirectory() / "Cache" / "Thumbnails";
            return {};
        }

        bool Stamp(const std::filesystem::path& file, uint64_t& stamp) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(file, ec);
            if (ec) return false;
            const auto time = std::filesystem::last_write_time(file, ec);
            if (ec) return false;
            const uint64_t ticks = (uint64_t)time.time_since_epoch().count();
            const uint64_t bytes = (uint64_t)size;
            stamp = DiskCache::Hash64(&bytes, sizeof(bytes), DiskCache::Hash64(&ticks, sizeof(ticks)));
            return true;
        }

        bool Decode(const std::filesystem::path& file, ThumbnailPixels& out) {
            const std::string path = file.string();
            int width = 0, height = 0, channels = 0;
            if (!stbi_info(path.c_str(), &width, &height, &channels) || width <= 0 || height <= 0
                || (uint64_t)width * (uint64_t)height > kMaxSourcePixels)
                return false;
            stbi_set_flip_vertically_on_load(true); // mesma orientacao de Texture2D (o navegador desenha com o V invertido)
            stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
            if (!pixels)
                return false;
            out = ThumbnailImage::Downscale(pixels, (uint32_t)width, (uint32_t)height, ThumbnailCache::kMaxSide);
            stbi_image_free(pixels);
            return out.Width > 0;
        }

    }

    void ThumbnailCache::NewFrame() {
        s_BuildsThisFrame = 0;
    }

    void ThumbnailCache::Clear() {
        s_Entries.clear();
    }

    Texture2D* ThumbnailCache::Get(const std::filesystem::path& file) {
        const std::string pathKey = file.generic_string();
        Entry& entry = s_Entries[pathKey];
        const double now = Now();

        if (now - entry.CheckedAt < kRecheckSeconds)
            return entry.Texture.get();

        uint64_t stamp = 0;
        if (!Stamp(file, stamp)) { // arquivo sumiu
            s_Entries.erase(pathKey);
            return nullptr;
        }
        if (stamp == entry.Stamp && entry.CheckedAt > -1e8) { // mesma versao do arquivo
            entry.CheckedAt = now;
            return entry.Texture.get();
        }
        if (s_BuildsThisFrame >= kBuildsPerFrame)
            return entry.Texture.get(); // fica para o proximo quadro (a versao antiga, se houver, continua aparecendo)
        s_BuildsThisFrame++;

        entry.Stamp = stamp;
        entry.CheckedAt = now;

        const std::filesystem::path directory = CacheDirectory();
        const std::string key = DiskCache::KeyName({ DiskCache::Hash64(pathKey), stamp, kThumbnailFormat });

        ThumbnailPixels pixels;
        bool loaded = false;
        if (!directory.empty()) {
            if (!s_Pruned) {
                s_Pruned = true;
                DiskCache::Prune(directory, kMaxCacheBytes);
            }
            std::vector<uint8_t> bytes;
            loaded = DiskCache::Read(directory, key, bytes) && ThumbnailImage::Unpack(bytes, pixels, kMaxSide);
        }
        if (!loaded && Decode(file, pixels)) {
            loaded = true;
            if (!directory.empty())
                DiskCache::Write(directory, key, ThumbnailImage::Pack(pixels));
        }

        if (!loaded) {
            entry.Texture.reset();
            return nullptr;
        }
        entry.Texture = CreateScope<Texture2D>(pixels.Width, pixels.Height, pixels.RGBA.data(), /*isSRGB*/ true, pathKey);
        if (!entry.Texture->IsValid())
            entry.Texture.reset();
        return entry.Texture.get();
    }

}
