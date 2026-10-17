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
// ESCOPO ATUAL (v2 da importacao):
//   - Um arquivo de modelo pode conter varios "meshes" do Assimp. Eles sao
//     AGRUPADOS POR MATERIAL em ImportedSubmesh (um submesh por material do
//     arquivo, com a hierarquia de nos "gravada" nos vertices). O editor usa
//     isso para criar uma entidade por submesh, cada uma com o seu
//     MaterialComponent (ver MeshRendererComponent::SubmeshIndex). A malha
//     mesclada de TODOS os submeshes (ModelImportResult::Mesh) continua
//     disponivel para quem so precisa da geometria inteira.
//   - Materiais: cada ImportedSubmesh traz um ImportedMaterial com os
//     caminhos (ABSOLUTOS) das texturas de albedo/normal/roughness-metallic
//     e os fatores (cor base, roughness, metallic). Texturas EMBUTIDAS no
//     arquivo (ex: .glb) so sao gravadas em disco quando
//     ModelImportOptions::ExtractEmbeddedTextures e true - o carregamento
//     em runtime do Renderer nunca escreve nada no disco.
//   - NAO e importado: esqueleto/skinning (animacao), camera/luzes embutidas
//     no arquivo, nem a hierarquia de nos como entidades separadas.
// ============================================================================

#include "../Renderer/PrimitiveMeshFactory.h" // GeneratedMesh
#include <glm/glm.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace Prism {

    // Material de um submesh, ja traduzido para o modelo PBR metallic/
    // roughness da engine (ver MaterialComponent, Components.h). Caminhos
    // ABSOLUTOS (vazio = sem aquela textura) - quem consome converte para
    // relativo a pasta do projeto.
    struct ImportedMaterial {
        std::string Name;
        std::filesystem::path AlbedoPath;
        std::filesystem::path NormalPath;
        std::filesystem::path RoughnessMetallicPath; // convencao glTF: G = roughness, B = metallic
        glm::vec3 AlbedoTint = { 1.0f, 1.0f, 1.0f };
        float Roughness = 0.5f;
        float Metallic = 0.0f;
    };

    // Todos os triangulos do arquivo que usam o MESMO material.
    struct ImportedSubmesh {
        std::string Name; // nome do material (ou "Submesh N" se o material nao tiver nome)
        GeneratedMesh Mesh;
        ImportedMaterial Material;
    };

    struct ModelImportOptions {
        // true: grava em 'TextureOutputDir' as texturas embutidas no arquivo
        // (ex: imagens dentro de um .glb) e devolve o caminho do arquivo
        // gravado. false (padrao): texturas embutidas sao ignoradas.
        bool ExtractEmbeddedTextures = false;
        std::filesystem::path TextureOutputDir;
    };

    struct ModelImportResult {
        bool Success = false;
        std::string Error;                   // vazio se Success == true; mensagem legivel se false
        GeneratedMesh Mesh;                  // TODOS os submeshes mesclados - valido somente se Success == true
        std::vector<ImportedSubmesh> Submeshes; // um por material usado - valido somente se Success == true
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
        static ModelImportResult Load(const std::filesystem::path& absolutePath, const ModelImportOptions& options = {});

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
