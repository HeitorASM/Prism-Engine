#pragma once

// ============================================================================
// SystemRegistry.h
// Registro dos IEngineSystem (ver EngineSystem.h). A Scene chama SceneStart /
// Update / FixedUpdate / SceneStop nos pontos certos do Play; quem usa a
// engine so registra:
//
//     SystemRegistry::Add<InventorySystem>("MinhaExtensao");
//
// Registre no inicio do programa / ao carregar uma extensao, ANTES de o Play
// comecar: um sistema registrado com o Play ja em andamento so recebe OnUpdate
// (nao ganha o OnSceneStart daquele Play).
//
// Single-thread (a engine inteira e). Registrar/remover DENTRO de um callback e
// seguro: a mudanca vale a partir da proxima chamada.
// ============================================================================

#include "EngineSystem.h"
#include "../Core/Base.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Prism {

    class SystemRegistry {
    public:
        // false (e erro no log) se o sistema e nulo, o nome e vazio ou ja existe outro com o mesmo nome.
        // 'source' = nome da extensao dona (para remover tudo dela de uma vez); "" = engine/jogo.
        static bool Register(Scope<IEngineSystem> system, const std::string& source = "");

        template<typename T, typename... Args>
        static bool Add(const std::string& source, Args&&... args) {
            return Register(CreateScope<T>(std::forward<Args>(args)...), source);
        }

        // Remove (chama OnShutdown). false se nao existe.
        static bool Unregister(const std::string& name);
        static void UnregisterSource(const std::string& source);

        // Remove todos (OnShutdown de cada um). Chamado ao encerrar a engine.
        static void Shutdown();

        // Nomes na ORDEM DE EXECUCAO.
        static std::vector<std::string> GetNames();
        static size_t Count();
        // O sistema lancou excecao e foi desligado?
        static bool IsFaulted(const std::string& name);

        // --- chamados pela Scene (nao use direto) ---------------------------------
        static void SceneStart(Scene& scene);
        static void SceneStop(Scene& scene);
        static void Update(Scene& scene, float deltaTime);
        static void FixedUpdate(Scene& scene, float fixedDeltaTime);
    };

}
