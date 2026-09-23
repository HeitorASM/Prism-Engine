#pragma once

// ============================================================================
// MaterialSerializer.h
// Um "Material Asset" e um MaterialComponent (ver Components.h) salvo
// SOZINHO em disco, para ser REUTILIZADO por varias entidades - o mesmo
// conceito do asset ".mat" da Unity ou do ".tres" de material da Godot.
// Arquivos ficam em Project::GetAssetDirectory() / "Materials", extensao
// .prismmat.
//
// DIFERENTE DE UM PREFAB: um Material Asset nao tem entidade nenhuma, so
// os campos de UM MaterialComponent (AlbedoPath/NormalPath/
// RoughnessMetallicPath/AlbedoTint/RoughnessFactor/MetallicFactor) - o
// arquivo mais simples que existe na engine (sem lista de entidades, sem
// ComponentRegistry, sem RelationshipComponent).
//
// VINCULO VIVO (ver MaterialComponent::LinkedAsset em Components.h): este
// serializador so grava/le os SEIS campos de material listados acima -
// NUNCA o AssetID de ninguem. O vinculo entre uma entidade e este arquivo
// vive em MaterialComponent::LinkedAsset (na entidade, resolvido via
// Project::GetAssetRegistry() - ver Assets/AssetRegistry.h), nunca AQUI
// no arquivo: um .prismmat nao sabe, e nao precisa saber, quais entidades
// apontam para ele hoje. Por isso Deserialize() preenche 'outMaterial'
// SEM tocar em LinkedAsset - quem decide se o resultado fica vinculado
// e o chamador (ver EditorLayer::RenderPropertiesPanel, secao Material):
//   - "Carregar de Asset" / soltar um .prismmat no painel: LIGA o vinculo
//     (seta LinkedAsset com o AssetID do arquivo solto/escolhido).
//   - "Salvar como Asset...": NAO liga vinculo nenhum - e uma copia
//     pontual dos valores atuais para um arquivo novo (ou existente,
//     sobrescrevendo), mesmo com a entidade ja estando vinculada a OUTRO
//     asset; ligar o vinculo ao arquivo recem-salvo seria uma segunda
//     acao implicita que o botao nunca prometeu.
// Enquanto vinculado, EditorLayer::FlushMaterialLinkSave() grava os seis
// campos de volta neste MESMO arquivo (debounce, mesmo padrao de
// FlushRenderSettingsSave) a cada edicao no painel, e
// EditorLayer::ReconcileLinkedMaterial() releva o arquivo (comparando a
// hora de modificacao) para refletir a mudanca em QUALQUER OUTRA entidade
// com o mesmo LinkedAsset - inclusive entidades que nunca tocaram no
// painel Material, so tem o mesmo AssetID herdado de um prefab (ver nota
// em PrefabSerializer.cpp) ou carregado independentemente.
// ============================================================================

#include "../Core/Base.h"
#include "Components.h"
#include <filesystem>

namespace Prism {

    class MaterialSerializer {
    public:
        // Salva os campos de 'material' em 'filepath'. Cria a pasta de
        // destino se nao existir (mesmo comportamento de
        // PrefabSerializer::Serialize).
        static bool Serialize(const MaterialComponent& material, const std::filesystem::path& filepath);

        // Le 'filepath' e preenche 'outMaterial' com os campos salvos.
        // 'outMaterial' so e modificado em caso de sucesso (retorno true) -
        // uma leitura que falha no meio nunca deixa o material do
        // chamador pela metade.
        static bool Deserialize(const std::filesystem::path& filepath, MaterialComponent& outMaterial);
    };

}
