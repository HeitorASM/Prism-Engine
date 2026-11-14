#include "ParticleLinkSync.h"

#include <algorithm>

namespace Prism {

    ResourceDocument ParticleLinkSync::Canonical(const ParticleEmitterComponent& emitter) {
        // Passa por FromResource para sanear (limites, NaN, enums) como se o valor tivesse vindo
        // de um arquivo: o resultado e estavel (idempotente) e e o que o arquivo guardara.
        ParticleEmitterComponent normalized;
        ParticleResource::FromResource(ParticleResource::ToResource(emitter), normalized);
        return ParticleResource::ToResource(normalized);
    }

    bool ParticleLinkSync::Save(AssetID id, Entry& entry, const std::filesystem::path& path) {
        std::string error;
        if (!ResourceIO::SaveByExtension(path, entry.Pending, &error)) {
            m_LastError = "nao foi possivel gravar '" + path.string() + "': " + error;
            return false;
        }
        (void)id;
        entry.Doc = entry.Pending;
        entry.Dirty = false;
        std::error_code ec;
        entry.MTime = std::filesystem::last_write_time(path, ec);
        m_Stats.Saves++;
        return true;
    }

    bool ParticleLinkSync::Update(const std::vector<ParticleEmitterComponent*>& emitters, const PathResolver& resolve, double now, bool holdSave) {
        if (now - m_LastScan < kScanInterval)
            return false;
        m_LastScan = now;

        // Agrupa os emissores vinculados por arquivo.
        std::unordered_map<AssetID, std::vector<ParticleEmitterComponent*>> groups;
        for (ParticleEmitterComponent* emitter : emitters)
            if (emitter && emitter->LinkedAsset.IsValid())
                groups[emitter->LinkedAsset].push_back(emitter);

        bool changedAny = false;
        auto applyToAll = [&](const std::vector<ParticleEmitterComponent*>& group, const ResourceDocument& doc, const ParticleEmitterComponent* except) {
            for (ParticleEmitterComponent* emitter : group) {
                if (emitter == except)
                    continue;
                if (Canonical(*emitter) == doc)
                    continue; // ja esta igual: nao toca (evita trabalho e mudancas fantasmas)
                ParticleResource::FromResource(doc, *emitter);
                changedAny = true;
            }
        };

        for (auto& [asset, group] : groups) {
            const std::filesystem::path path = resolve(asset);
            if (path.empty())
                continue; // asset sumiu: nada a ler nem gravar (o editor avisa que o vinculo esta quebrado)

            Entry& entry = m_Entries[asset];
            std::error_code ec;
            const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(path, ec);

            // 1) Primeira vez ou arquivo alterado por fora -> o ARQUIVO manda.
            const bool fileIsNewer = !ec && entry.Synced && mtime != entry.MTime;
            if ((!entry.Synced || fileIsNewer) && !ec && now >= entry.NextRetry) {
                // Se ha uma edicao nossa pendente E o arquivo mudou por fora, o arquivo ganha
                // (a pendencia e descartada): quem gravou por ultimo no disco vence.
                ParticleEmitterComponent loaded;
                std::string error;
                ResourceDocument doc;
                if (ResourceIO::Load(path, doc, &error) && ParticleResource::FromResource(doc, loaded, &error)) {
                    entry.Doc = ParticleResource::ToResource(loaded);
                    entry.MTime = mtime;
                    entry.Synced = true;
                    entry.Dirty = false;
                    m_Stats.FileReloads++;
                    applyToAll(group, entry.Doc, nullptr);
                }
                else {
                    m_LastError = "nao foi possivel ler '" + path.string() + "': " + error;
                    entry.NextRetry = now + 2.0; // nao repete o erro a cada varredura
                    if (!entry.Synced)
                        continue; // sem estado conhecido: nao toca nos emissores
                }
                continue;
            }
            if (!entry.Synced)
                continue; // arquivo ilegivel/inexistente ainda

            // 2) Algum emissor difere do estado sincronizado -> e uma EDICAO dele.
            ParticleEmitterComponent* source = nullptr;
            ResourceDocument sourceDoc;
            for (ParticleEmitterComponent* emitter : group) {
                ResourceDocument doc = Canonical(*emitter);
                if (doc != entry.Doc) {
                    source = emitter;
                    sourceDoc = std::move(doc);
                    break;
                }
            }
            if (!source) {
                entry.Dirty = false; // voltou ao estado do arquivo (ex.: Undo): nada a gravar
                continue;
            }
            if (!entry.Dirty || entry.Pending != sourceDoc) {
                entry.Dirty = true;
                entry.Pending = sourceDoc;
                entry.DirtySince = now;
            }
            if (holdSave || now - entry.DirtySince < kSaveDelay)
                continue;
            if (Save(asset, entry, path))
                applyToAll(group, entry.Doc, source); // os OUTROS emissores vinculados seguem o arquivo
        }

        // Entradas de arquivos que nenhum emissor usa mais: esquece (se houver edicao pendente, grava antes).
        for (auto it = m_Entries.begin(); it != m_Entries.end();) {
            if (groups.find(it->first) != groups.end()) {
                ++it;
                continue;
            }
            if (it->second.Dirty) {
                const std::filesystem::path path = resolve(it->first);
                if (!path.empty())
                    Save(it->first, it->second, path);
            }
            it = m_Entries.erase(it);
        }
        return changedAny;
    }

    void ParticleLinkSync::FlushAll(const PathResolver& resolve) {
        for (auto& [asset, entry] : m_Entries) {
            if (!entry.Dirty)
                continue;
            const std::filesystem::path path = resolve(asset);
            if (!path.empty())
                Save(asset, entry, path);
        }
    }

    void ParticleLinkSync::Reset() {
        m_Entries.clear();
        m_LastScan = -1e30;
    }

    bool ParticleLinkSync::HasPending() const {
        for (const auto& [asset, entry] : m_Entries)
            if (entry.Dirty)
                return true;
        return false;
    }

}
