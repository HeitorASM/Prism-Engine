#pragma once

// ============================================================================
// ThumbnailCache.h
// Miniaturas de imagens para o navegador de conteudo do editor.
//
// Antes, cada imagem visivel era carregada INTEIRA (decodificacao + upload +
// mipmaps de, por exemplo, 4096x4096) na thread da interface, e ficava toda na
// memoria da GPU. Agora:
//   * a miniatura (ate kMaxSide px) e gerada uma vez e guardada em
//     <Projeto>/Cache/Thumbnails (DiskCache, chave = caminho + tamanho + data
//     de modificacao): abrir a pasta de novo, ou o projeto de novo, so le
//     arquivos pequenos;
//   * no maximo kBuildsPerFrame miniaturas sao GERADAS por quadro (as demais
//     aparecem nos quadros seguintes, o navegador nao trava);
//   * editar/substituir a imagem gera outra miniatura sozinho.
//
// Precisa do contexto OpenGL atual (cria a textura) - thread principal.
// ============================================================================

#include "Texture.h"
#include "../Core/Base.h"
#include <filesystem>

namespace Prism {

    class ThumbnailCache {
    public:
        static constexpr uint32_t kMaxSide = 128;
        static constexpr uint32_t kBuildsPerFrame = 3;

        // Chame uma vez por quadro, antes dos pedidos (renova o orcamento de geracao).
        static void NewFrame();

        // Miniatura da imagem, ou nullptr se ainda nao esta pronta (orcamento do quadro
        // esgotado) ou se a imagem nao pode ser lida (nao tenta de novo ate o arquivo mudar).
        static Texture2D* Get(const std::filesystem::path& absoluteImagePath);

        // Esquece todas as texturas em memoria (o cache em disco continua).
        static void Clear();
    };

}
