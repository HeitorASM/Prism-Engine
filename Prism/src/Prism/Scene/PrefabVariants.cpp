#include "PrefabVariants.h"
#include "PrefabSerializer.h"
#include "PrefabSyncer.h"
#include "ComponentRegistry.h"
#include "Scene.h"
#include "../Core/Log.h"

#include <unordered_map>
#include <unordered_set>

namespace Prism {

    uint64_t PrefabVariants::Hash(const void* data, size_t size, uint64_t seed) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        uint64_t h = seed;
        for (size_t i = 0; i < size; i++) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }

    namespace {
        struct Loaded {
            Ref<Scene> Scn;
            std::vector<Entity> Entities;
            std::vector<uint64_t> Guids;
            PrefabVariantInfo Info;
            bool Ok = false;
        };

        Loaded Load(const std::filesystem::path& path) {
            Loaded l;
            l.Scn = Scene::Create("__prefab_variant_temp__");
            Entity root = PrefabSerializer::Instantiate(*l.Scn, path, {}, &l.Guids, &l.Info);
            if (!root) return l;
            PrefabSyncer::CollectSubtreePreOrder(root, l.Entities);
            l.Ok = l.Entities.size() == l.Guids.size();
            return l;
        }

        uint64_t NameHash(const std::string& n) { return PrefabVariants::Hash(n.data(), n.size()); }
        uint64_t DataHash(const std::vector<char>& b) { return PrefabVariants::Hash(b.data(), b.size()); }

        // Hashes de todos os components de todas as entidades (Transform so nos filhos).
        std::vector<PrefabVariantInfo::ComponentHash> HashesOf(const Loaded& l) {
            std::vector<PrefabVariantInfo::ComponentHash> out;
            for (size_t i = 0; i < l.Entities.size(); i++)
                for (auto& [name, bytes] : PrefabSyncer::SerializeAllComponents(l.Entities[i], i != 0))
                    out.push_back({ l.Guids[i], NameHash(name), DataHash(bytes) });
            return out;
        }

        void ApplyComponent(const std::string& name, Entity from, Entity to) {
            if (name == "Transform") {
                to.GetComponent<TransformComponent>() = from.GetComponent<TransformComponent>();
                return;
            }
            for (auto& info : ComponentRegistry::GetAll()) {
                if (info.DisplayName != name) continue;
                const bool fromHas = info.Has(from);
                if (info.Has(to)) info.Remove(to);
                if (fromHas) info.Copy(from, to);
                return;
            }
        }
    }

    bool PrefabVariants::Create(const std::filesystem::path& basePath, AssetID baseAsset, const std::filesystem::path& variantPath) {
        if (!baseAsset.IsValid()) return false;
        Loaded base = Load(basePath);
        if (!base.Ok) return false;
        PrefabVariantInfo info;
        info.Base = baseAsset;
        info.KnownBaseGuids = base.Guids;
        info.BaseHashes = HashesOf(base);
        return PrefabSerializer::Serialize(base.Entities[0], variantPath, &base.Guids, nullptr, &info);
    }

    PrefabVariantUpdateResult PrefabVariants::Update(const std::filesystem::path& variantPath, const std::filesystem::path& basePath) {
        PrefabVariantUpdateResult r;
        Loaded var = Load(variantPath);
        Loaded base = Load(basePath);
        if (!var.Ok || !base.Ok) { r.Error = "nao foi possivel ler a variante ou o base"; return r; }
        if (!var.Info.IsVariant()) { r.Error = "o arquivo nao e uma variante"; return r; }
        r.Ok = true;

        std::unordered_map<uint64_t, Entity> varByGuid, baseByGuid;
        for (size_t i = 0; i < var.Entities.size(); i++) varByGuid[var.Guids[i]] = var.Entities[i];
        for (size_t i = 0; i < base.Entities.size(); i++) baseByGuid[base.Guids[i]] = base.Entities[i];
        const std::unordered_set<uint64_t> known(var.Info.KnownBaseGuids.begin(), var.Info.KnownBaseGuids.end());

        std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t>> oldHash; // guid -> nameHash -> dataHash
        for (const auto& h : var.Info.BaseHashes) oldHash[h.Guid][h.NameHash] = h.DataHash;

        Scene* scn = var.Scn.get();
        std::unordered_map<entt::entity, uint64_t> guidOf; // para reconstruir o vetor de GUIDs apos mudar a arvore
        for (size_t i = 0; i < var.Entities.size(); i++) guidOf[var.Entities[i].GetHandle()] = var.Guids[i];

        // 1) Remocoes: estava no base, saiu do base (a raiz nunca sai).
        std::vector<Entity> removed;
        for (size_t i = 1; i < var.Entities.size(); i++) {
            const uint64_t g = var.Guids[i];
            if (known.count(g) && !baseByGuid.count(g)) removed.push_back(var.Entities[i]);
        }
        std::unordered_set<entt::entity> removedSet;
        for (Entity e : removed) removedSet.insert(e.GetHandle());
        for (size_t k = removed.size(); k-- > 0;) {
            Entity e = removed[k];
            Entity parent = e.GetParent();
            std::vector<Entity> kids;
            for (size_t i = 0; i < e.GetChildCount(); i++)
                if (Entity c = e.GetChildAt(i)) kids.push_back(c);
            for (Entity c : kids)
                if (!removedSet.count(c.GetHandle()) && parent) scn->SetParent(c, parent);
            varByGuid.erase(guidOf[e.GetHandle()]);
            guidOf.erase(e.GetHandle());
            scn->DestroyEntity(e);
            r.Removed++;
        }

        // 2) Adicoes: no base, desconhecida da variante e ausente nela.
        for (size_t i = 1; i < base.Entities.size(); i++) {
            const uint64_t g = base.Guids[i];
            if (known.count(g) || varByGuid.count(g)) continue;
            Entity src = base.Entities[i];
            Entity parent = var.Entities[0];
            if (Entity bp = src.GetParent()) {
                for (size_t j = 0; j < base.Entities.size(); j++)
                    if (base.Entities[j] == bp) {
                        auto it = varByGuid.find(base.Guids[j]);
                        if (it != varByGuid.end()) parent = it->second;
                        break;
                    }
            }
            Entity created = scn->CreateEntity(src.GetComponent<TagComponent>().Tag);
            created.GetComponent<TransformComponent>() = src.GetComponent<TransformComponent>();
            for (auto& info : ComponentRegistry::GetAll())
                if (info.Has(src) && !info.Has(created)) info.Copy(src, created);
            scn->SetParent(created, parent);
            varByGuid[g] = created;
            guidOf[created.GetHandle()] = g;
            r.Added++;
        }

        // 3) Components das entidades presentes nos dois lados.
        for (size_t i = 0; i < base.Entities.size(); i++) {
            const uint64_t g = base.Guids[i];
            auto vit = varByGuid.find(g);
            if (vit == varByGuid.end()) continue;
            if (!known.count(g) && i != 0) continue; // acabou de ser criada a partir do base: ja esta igual
            Entity ve = vit->second, be = base.Entities[i];
            const bool withTransform = i != 0;
            auto bBytes = PrefabSyncer::SerializeAllComponents(be, withTransform);
            auto vBytes = PrefabSyncer::SerializeAllComponents(ve, withTransform);
            static const std::unordered_map<uint64_t, uint64_t> kEmpty;
            auto ohIt = oldHash.find(g);
            const auto& oh = ohIt != oldHash.end() ? ohIt->second : kEmpty;

            for (auto& [name, bytes] : bBytes) {
                auto o = oh.find(NameHash(name));
                auto v = vBytes.find(name);
                if (o == oh.end()) {                 // component novo no base
                    if (v == vBytes.end()) { ApplyComponent(name, be, ve); r.ComponentsUpdated++; }
                    continue;                          // a variante ja tinha: e dela
                }
                if (o->second == DataHash(bytes)) continue; // base nao mudou
                if (v != vBytes.end() && DataHash(v->second) == o->second) { // variante nao sobrescreveu
                    ApplyComponent(name, be, ve);
                    r.ComponentsUpdated++;
                }
            }
            // component que existia no base e sumiu, sem override na variante
            for (auto& [name, bytes] : vBytes) {
                if (bBytes.count(name)) continue;
                auto o = oh.find(NameHash(name));
                if (o != oh.end() && o->second == DataHash(bytes)) {
                    ApplyComponent(name, be, ve); // 'be' nao tem: remove
                    r.ComponentsUpdated++;
                }
            }
        }

        // 4) Regrava com o novo estado conhecido do base.
        PrefabVariantInfo info;
        info.Base = var.Info.Base;
        info.KnownBaseGuids = base.Guids;
        info.BaseHashes = HashesOf(base);

        const bool same = r.Added == 0 && r.Removed == 0 && r.ComponentsUpdated == 0
            && info.KnownBaseGuids == var.Info.KnownBaseGuids && info.BaseHashes.size() == var.Info.BaseHashes.size();
        bool hashesSame = same;
        if (same) {
            std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t>> nh;
            for (const auto& h : info.BaseHashes) nh[h.Guid][h.NameHash] = h.DataHash;
            hashesSame = nh == oldHash;
        }
        if (hashesSame) return r;

        std::vector<Entity> order;
        PrefabSyncer::CollectSubtreePreOrder(var.Entities[0], order);
        std::vector<uint64_t> guids;
        for (Entity e : order) guids.push_back(guidOf[e.GetHandle()]);
        if (!PrefabSerializer::Serialize(var.Entities[0], variantPath, &guids, nullptr, &info)) {
            r.Ok = false; r.Error = "falha ao gravar a variante";
            return r;
        }
        r.Changed = true;
        return r;
    }

    std::vector<std::filesystem::path> PrefabVariants::FindVariantsOf(AssetID baseAsset, const std::filesystem::path& rootDir) {
        std::vector<std::filesystem::path> out;
        std::error_code ec;
        if (!baseAsset.IsValid() || !std::filesystem::exists(rootDir, ec)) return out;
        for (auto it = std::filesystem::recursive_directory_iterator(rootDir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".prismprefab") continue;
            PrefabVariantInfo info;
            if (PrefabSerializer::ReadVariantInfo(it->path(), info) && info.IsVariant() && info.Base == baseAsset)
                out.push_back(it->path());
        }
        return out;
    }

    std::vector<PrefabVariantChange> PrefabVariants::UpdateDependents(AssetID baseAsset, const std::filesystem::path& rootDir,
        const std::function<std::filesystem::path(AssetID)>& resolve, const std::function<AssetID(const std::filesystem::path&)>& idOf) {
        std::vector<PrefabVariantChange> changed;
        std::unordered_set<AssetID> visited;
        std::vector<AssetID> queue{ baseAsset };
        while (!queue.empty()) {
            AssetID cur = queue.back(); queue.pop_back();
            if (!visited.insert(cur).second) continue; // ciclo
            const std::filesystem::path basePath = resolve(cur);
            if (basePath.empty()) continue;
            for (const auto& v : FindVariantsOf(cur, rootDir)) {
                std::vector<uint64_t> before;
                PrefabSyncer::ReadFileGuids(v, before);
                auto res = Update(v, basePath);
                if (res.Ok && res.Changed) changed.push_back({ v, std::move(before) });
                if (AssetID id = idOf(v); id.IsValid()) queue.push_back(id);
            }
        }
        return changed;
    }
}
