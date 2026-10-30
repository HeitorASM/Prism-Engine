#pragma once

// ============================================================================
// ModelImport.h
// Lado do EDITOR da importacao de modelos 3D: usa Prism::ModelLoader para
// ler o arquivo e traduz o resultado (submeshes + materiais) em dados prontos
// para virar entidades na cena - uma "parte" por material, cada uma com o seu
// MaterialComponent ja com os caminhos de textura RELATIVOS a pasta do
// projeto (a convencao de MaterialComponent::AlbedoPath etc).
//
// Texturas embutidas no arquivo (ex: .glb) sao extraidas para
// "<pasta do modelo>/<nome do modelo>_textures/" - e o unico lugar onde a
// importacao escreve no disco, e so acontece aqui (nunca em runtime).
// ============================================================================

#include <Prism.h>
#include <filesystem>
#include <string>
#include <vector>

namespace PrismEditor::ModelImport {

    struct Part {
        std::string Name;
        int SubmeshIndex = -1;            // vai para MeshRendererComponent::SubmeshIndex
        Prism::MaterialComponent Material;
    };

    struct PreparedModel {
        bool Success = false;
        std::string Error;                // legivel, so se Success == false
        Prism::AssetID Asset;             // AssetID do arquivo de modelo
        std::string Name;                 // nome do arquivo sem extensao
        std::vector<Part> Parts;          // um por material do modelo
        bool HasRig = false;              // o modelo tem esqueleto e/ou animacoes (ganha um Animator)
        std::string FirstClip;            // primeiro clipe de animacao ("" se nao houver)
        size_t ClipCount = 0;
    };

    // Resolve o AssetID de 'absoluteModelPath' (o arquivo precisa estar dentro
    // da pasta de Assets do projeto), le o modelo extraindo as texturas
    // embutidas e monta uma Part por material. Chamada pesada (le o arquivo
    // inteiro) - use so em acoes do usuario, nunca por frame.
    PreparedModel Prepare(const std::filesystem::path& absoluteModelPath);

}
