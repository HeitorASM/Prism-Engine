#pragma once

// ============================================================================
// ModelLoader.h
// Importacao de modelos 3D de terceiros (.obj/.fbx/.gltf/.glb) via Assimp,
// convertidos para o MESMO formato interno (GeneratedMesh: MeshVertex +
// indices) que PrimitiveMeshFactory ja produz para as primitivas embutidas
// (ver PrimitiveMeshFactory.h) - o resto da engine (Mesh::Create,
// Renderer::DrawMesh) nunca precisa saber se uma malha veio de uma formula
// (esfera/cubo/...) ou de um arquivo importado.
//
// ESCOPO ATUAL (v1 da importacao):
//   - Um arquivo de modelo pode conter varios "meshes" da parte da Assimp
//     (um por material/submesh, ex: um carro com "Carroceria"/"Vidro"/
//     "Pneu" separados). Esta engine ainda nao tem multi-material por
//     entidade (MaterialComponent e UM material por entidade, ver
//     Components.h) - por isso TODOS os submeshes do arquivo sao
//     mesclados (merge) numa UNICA GeneratedMesh, na leitura, perdendo a
//     divisao por material. Reflete diretamente a limitacao ja documentada
//     em MaterialComponent: "um material por entidade".
//   - So a HIERARQUIA DE VERTICES/TRIANGULOS e importada - nao ha
//     importacao de: esqueleto/skinning (animacao), camera/luzes embutidas
//     no arquivo, ou multiplos nos (cada no do arquivo vira so mais
//     triangulos mesclados na mesma malha, sem preservar a hierarquia de
//     transformacoes entre nos). Ver docs/formatos-de-arquivo.md para o
//     estado exato desta limitacao.
//   - Texturas embutidas no proprio arquivo (ex: .glb com imagens
//     empacotadas) NAO sao extraidas ainda - so a geometria. O material
//     visual de uma entidade com modelo importado continua vindo de
//     MaterialComponent, configurado a parte pelo usuario no editor.
// ============================================================================

#include "../Renderer/PrimitiveMeshFactory.h" // GeneratedMesh
#include <filesystem>
#include <string>

namespace Prism {

    struct ModelImportResult {
        bool Success = false;
        std::string Error;     // vazio se Success == true; mensagem legivel se false
        GeneratedMesh Mesh;     // valido somente se Success == true
    };

    class ModelLoader {
    public:
        // Carrega 'absolutePath' (arquivo .obj/.fbx/.gltf/.glb no disco) e
        // devolve a malha mesclada pronta para Mesh::Create - ver comentario
        // grande acima sobre o que e/nao e preservado. Chamada SINCRONA e
        // relativamente pesada (parsing do arquivo inteiro, triangulacao,
        // calculo de tangentes) - pensada para ser usada na hora de
        // (re)carregar um asset de Model (Renderer::GetOrLoadModelMesh, ver
        // Renderer.h), nunca dentro do loop de desenho por frame.
        static ModelImportResult Load(const std::filesystem::path& absolutePath);

        // Extensoes de arquivo suportadas por esta engine (subconjunto do
        // que o Assimp entende de verdade - ver ASSIMP_BUILD_*_IMPORTER em
        // vendor/CMakeLists.txt, que liga so os importadores destes
        // formatos para nao inflar o tempo de build). Minusculo, com ponto
        // (".obj", ".fbx", ".gltf", ".glb") - mesma convencao de
        // AssetTypeFromExtension (AssetMeta.h). Usada pelo Content Browser
        // do editor para decidir quais arquivos sao "modelos arrastaveis"
        // e por AssetTypeFromExtension para classificar como AssetType::Model.
        static bool IsSupportedExtension(const std::filesystem::path& file);
    };

}
