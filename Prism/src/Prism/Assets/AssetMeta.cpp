#include "AssetMeta.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace Prism {

    // --- AssetType -------------------------------------------------------

    const char* AssetTypeToString(AssetType type) {
        switch (type) {
            case AssetType::Texture:  return "Texture";
            case AssetType::Material: return "Material";
            case AssetType::Prefab:   return "Prefab";
            case AssetType::Scene:    return "Scene";
            case AssetType::Script:   return "Script";
            case AssetType::Model:    return "Model";
            case AssetType::Audio:    return "Audio";
            case AssetType::Unknown:  break;
        }
        return "Unknown";
    }

    AssetType AssetTypeFromString(const std::string& text) {
        static const AssetType kAll[] = {
            AssetType::Texture, AssetType::Material, AssetType::Prefab,
            AssetType::Scene, AssetType::Script, AssetType::Model, AssetType::Audio
        };
        for (AssetType t : kAll)
            if (text == AssetTypeToString(t))
                return t;
        return AssetType::Unknown;
    }

    AssetType AssetTypeFromExtension(const std::filesystem::path& file) {
        std::string ext = file.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") return AssetType::Texture;
        if (ext == ".prismmat")    return AssetType::Material;
        if (ext == ".prismprefab") return AssetType::Prefab;
        if (ext == ".prismmap")    return AssetType::Scene;
        if (ext == ".lua")         return AssetType::Script;
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") return AssetType::Model;
        if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return AssetType::Audio;
        return AssetType::Unknown;
    }

    // --- AssetMetaFile ---------------------------------------------------

    std::filesystem::path AssetMetaFile::MetaPathFor(const std::filesystem::path& assetPath) {
        std::filesystem::path p = assetPath;
        p += ".meta"; // += concatena SEM separador: "Rock.png" -> "Rock.png.meta"
        return p;
    }

    bool AssetMetaFile::IsMetaFile(const std::filesystem::path& file) {
        return file.extension() == ".meta";
    }

    static std::string Trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) a++;
        while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
        return s.substr(a, b - a);
    }

    bool AssetMetaFile::Read(const std::filesystem::path& metaPath, AssetMeta& out) {
        std::ifstream in(metaPath);
        if (!in.is_open())
            return false;

        // Le para uma copia LOCAL e so devolve em caso de sucesso - mesma
        // garantia "nunca deixa pela metade" de MaterialSerializer::Deserialize.
        AssetMeta parsed;
        bool haveID = false;

        std::string line;
        while (std::getline(in, line)) {
            line = Trim(line); // tambem remove o '\r' de arquivos CRLF (Windows)
            if (line.empty() || line[0] == '#')
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue; // linha sem '=' nao e Chave=Valor: ignora

            std::string key = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));

            if (key == "Version") {
                try { parsed.Version = (uint32_t)std::stoul(value); }
                catch (...) { /* mantem o padrao - Version e informativo */ }
            }
            else if (key == "ID") {
                haveID = AssetID::TryParse(value, parsed.ID);
            }
            else if (key == "Type") {
                parsed.Type = AssetTypeFromString(value);
            }
            // demais chaves: ignoradas de proposito (ver comentario no .h)
        }

        if (!haveID || !parsed.ID.IsValid())
            return false;

        out = parsed;
        return true;
    }

    bool AssetMetaFile::Write(const std::filesystem::path& metaPath, const AssetMeta& meta) {
        if (!meta.ID.IsValid()) {
            PRISM_CORE_ERROR("AssetMetaFile: recusando gravar um .meta com ID invalido (0): ", metaPath.string());
            return false;
        }

        std::error_code ec;
        if (metaPath.has_parent_path())
            std::filesystem::create_directories(metaPath.parent_path(), ec);

        // Escrita atomica: temporario ao lado + rename. rename sobre um
        // arquivo existente e atomico no mesmo volume (POSIX e, no Windows,
        // std::filesystem::rename usa MoveFileEx com REPLACE_EXISTING).
        std::filesystem::path tempPath = metaPath;
        tempPath += ".tmp";

        {
            std::ofstream out(tempPath, std::ios::trunc);
            if (!out.is_open()) {
                PRISM_CORE_ERROR("AssetMetaFile: nao foi possivel criar o arquivo temporario: ", tempPath.string());
                return false;
            }

            out << "# Prism asset meta - nao apague; guarda a identidade do asset.\n";
            out << "Version=" << meta.Version << "\n";
            out << "ID=" << meta.ID.ToString() << "\n";
            out << "Type=" << AssetTypeToString(meta.Type) << "\n";
            out.flush();

            if (!out) {
                out.close();
                std::filesystem::remove(tempPath, ec);
                PRISM_CORE_ERROR("AssetMetaFile: falha ao escrever: ", tempPath.string());
                return false;
            }
        }

        std::filesystem::rename(tempPath, metaPath, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            PRISM_CORE_ERROR("AssetMetaFile: falha ao gravar '", metaPath.string(), "': ", ec.message());
            return false;
        }
        return true;
    }

}
