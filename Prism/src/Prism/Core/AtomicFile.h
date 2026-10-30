#pragma once

// ============================================================================
// AtomicFile.h
// Gravacao ATOMICA de arquivos: o serializador escreve num arquivo temporario
// ao lado ("mapa.prismmap.tmp") e so depois de terminar tudo com sucesso
// troca pelo definitivo. Um crash, falta de disco ou erro no meio do save nunca
// deixa o mapa/prefab pela metade: ou fica o arquivo antigo inteiro, ou o novo
// inteiro. (O .prismproj e o .meta usam o mesmo padrao, cada um com a sua copia.)
// ============================================================================

#include <filesystem>
#include <system_error>

namespace Prism {

    // Caminho do temporario para 'target' (mesma pasta, para o rename ser
    // atomico no mesmo volume).
    inline std::filesystem::path AtomicTempPath(const std::filesystem::path& target) {
        std::filesystem::path temp = target;
        temp += ".tmp";
        return temp;
    }

    // Troca 'target' por 'temp' (que ja deve estar completo e FECHADO). Em
    // falha apaga o temporario. true = 'target' agora e o conteudo novo.
    inline bool CommitAtomicFile(const std::filesystem::path& temp, const std::filesystem::path& target) {
        std::error_code ec;
        std::filesystem::rename(temp, target, ec);
        if (ec) {
            // Alguns sistemas de arquivos recusam rename sobre arquivo existente.
            ec.clear();
            std::filesystem::copy_file(temp, target, std::filesystem::copy_options::overwrite_existing, ec);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return !ec;
        }
        return true;
    }

    inline void DiscardAtomicFile(const std::filesystem::path& temp) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
    }

}
