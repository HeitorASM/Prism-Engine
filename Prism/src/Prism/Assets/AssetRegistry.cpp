#include "AssetRegistry.h"
#include "../Core/Log.h"

#include <algorithm>
#include <set>

namespace Prism {

    namespace fs = std::filesystem;

    void AssetRegistry::SetRoot(const fs::path& assetRoot) {
        m_Root = assetRoot;
        m_ByID.clear();
        m_ByPath.clear();
    }

    std::string AssetRegistry::ToRelative(const fs::path& absolutePath) const {
        std::error_code ec;
        fs::path rel = fs::relative(absolutePath, m_Root, ec);
        if (ec || rel.empty())
            return {};

        std::string s = rel.generic_string(); // generic_string sempre usa '/'
        // Fora da raiz: relative() devolve algo comecando com "..".
        if (s == ".." || s.rfind("../", 0) == 0)
            return {};
        return s;
    }

    AssetRefreshReport AssetRegistry::Refresh() {
        AssetRefreshReport report;

        // Guarda o indice ANTIGO (ID -> caminho) para detectar movimentos.
        std::unordered_map<AssetID, std::string> previousPathOfID;
        for (const auto& [id, rec] : m_ByID)
            previousPathOfID[id] = rec.Path;

        m_ByID.clear();
        m_ByPath.clear();

        std::error_code ec;
        if (m_Root.empty() || !fs::is_directory(m_Root, ec))
            return report;

        // 1) Coleta e ORDENA todos os arquivos. Ordenar torna o resultado
        //    deterministico: sem isso, em caso de IDs duplicados, "quem
        //    ganha o ID original" dependeria da ordem que o SO lista a
        //    pasta (varia entre Windows/Linux e entre execucoes).
        std::vector<fs::path> assetFiles;
        std::vector<fs::path> metaFiles;
        for (fs::recursive_directory_iterator it(m_Root, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;

            const fs::path& p = it->path();
            const std::string name = p.filename().string();

            if (AssetMetaFile::IsMetaFile(p)) { metaFiles.push_back(p); continue; }
            if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) continue; // sobra de escrita atomica interrompida
            if (!name.empty() && name[0] == '.') continue;                                   // .gitkeep, .DS_Store...
            assetFiles.push_back(p);
        }
        std::sort(assetFiles.begin(), assetFiles.end());
        std::sort(metaFiles.begin(), metaFiles.end());

        // 2) Para cada asset, garante um .meta com ID valido e UNICO.
        for (const fs::path& assetPath : assetFiles) {
            const std::string rel = ToRelative(assetPath);
            if (rel.empty()) continue;

            const fs::path metaPath = AssetMetaFile::MetaPathFor(assetPath);
            AssetMeta meta;
            bool hadMeta = fs::exists(metaPath, ec);
            bool readOk = hadMeta && AssetMetaFile::Read(metaPath, meta);

            bool needsWrite = false;
            bool repaired = false;

            if (!readOk) {
                if (hadMeta) {
                    PRISM_CORE_WARN("AssetRegistry: .meta invalido/corrompido, gerando ID novo: ", metaPath.string());
                    repaired = true;
                }
                meta = AssetMeta{};
                meta.ID = AssetID::Generate();
                needsWrite = true;
            }
            else if (m_ByID.count(meta.ID)) {
                // ID ja ocupado por outro asset (o que veio antes na ordem).
                // Tipico: o usuario copiou "Rock.png" + "Rock.png.meta".
                PRISM_CORE_WARN("AssetRegistry: ID duplicado (", meta.ID.ToString(), ") em '", rel,
                    "' - gerando ID novo (provavel copia de asset junto com o .meta).");
                meta.ID = AssetID::Generate();
                needsWrite = true;
                repaired = true;
            }

            // O tipo vem da extensao, e o .meta e atualizado se divergir
            // (ex: usuario trocou o conteudo por outro formato).
            AssetType typeByExt = AssetTypeFromExtension(assetPath);
            if (meta.Type != typeByExt) {
                meta.Type = typeByExt;
                needsWrite = true;
            }

            if (needsWrite) {
                // Garante um ID nunca visto neste indice (probabilidade de
                // colisao ~0, mas o custo do laco e zero).
                while (m_ByID.count(meta.ID))
                    meta.ID = AssetID::Generate();

                if (!AssetMetaFile::Write(metaPath, meta)) {
                    PRISM_CORE_ERROR("AssetRegistry: nao foi possivel gravar o .meta de '", rel, "' - asset ignorado.");
                    continue;
                }
                if (repaired) report.Repaired++;
                else if (!readOk && !hadMeta) report.NewlyCreated++;
            }

            AssetRecord rec;
            rec.ID = meta.ID;
            rec.Type = meta.Type;
            rec.Path = rel;
            m_ByID[rec.ID] = rec;
            m_ByPath[rec.Path] = rec.ID;

            auto prev = previousPathOfID.find(rec.ID);
            if (prev != previousPathOfID.end() && prev->second != rel)
                report.Moved++;
        }

        // 3) .meta orfaos: nenhum asset com o nome correspondente.
        for (const fs::path& metaPath : metaFiles) {
            fs::path assetPath = metaPath;
            assetPath.replace_extension(); // tira o ".meta" final
            if (!fs::exists(assetPath, ec)) {
                std::string relMeta = ToRelative(metaPath);
                if (!relMeta.empty()) report.OrphanMetas.push_back(relMeta);
            }
        }

        report.Registered = (int)m_ByID.size();
        return report;
    }

    const AssetRecord* AssetRegistry::Find(AssetID id) const {
        auto it = m_ByID.find(id);
        return it == m_ByID.end() ? nullptr : &it->second;
    }

    const AssetRecord* AssetRegistry::FindByPath(const std::string& relativePath) const {
        auto it = m_ByPath.find(relativePath);
        if (it == m_ByPath.end()) return nullptr;
        return Find(it->second);
    }

    AssetID AssetRegistry::IdForPath(const std::string& relativePath) const {
        auto it = m_ByPath.find(relativePath);
        return it == m_ByPath.end() ? AssetID{} : it->second;
    }

    fs::path AssetRegistry::AbsolutePath(AssetID id) const {
        const AssetRecord* rec = Find(id);
        return rec ? (m_Root / rec->Path) : fs::path{};
    }

    std::vector<const AssetRecord*> AssetRegistry::GetAll() const {
        std::vector<const AssetRecord*> out;
        out.reserve(m_ByID.size());
        for (const auto& [id, rec] : m_ByID)
            out.push_back(&rec);
        std::sort(out.begin(), out.end(),
            [](const AssetRecord* a, const AssetRecord* b) { return a->Path < b->Path; });
        return out;
    }

}
