#include "SystemRegistry.h"
#include "../Core/Log.h"

#include <algorithm>
#include <exception>
#include <functional>

namespace Prism {

    namespace {

        struct Entry {
            Scope<IEngineSystem> System;
            std::string Name;
            std::string Source;
            int Order = 0;
            uint64_t Sequence = 0;   // desempate: ordem de registro
            bool Faulted = false;
            bool Removed = false;    // removido durante um callback: sai quando o laco externo termina
        };

        std::vector<Scope<Entry>>& Entries() {
            static std::vector<Scope<Entry>> entries;
            return entries;
        }

        uint64_t s_NextSequence = 0;
        int s_Depth = 0; // > 0 enquanto um callback esta rodando (nao da para apagar entradas agora)

        void SortEntries() {
            auto& entries = Entries();
            std::stable_sort(entries.begin(), entries.end(), [](const Scope<Entry>& a, const Scope<Entry>& b) {
                if (a->Order != b->Order) return a->Order < b->Order;
                return a->Sequence < b->Sequence;
            });
        }

        void SweepRemoved() {
            if (s_Depth > 0)
                return;
            auto& entries = Entries();
            entries.erase(std::remove_if(entries.begin(), entries.end(), [](const Scope<Entry>& e) { return e->Removed; }), entries.end());
        }

        // Chama 'callback' em cada sistema ativo, em ordem. Excecao: desliga so aquele sistema.
        void ForEachActive(const char* callbackName, const std::function<void(IEngineSystem&)>& callback) {
            // Snapshot: um callback pode registrar/remover sistemas sem invalidar este laco.
            std::vector<Entry*> snapshot;
            snapshot.reserve(Entries().size());
            for (auto& entry : Entries())
                if (!entry->Removed && !entry->Faulted)
                    snapshot.push_back(entry.get());

            ++s_Depth;
            for (Entry* entry : snapshot) {
                if (entry->Removed || entry->Faulted)
                    continue;
                try {
                    callback(*entry->System);
                } catch (const std::exception& e) {
                    entry->Faulted = true;
                    PRISM_CORE_ERROR("Sistema '", entry->Name, "' lancou uma excecao em ", callbackName, ": ", e.what(), ". Ele foi desligado.");
                } catch (...) {
                    entry->Faulted = true;
                    PRISM_CORE_ERROR("Sistema '", entry->Name, "' lancou uma excecao desconhecida em ", callbackName, ". Ele foi desligado.");
                }
            }
            --s_Depth;
            SweepRemoved();
        }

        void ShutdownEntry(Entry& entry) {
            try {
                entry.System->OnShutdown();
            } catch (...) {
                PRISM_CORE_ERROR("Sistema '", entry.Name, "' lancou uma excecao em OnShutdown (ignorada).");
            }
        }

    } // namespace

    bool SystemRegistry::Register(Scope<IEngineSystem> system, const std::string& source) {
        if (!system) {
            PRISM_CORE_ERROR("SystemRegistry: sistema nulo recusado.");
            return false;
        }
        const char* rawName = system->GetName();
        const std::string name = rawName ? rawName : "";
        if (name.empty()) {
            PRISM_CORE_ERROR("SystemRegistry: sistema sem nome recusado (GetName() devolveu vazio).");
            return false;
        }
        for (auto& entry : Entries()) {
            if (!entry->Removed && entry->Name == name) {
                PRISM_CORE_ERROR("SystemRegistry: ja existe um sistema chamado '", name, "'; o novo foi recusado.");
                return false;
            }
        }

        auto entry = CreateScope<Entry>();
        entry->Name = name;
        entry->Source = source;
        entry->Order = system->GetOrder();
        entry->Sequence = s_NextSequence++;
        entry->System = std::move(system);
        Entries().push_back(std::move(entry));
        SortEntries();
        return true;
    }

    bool SystemRegistry::Unregister(const std::string& name) {
        for (auto& entry : Entries()) {
            if (entry->Removed || entry->Name != name)
                continue;
            entry->Removed = true;
            ShutdownEntry(*entry);
            SweepRemoved();
            return true;
        }
        return false;
    }

    void SystemRegistry::UnregisterSource(const std::string& source) {
        for (auto& entry : Entries()) {
            if (entry->Removed || entry->Source != source)
                continue;
            entry->Removed = true;
            ShutdownEntry(*entry);
        }
        SweepRemoved();
    }

    void SystemRegistry::Shutdown() {
        for (auto& entry : Entries()) {
            if (!entry->Removed) {
                entry->Removed = true;
                ShutdownEntry(*entry);
            }
        }
        SweepRemoved();
    }

    std::vector<std::string> SystemRegistry::GetNames() {
        std::vector<std::string> names;
        for (auto& entry : Entries())
            if (!entry->Removed)
                names.push_back(entry->Name);
        return names;
    }

    size_t SystemRegistry::Count() {
        size_t count = 0;
        for (auto& entry : Entries())
            if (!entry->Removed)
                count++;
        return count;
    }

    bool SystemRegistry::IsFaulted(const std::string& name) {
        for (auto& entry : Entries())
            if (!entry->Removed && entry->Name == name)
                return entry->Faulted;
        return false;
    }

    void SystemRegistry::SceneStart(Scene& scene) {
        ForEachActive("OnSceneStart", [&](IEngineSystem& system) { system.OnSceneStart(scene); });
    }
    void SystemRegistry::SceneStop(Scene& scene) {
        ForEachActive("OnSceneStop", [&](IEngineSystem& system) { system.OnSceneStop(scene); });
    }
    void SystemRegistry::Update(Scene& scene, float deltaTime) {
        ForEachActive("OnUpdate", [&](IEngineSystem& system) { system.OnUpdate(scene, deltaTime); });
    }
    void SystemRegistry::FixedUpdate(Scene& scene, float fixedDeltaTime) {
        ForEachActive("OnFixedUpdate", [&](IEngineSystem& system) { system.OnFixedUpdate(scene, fixedDeltaTime); });
    }

}
