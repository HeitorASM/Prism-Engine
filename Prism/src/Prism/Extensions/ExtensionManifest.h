#pragma once

// ============================================================================
// ExtensionManifest.h
// O arquivo `extension.ini` de uma extensao + a resolucao da ordem de carga
// (dependencias). So usa a std: testavel sem OpenGL, sem dll e sem engine.
//
// Formato (uma chave por linha; '#' ou ';' comeca comentario):
//     name        = Health            (obrigatorio: letras, numeros, _ . -)
//     api         = 1                 (obrigatorio: kExtensionApiVersion para a qual foi compilada)
//     version     = 1.0.0
//     description = Vida e dano
//     author      = Fulano
//     library     = Health            (nome do binario sem extensao; padrao = name)
//     target      = both              (both | editor | runtime)
//     depends     = Outra, MaisUma    (extensoes que precisam carregar ANTES)
//     enabled     = true
// Chaves desconhecidas viram aviso (versoes futuras do manifesto continuam abrindo).
// ============================================================================

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Prism {

    enum class ExtensionTarget { Both, Editor, Runtime };

    struct ExtensionManifest {
        std::string Name;
        std::string Version = "0.0.0";
        std::string Description;
        std::string Author;
        std::string Library;                 // sem extensao; nunca contem separador de caminho
        ExtensionTarget Target = ExtensionTarget::Both;
        uint32_t ApiVersion = 0;
        std::vector<std::string> Dependencies;
        bool Enabled = true;
    };

    struct ManifestParseResult {
        bool Ok = false;
        std::string Error;                   // preenchido quando !Ok
        std::vector<std::string> Warnings;   // chaves desconhecidas etc.
        ExtensionManifest Manifest;
    };

    // Nome valido de extensao/dependencia: 1..64 de [A-Za-z0-9_.-].
    bool IsValidExtensionName(const std::string& name);

    ManifestParseResult ParseExtensionManifest(const std::string& text);

    const char* ExtensionTargetName(ExtensionTarget target);

    // Devolve 'text' com a chave 'key' definida como 'value' ("key = value"): troca a linha existente
    // (a primeira, ignorando comentarios) ou acrescenta ao fim. Preserva o resto do arquivo, os
    // comentarios e o estilo de quebra de linha. Usado pelo editor para ligar/desligar 'enabled'.
    std::string SetManifestValue(const std::string& text, const std::string& key, const std::string& value);

    // Ordem de carga: dependencias antes de quem depende delas (estavel: empates seguem a ordem de
    // 'manifests'). Quem nao puder carregar (dependencia ausente, ciclica ou ela mesma com falha)
    // fica FORA do retorno e entra em 'failures' (nome -> motivo). 'alreadyLoaded' = extensoes que
    // ja estao carregadas e satisfazem dependencias. Nomes duplicados em 'manifests': chame com a
    // lista ja deduplicada.
    std::vector<std::string> ResolveLoadOrder(const std::vector<ExtensionManifest>& manifests,
                                              const std::set<std::string>& alreadyLoaded,
                                              std::map<std::string, std::string>& failures);

} // namespace Prism
