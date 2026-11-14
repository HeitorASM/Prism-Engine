#pragma once

// ============================================================================
// ParticleLinkSync.h
// Vinculo VIVO entre emissores de particulas e um recurso .prismres
// (ParticleEmitterComponent::LinkedAsset). Mesmo modelo do Material vinculado, mas sem
// depender de hooks nos comandos de Undo: a cada varredura COMPARA o estado de cada
// emissor vinculado com o ultimo estado sincronizado do arquivo e decide:
//
//   - 1a vez que um arquivo e visto nesta sessao:  o ARQUIVO manda -> todos os emissores
//                                                  vinculados a ele recebem os valores dele.
//   - arquivo mudou no disco (editor externo,
//     git, outra copia do editor):                 o ARQUIVO manda -> recarrega todos.
//   - um emissor mudou (painel, Undo/Redo, script
//     do editor...):                               grava o arquivo DEPOIS de um atraso
//                                                  (debounce, e nunca com o mouse apertado)
//                                                  e entao atualiza os OUTROS emissores
//                                                  vinculados ao mesmo arquivo.
//
// Detectar por comparacao (em vez de "MarkDirty" nos pontos de edicao) cobre de graca
// Undo/Redo, arrastar sliders, colar valores e qualquer edicao futura.
//
// Comparacoes usam a forma CANONICA (valores ja saneados): um recurso com numeros fora de
// faixa nao fica "diferente de si mesmo" para sempre nem regrava o arquivo em loop.
//
// Sem OpenGL/ImGui/Scene: recebe a lista de emissores, o resolvedor de caminho e o relogio.
// E testado isoladamente (tests/ParticleLinkSyncTests.cpp).
// ============================================================================

#include "ParticleResource.h"

#include <filesystem>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Prism {

    class ParticleLinkSync {
    public:
        static constexpr double kSaveDelay = 0.35;      // segundos sem mudar antes de gravar
        static constexpr double kScanInterval = 0.15;   // varredura no maximo a cada 150 ms

        // AssetID -> caminho absoluto no disco ("" se o asset nao existe mais).
        using PathResolver = std::function<std::filesystem::path(AssetID)>;

        struct Stats {
            int Saves = 0;          // arquivos gravados
            int FileReloads = 0;    // vezes em que o arquivo mandou nos emissores
        };

        // 'emitters': TODOS os emissores da cena (ou so os vinculados) - os sem vinculo sao ignorados.
        // 'now': segundos (relogio monotono). 'holdSave': true enquanto o usuario arrasta algo
        // (mouse apertado) - adia a gravacao. Devolve true se ALGUM emissor foi alterado por esta chamada.
        bool Update(const std::vector<ParticleEmitterComponent*>& emitters, const PathResolver& resolve, double now, bool holdSave);

        // Grava AGORA tudo que esta pendente (troca de cena, fechar o editor). Nao precisa dos emissores:
        // o estado pendente e guardado aqui.
        void FlushAll(const PathResolver& resolve);

        // Esquece tudo (cena nova/aberta): o proximo Update trata cada arquivo como "visto pela 1a vez".
        // Chame FlushAll antes se houver edicao pendente.
        void Reset();

        bool HasPending() const;
        const Stats& GetStats() const { return m_Stats; }
        // Ultimo erro de leitura/gravacao (vazio se nenhum); o editor o mostra no log.
        std::string TakeLastError() { std::string e = std::move(m_LastError); m_LastError.clear(); return e; }

        // Forma canonica (saneada) da definicao de um emissor - o que de fato vai para o arquivo.
        static ResourceDocument Canonical(const ParticleEmitterComponent& emitter);

    private:
        struct Entry {
            ResourceDocument Doc;                    // ultimo estado sincronizado (canonico)
            std::filesystem::file_time_type MTime{}; // mtime do arquivo nesse estado
            bool Synced = false;                     // ja lemos o arquivo com sucesso nesta sessao
            bool Dirty = false;                      // ha uma edicao esperando para ser gravada
            ResourceDocument Pending;                // o que sera gravado (canonico)
            double DirtySince = 0.0;                 // quando Pending mudou pela ultima vez
            double NextRetry = 0.0;                  // arquivo ilegivel: nao tenta de novo antes disto
        };

        bool Save(AssetID id, Entry& entry, const std::filesystem::path& path);

        std::unordered_map<AssetID, Entry> m_Entries;
        double m_LastScan = -1e30;
        Stats m_Stats;
        std::string m_LastError;
    };

}
