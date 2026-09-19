#include "MaterialSerializer.h"
#include "ComponentRegistry.h"
#include "../Core/Log.h"

#include <fstream>
#include <cstdint>
#include <cstring>

namespace Prism {

    // Formato do arquivo .prismmat (little-endian):
    //   [4 bytes] magic "PMAT"
    //   [4 bytes] versao do formato (kMaterialFormatVersion abaixo)
    //   [string]  AlbedoPath
    //   [string]  NormalPath
    //   [string]  RoughnessMetallicPath
    //   [3 floats] AlbedoTint
    //   [float]    RoughnessFactor
    //   [float]    MetallicFactor
    //
    // Sem flag de presenca nem ComponentRegistry aqui, ao contrario de
    // SceneSerializer/PrefabSerializer - este arquivo so representa UM
    // MaterialComponent isolado, entao todos os campos sao sempre
    // gravados/lidos diretamente, na mesma ordem que MaterialComponent os
    // declara (ver Components.h).
    static constexpr uint32_t kMaterialFormatVersion = 1;
    static constexpr char kMagic[4] = { 'P', 'M', 'A', 'T' };

    static void WriteString(std::ofstream& out, const std::string& str) {
        ComponentRegistry::WriteString(out, str);
    }
    static bool ReadString(std::ifstream& in, std::string& outStr) {
        return ComponentRegistry::ReadString(in, outStr);
    }
    template<typename T>
    static void WriteRaw(std::ofstream& out, const T& value) {
        ComponentRegistry::WriteRaw(out, value);
    }
    template<typename T>
    static bool ReadRaw(std::ifstream& in, T& value) {
        return ComponentRegistry::ReadRaw(in, value);
    }

    bool MaterialSerializer::Serialize(const MaterialComponent& material, const std::filesystem::path& filepath) {
        std::error_code ec;
        std::filesystem::create_directories(filepath.parent_path(), ec);

        std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            PRISM_CORE_ERROR("MaterialSerializer: nao foi possivel criar o arquivo de material: ", filepath.string());
            return false;
        }

        out.write(kMagic, sizeof(kMagic));
        WriteRaw(out, kMaterialFormatVersion);
        WriteString(out, material.AlbedoPath);
        WriteString(out, material.NormalPath);
        WriteString(out, material.RoughnessMetallicPath);
        WriteRaw(out, material.AlbedoTint);
        WriteRaw(out, material.RoughnessFactor);
        WriteRaw(out, material.MetallicFactor);

        if (!out) {
            PRISM_CORE_ERROR("MaterialSerializer: falha ao escrever dados do material em: ", filepath.string());
            return false;
        }

        PRISM_CORE_INFO("Material salvo em: ", filepath.string());
        return true;
    }

    bool MaterialSerializer::Deserialize(const std::filesystem::path& filepath, MaterialComponent& outMaterial) {
        std::ifstream in(filepath, std::ios::binary);
        if (!in.is_open()) {
            PRISM_CORE_ERROR("MaterialSerializer: nao foi possivel abrir o arquivo de material: ", filepath.string());
            return false;
        }

        char magic[4];
        in.read(magic, sizeof(magic));
        if (!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
            PRISM_CORE_ERROR("MaterialSerializer: '", filepath.string(), "' nao e um arquivo de material Prism valido (magic incorreto).");
            return false;
        }

        uint32_t version = 0;
        if (!ReadRaw(in, version) || version != kMaterialFormatVersion) {
            PRISM_CORE_ERROR("MaterialSerializer: versao de formato incompativel em '", filepath.string(),
                "' (arquivo=v", version, ", esperado=v", kMaterialFormatVersion, ").");
            return false;
        }

        // Le tudo para uma copia LOCAL primeiro - so escreve em
        // 'outMaterial' se TODOS os campos forem lidos com sucesso (ver
        // comentario no .h: uma leitura que falha nao deve deixar o
        // material do chamador pela metade).
        MaterialComponent loaded;
        bool ok = ReadString(in, loaded.AlbedoPath)
               && ReadString(in, loaded.NormalPath)
               && ReadString(in, loaded.RoughnessMetallicPath)
               && ReadRaw(in, loaded.AlbedoTint)
               && ReadRaw(in, loaded.RoughnessFactor)
               && ReadRaw(in, loaded.MetallicFactor);

        if (!ok) {
            PRISM_CORE_ERROR("MaterialSerializer: arquivo de material corrompido: ", filepath.string());
            return false;
        }

        outMaterial = loaded;
        PRISM_CORE_INFO("Material carregado de: ", filepath.string());
        return true;
    }

}
