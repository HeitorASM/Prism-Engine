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
// ASSIM COMO PREFAB (ver PrefabSerializer.h), NAO ha vinculo vivo entre o
// arquivo e as entidades que carregaram dele - "Carregar de Asset" COPIA
// os campos para o MaterialComponent da entidade selecionada no momento;
// editar o .prismmat depois nao propaga automaticamente para entidades
// que ja carregaram dele antes. Um sistema de referencia compartilhada
// (todas as entidades apontando para o MESMO material em memoria,
// mudanca em uma reflete em todas) e uma funcionalidade maior, que fica
// para uma iteracao futura caso vire necessidade real de producao.
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
