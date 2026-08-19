#pragma once

// ============================================================================
// ScriptEngine.h
// Ponte entre a engine e Lua (via sol2 - ver vendor/CMakeLists.txt). Cuida
// de tres coisas:
//   1) o sol::state global (uma unica VM Lua para o processo inteiro do
//      editor - nao uma por entidade/script, ver comentario em Init())
//   2) a API que scripts Lua enxergam (usertypes de glm::vec3/Entity/
//      Transform + funcoes globais como log()) - ver RegisterAPI() no .cpp
//   3) o ciclo de vida de UM ScriptComponent: carregar o arquivo .lua,
//      chamar OnCreate/OnUpdate/OnDestroy quando existirem, e nunca deixar
//      um erro de script (sintaxe ou runtime) derrubar o editor inteiro -
//      erros viram PRISM_CORE_ERROR no Console, o script fica "quebrado"
//      ate ser corrigido e recarregado, so isso.
//
// Escopo da API Lua: Transform (posicao/rotacao/escala da PROPRIA
// entidade), fisica (ApplyForce/ApplyImpulse/GetVelocity/SetVelocity em
// Entity + Physics.Raycast global - ver PhysicsEngine::Raycast), Input
// (teclado/mouse/cursor - ver Core/Input.h) e log(). Ver
// example_player_input_raycast.lua (PrismEditor/assets/ScriptExamples)
// para um exemplo completo combinando os tres.
// ============================================================================

#include "../Core/Base.h"
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <filesystem>

namespace Prism {

    class Scene;
    class Entity;

    // Uma instancia de script CARREGADA e associada a UMA entidade -
    // guarda a sol::table de instancia (o "self" do script, criado a
    // partir do arquivo .lua) junto com os tres callbacks resolvidos uma
    // unica vez no load (evita re-resolver "self.OnUpdate" via string toda
    // vez que ele e chamado, o que seria uma busca em tabela hash por
    // frame, desnecessaria).
    struct LoadedScript {
        sol::table Instance;
        sol::protected_function OnCreate;
        sol::protected_function OnUpdate;
        sol::protected_function OnDestroy;
        bool HasRuntimeError = false; // true apos qualquer chamada falhar - impede spam do mesmo erro todo frame
    };

    class ScriptEngine {
    public:
        // Cria a VM Lua e registra a API da engine (usertypes + funcoes
        // globais - ver RegisterAPI() no .cpp). Chamado uma vez, no
        // OnAttach do EditorLayer (mesmo padrao de Renderer::Init()).
        static void Init();
        static void Shutdown();

        // Carrega (ou recarrega, se ja estava carregado) o script de UMA
        // entidade a partir de ScriptComponent::ScriptPath, e chama
        // OnCreate() uma vez se ele existir no arquivo. Chamado por
        // Scene::OnScriptsStart() para toda entidade com ScriptComponent
        // quando o modo Play comeca (ver Scene.h) - tambem pode ser
        // chamado isoladamente pelo editor para testar um script sem
        // precisar entrar em Play (ver botao "Recarregar" na Properties
        // panel). Retorna false se o arquivo nao existe ou tem erro de
        // sintaxe - o erro concreto vai para PRISM_CORE_ERROR/Console.
        static bool LoadScript(Entity entity, const std::filesystem::path& scriptAbsolutePath);

        // Chama OnUpdate(deltaTime) do script carregado desta entidade, se
        // houver um e se ele nao estiver marcado com erro de runtime
        // anterior (ver LoadedScript::HasRuntimeError). Chamado por
        // Scene::OnUpdate() para toda entidade com ScriptComponent.
        static void UpdateScript(Entity entity, float deltaTime);

        // Chama OnDestroy() (se existir) e libera a instancia carregada
        // desta entidade. Chamado por Scene::OnScriptsStop() e por
        // Scene::DestroyEntity() quando a entidade tinha um script rodando.
        static void UnloadScript(Entity entity);

        // Descarrega TODOS os scripts carregados - usado ao trocar de cena
        // (LoadScene/NewMap), ja que os handles de entt::entity antigos
        // deixam de fazer sentido para uma Scene nova.
        static void UnloadAll();

        static sol::state& GetLuaState() { return *s_Lua; }

    private:
        static void RegisterAPI();

        // Chave usada no mapa de scripts carregados: combina a Scene dona
        // (ponteiro cru, so para diferenciar - nunca desreferenciado aqui)
        // com o handle da entidade, ja que o mesmo entt::entity pode
        // existir em Scenes diferentes (ex: cena carregada de novo depois
        // de um Play) sem ser a "mesma" entidade logica.
        struct ScriptKey {
            const void* SceneKey;
            uint32_t EntityHandle;
            bool operator==(const ScriptKey& other) const {
                return SceneKey == other.SceneKey && EntityHandle == other.EntityHandle;
            }
        };
        struct ScriptKeyHash {
            size_t operator()(const ScriptKey& key) const {
                return std::hash<const void*>()(key.SceneKey) ^ (std::hash<uint32_t>()(key.EntityHandle) << 1);
            }
        };

        inline static Scope<sol::state> s_Lua;
        inline static std::unordered_map<ScriptKey, LoadedScript, ScriptKeyHash> s_LoadedScripts;
    };

}
