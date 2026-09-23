#pragma once

// ============================================================================
// AssetMeta.h
// O arquivo ".meta" que acompanha cada asset e guarda a IDENTIDADE dele:
//     Assets/Textures/Rock.png
//     Assets/Textures/Rock.png.meta      <- este arquivo
//
// Formato TEXTO (Chave=Valor, '#' = comentario), igual ao .prismproj:
// legivel, e produz diffs pequenos e sem conflito no controle de versao.
//
//     # Prism asset meta - nao apague; guarda a identidade do asset.
//     Version=1
//     ID=8f3a1c5d2b7e9041
//     Type=Texture
//
// PS: mover/renomear um asset SEMPRE junto com o
// seu .meta preserva o ID (e todas as referencias a ele). Apagar so o .meta
// gera um ID novo na proxima varredura - e as referencias antigas ficam
// penduradas (o AssetRegistry avisa disso).
//
// Chaves DESCONHECIDAS sao ignoradas na leitura (compatibilidade para
// frente: uma versao futura pode acrescentar campos sem quebrar esta).
// ============================================================================

#include "AssetID.h"
#include <filesystem>
#include <string>

namespace Prism {

    enum class AssetType {
        Unknown,
        Texture,   // .png .jpg .jpeg .bmp .tga
        Material,  // .prismmat
        Prefab,    // .prismprefab
        Scene,     // .prismmap
        Script,    // .lua
        Model,     // .obj .fbx .gltf .glb (importacao ainda nao existe; ja ganha identidade)
        Audio      // .wav .ogg .mp3
    };

    const char* AssetTypeToString(AssetType type);

    // Nome desconhecido devolve AssetType::Unknown (nunca falha) - um .meta
    // gravado por uma versao futura com um tipo novo continua legivel.
    AssetType AssetTypeFromString(const std::string& text);

    // Deduz o tipo pela extensao (case-insensitive: "ROCK.PNG" -> Texture).
    AssetType AssetTypeFromExtension(const std::filesystem::path& file);

    struct AssetMeta {
        static constexpr uint32_t kCurrentVersion = 1;

        uint32_t  Version = kCurrentVersion;
        AssetID   ID;
        AssetType Type = AssetType::Unknown;
    };

    class AssetMetaFile {
    public:
        // "Rock.png" -> "Rock.png.meta". A extensao original e MANTIDA (nao
        // trocada), senao "Rock.png" e "Rock.jpg" na mesma pasta disputariam
        // o mesmo "Rock.meta".
        static std::filesystem::path MetaPathFor(const std::filesystem::path& assetPath);

        static bool IsMetaFile(const std::filesystem::path& file);

        // Le 'metaPath'. Retorna false (e deixa 'out' INTACTO) se o arquivo
        // nao existe, nao tem um ID valido, ou esta corrompido. Um .meta sem
        // ID valido NAO e aceito: o ID e a unica razao de o arquivo existir.
        static bool Read(const std::filesystem::path& metaPath, AssetMeta& out);

        // Grava de forma ATOMICA: escreve num arquivo temporario ao lado e
        // so entao troca pelo definitivo (rename). Um crash/queda de luz no
        // meio da escrita nunca deixa um .meta pela metade - o antigo
        // continua intacto. Cria a pasta de destino se preciso.
        static bool Write(const std::filesystem::path& metaPath, const AssetMeta& meta);
    };

}
