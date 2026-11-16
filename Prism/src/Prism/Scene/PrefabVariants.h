#pragma once

// ============================================================================
// PrefabVariants.h
// Variante de prefab: um .prismprefab COMPLETO (instancia, sincroniza e edita
// como qualquer prefab) que lembra de qual prefab BASE veio (cabecalho v11,
// ver PrefabSerializer.h). Quando o base muda, Update() reaplica as mudancas
// do base na variante sem perder o que a variante sobrescreveu:
//   - entidade nova no base (GUID desconhecido)  -> entra na variante;
//   - entidade removida do base                  -> sai da variante;
//   - component cujo valor na variante ainda e o do base da ultima sincronizacao
//     (hash igual) -> acompanha o novo valor do base; se a variante mudou, e
//     override e fica como esta (mesma regra "component inteiro" do PrefabSyncer).
// A raiz nunca sincroniza o Transform (igual as instancias).
// Prefab aninhado (um prefab contendo instancia de outro) NAO faz parte disto.
// ============================================================================

#include "../Assets/AssetID.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Prism {

    struct PrefabVariantUpdateResult {
        bool Ok = false;          // false = nao leu base ou variante (nada foi gravado)
        bool Changed = false;     // o arquivo da variante foi regravado
        int Added = 0, Removed = 0, ComponentsUpdated = 0;
        std::string Error;
    };

    struct PrefabVariantChange {
        std::filesystem::path Path;
        std::vector<uint64_t> PreviousGuids;   // GUIDs do arquivo ANTES da atualizacao (base para o sync das instancias)
    };

    class PrefabVariants {
    public:
        // Hash estavel (FNV-1a 64) de bytes / nome.
        static uint64_t Hash(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);

        // Cria 'variantPath' como copia do base com o cabecalho de variante preenchido.
        static bool Create(const std::filesystem::path& basePath, AssetID baseAsset, const std::filesystem::path& variantPath);

        // Reaplica o base na variante (ver topo). Sem mudanca nao regrava o arquivo.
        static PrefabVariantUpdateResult Update(const std::filesystem::path& variantPath, const std::filesystem::path& basePath);

        // Variantes diretas de 'baseAsset' dentro de 'rootDir' (varredura recursiva de .prismprefab).
        static std::vector<std::filesystem::path> FindVariantsOf(AssetID baseAsset, const std::filesystem::path& rootDir);

        // Atualiza, em cascata (variante de variante), todas as variantes que dependem de 'baseAsset'.
        // 'resolve' converte AssetID -> caminho; 'idOf' caminho -> AssetID (para seguir a cascata).
        // Protegido contra ciclo. Devolve os arquivos regravados.
        static std::vector<PrefabVariantChange> UpdateDependents(AssetID baseAsset, const std::filesystem::path& rootDir,
                                                                   const std::function<std::filesystem::path(AssetID)>& resolve,
                                                                   const std::function<AssetID(const std::filesystem::path&)>& idOf);
    };

}
