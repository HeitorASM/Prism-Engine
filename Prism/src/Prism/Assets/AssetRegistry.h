#pragma once

// ============================================================================
// AssetRegistry.h
// Indice AssetID <-> caminho de todos os assets de um projeto. E ele que
// responde "onde esta o asset 8f3a...?" e "qual o ID deste arquivo?".
//
// COMO FUNCIONA: Refresh() varre a pasta de assets e RECONCILIA o que
// encontra com os arquivos .meta (ver AssetMeta.h):
//   - asset sem .meta ............ ganha um ID novo e um .meta
//   - asset com .meta valido ..... e registrado com o ID gravado
//   - asset movido COM seu .meta . mesmo ID, caminho novo (nao quebra
//                                  nenhuma referencia - o objetivo todo)
//   - .meta sem asset (orfao) .... reportado; NUNCA apagado sozinho
//   - .meta corrompido ........... avisado; o asset ganha ID novo
//   - dois .meta com o MESMO ID .. (tipico de copiar asset+.meta) o
//                                  segundo ganha um ID novo, senao dois
//                                  assets teriam a mesma identidade
//
// CAMINHOS sao guardados RELATIVOS a pasta de assets, com '/' como
// separador em qualquer plataforma ("Textures/Rock.png").
// ============================================================================

#include "AssetID.h"
#include "AssetMeta.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Prism {

    struct AssetRecord {
        AssetID   ID;
        AssetType Type = AssetType::Unknown;
        std::string Path; // relativo a raiz de assets, com '/' - ver comentario acima
    };

    // O que um Refresh() encontrou/fez - devolvido para o editor poder
    // mostrar um resumo (e para os testes verificarem cada caso).
    struct AssetRefreshReport {
        int Registered = 0;                    // total de assets no indice apos o Refresh
        int NewlyCreated = 0;                  // ganharam .meta agora (eram novos)
        int Moved = 0;                         // mesmo ID de antes, mas em outro caminho
        int Repaired = 0;                      // .meta corrompido/duplicado -> ID novo
        std::vector<std::string> OrphanMetas;  // .meta cujo asset nao existe (caminhos dos .meta)
    };

    class AssetRegistry {
    public:
        // Define a pasta raiz dos assets (ex: <Projeto>/Assets) e limpa o
        // indice. Nao varre - chame Refresh() em seguida.
        void SetRoot(const std::filesystem::path& assetRoot);
        const std::filesystem::path& GetRoot() const { return m_Root; }

        // Varre a pasta e reconcilia com os .meta (ver comentario no topo).
        // Pode ser chamado quantas vezes quiser (ex: ao focar o editor).
        AssetRefreshReport Refresh();

        // --- consultas ---
        const AssetRecord* Find(AssetID id) const;                       // nullptr se nao existe
        const AssetRecord* FindByPath(const std::string& relativePath) const;
        AssetID IdForPath(const std::string& relativePath) const;         // AssetID{} se nao existe
        std::filesystem::path AbsolutePath(AssetID id) const;             // vazio se nao existe
        size_t Count() const { return m_ByID.size(); }
        std::vector<const AssetRecord*> GetAll() const;

        // Normaliza um caminho absoluto DENTRO da raiz para a forma relativa
        // com '/'. Devolve "" se o caminho esta FORA da raiz.
        std::string ToRelative(const std::filesystem::path& absolutePath) const;

    private:
        std::filesystem::path m_Root;
        std::unordered_map<AssetID, AssetRecord> m_ByID;
        std::unordered_map<std::string, AssetID> m_ByPath;
    };

}
