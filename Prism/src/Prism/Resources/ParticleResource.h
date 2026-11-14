#pragma once

// ============================================================================
// ParticleResource.h
// Conversao ParticleEmitterComponent <-> recurso generico (.prismres, ver Resource.h)
// com o tipo "ParticleEmitter". E tudo o que o formato precisa saber sobre particulas.
//
// O QUE ENTRA NO RECURSO: a DEFINICAO do efeito (forma, vida, velocidade, tamanho,
// cor, curvas, mistura, textura, flipbook...). O que e da INSTANCIA fica de fora:
// Enabled e PlayOnStart (cada entidade decide se liga) e LinkedAsset (o vinculo vive
// na entidade, nunca no arquivo - mesma regra do .prismmat).
//
// FORMATO (texto; os nomes das chaves sao os campos do component):
//
//     prism_resource type="ParticleEmitter" version=1
//
//     Loop = true
//     EmissionRate = 40.0
//     Shape = "Sphere"            # nomes, nao numeros: legivel e a salvo de reordenar enums
//     BlendMode = "Additive"
//     SizeOverLife = [[0.0, 1.0], [0.5, 2.0], [1.0, 0.0]]       # [t, valor]
//     ColorOverLife = Gradient { Stops = [[0.0, 1,1,1,1], ...] } # [t, r, g, b, a]
//
// LEITURA TOLERANTE: chave ausente = valor padrao do component; chave desconhecida ou
// com tipo errado = ignorada; valores passam por ParticleEmitterComponent::Sanitize().
// Assim um recurso escrito a mao (ou por uma versao futura) nunca quebra o carregamento.
// ============================================================================

#include "Resource.h"
#include "../Scene/Components.h"

namespace Prism {

    namespace ParticleResource {

        constexpr const char* kTypeName = "ParticleEmitter";
        constexpr uint32_t kVersion = 1;

        // Definicao do efeito (sem Enabled/PlayOnStart/LinkedAsset).
        ResourceDocument ToResource(const ParticleEmitterComponent& emitter);

        // Aplica o recurso em 'emitter': TODOS os campos da definicao vem do recurso (ausentes =
        // padrao); Enabled, PlayOnStart e LinkedAsset do 'emitter' sao preservados. Se o tipo do
        // recurso nao for "ParticleEmitter", devolve false (com 'error') e NAO altera 'emitter'.
        bool FromResource(const ResourceDocument& doc, ParticleEmitterComponent& emitter, std::string* error = nullptr);

        // Atalhos de arquivo (Resource Load/Save). O formato e escolhido pelo chamador.
        bool Save(const std::filesystem::path& file, const ParticleEmitterComponent& emitter,
                  ResourceFormat format = ResourceFormat::Text, std::string* error = nullptr);
        bool Load(const std::filesystem::path& file, ParticleEmitterComponent& emitter, std::string* error = nullptr);
        // Formato pela extensao: .prismres = binario, .prismtres = texto.
        bool SaveByExtension(const std::filesystem::path& file, const ParticleEmitterComponent& emitter, std::string* error = nullptr);

    }

}
