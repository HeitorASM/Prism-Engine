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
#include <vector>
#include <chrono>
#include "../Scene/Components.h" // ScriptPropertyValue

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
        // Eventos fisicos (ver PhysicsEngine::Simulate): recebem a OUTRA
        // entidade. Collision* para colisores solidos, Trigger* quando um
        // dos corpos e um trigger (Collider::IsTrigger).
        sol::protected_function OnCollisionEnter;
        sol::protected_function OnCollisionExit;
        sol::protected_function OnTriggerEnter;
        sol::protected_function OnTriggerExit;
        sol::protected_function OnClick; // botao de UI desta entidade clicado
        sol::protected_function OnAnimationEvent; // evento de animacao (AnimatorComponent::Events): recebe o nome
        // Passo FIXO (1/60 s, o mesmo da fisica): chamado ZERO ou mais vezes por frame,
        // ANTES de cada passo da fisica - o lugar certo para ApplyForce/SetVelocity.
        sol::protected_function OnFixedUpdate;
        // Chamado so apos um hot reload (o OnCreate NAO roda de novo).
        sol::protected_function OnReload;

        // Hot reload: de onde veio o script e a data do arquivo na carga, e os valores
        // iniciais das variaveis globais (depois das Properties, antes do OnCreate) -
        // o que difere disso no reload e "estado de execucao" e e preservado.
        std::filesystem::path SourcePath;
        std::filesystem::file_time_type WriteTime{};
        struct InitialValue {
            sol::type Type = sol::type::none;
            double Number = 0.0;
            bool Bool = false;
            std::string String;
        };
        std::unordered_map<std::string, InitialValue> InitialValues;

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

        // Chama OnFixedUpdate(fixedDeltaTime) do script desta entidade (se houver).
        // Chamado por Scene antes de cada passo fixo da fisica.
        static void FixedUpdateScript(Entity entity, float fixedDeltaTime);

        // --- Hot reload (so Lua, durante o Play) ------------------------------
        // Quando o arquivo .lua de um script em execucao muda em disco (ex: salvo no
        // editor de scripts), ele e recarregado SEM parar o jogo: o estado de execucao
        // (variaveis alteradas pelo script, tabelas) e preservado, as variaveis nao
        // alteradas assumem o valor novo do arquivo, e OnReload() roda se existir.
        // Se o arquivo novo tem erro, o script antigo continua rodando (erro no Console).
        // Um script que estava com erro de execucao volta a rodar depois do reload.
        static void SetHotReloadEnabled(bool enabled) { s_HotReload = enabled; }
        static bool IsHotReloadEnabled() { return s_HotReload; }
        // Verifica (no maximo 2x por segundo) os scripts carregados da 'scene'.
        static void PollHotReload(Scene& scene);
        // Recarrega agora o script desta entidade, preservando o estado. false = falhou.
        static bool ReloadScript(Entity entity);

        // Chama OnDestroy() (se existir) e libera a instancia carregada
        // desta entidade. Chamado por Scene::OnScriptsStop() e por
        // Scene::DestroyEntity() quando a entidade tinha um script rodando.
        static void UnloadScript(Entity entity);

        // Descarrega TODOS os scripts carregados - usado ao trocar de cena
        // (LoadScene/NewMap), ja que os handles de entt::entity antigos
        // deixam de fazer sentido para uma Scene nova.
        static void UnloadAll();

        static sol::state& GetLuaState() { return *s_Lua; }

        // --- Tempo (tabela Lua `Time`) --------------------------------------
        // ResetTime() ao iniciar o Play; BeginFrame() uma vez por frame, antes
        // de qualquer script rodar (Scene::OnUpdate).
        static void ResetTime();
        static void BeginFrame(float deltaTime);
        static float GetDeltaTime() { return s_DeltaTime; }
        static uint64_t GetFrameCount() { return s_FrameCount; }
        static double GetElapsed() { return s_Elapsed; }

        // Chama OnCollisionEnter/Exit ou OnTriggerEnter/Exit do script de
        // 'self' (se existir), passando 'other'. Chamado por
        // PhysicsEngine::Simulate, na thread principal.
        static void DispatchContactEvent(Entity self, Entity other, bool enter, bool trigger);

        // Clique em botao de um UIDocumentComponent (.prismui): chama no script Lua e no C++
        // de 'self' a funcao 'handler' (atributo onClick do botao) com o id do botao; sem
        // 'handler', chama OnClick().
        static void DispatchUIDocumentClick(Entity self, const std::string& handler, const std::string& nodeId);

        // Evento de animacao: chama OnAnimationEvent(nome) do script Lua e do C++ de 'self'.
        static void DispatchAnimationEvent(Entity self, const std::string& name);

        // Destruicao ADIADA (entity:Destroy()/Scene.Destroy em Lua): destruir
        // uma entidade no meio de Scene::OnUpdate invalidaria a iteracao
        // sobre os scripts, entao os pedidos ficam numa fila executada ao
        // fim do frame por Scene::OnUpdate.
        static void QueueDestroy(Entity entity);

        // Instancia um prefab (caminho relativo a pasta do projeto) numa cena
        // em Play e ja o poe em jogo (fisica, audio, scripts Lua e nativos).
        // 'position' opcional. Entity invalida em caso de falha. Usado por
        // Scene.Instantiate (Lua) e pela API dos scripts nativos.
        static Entity InstantiateRuntime(Scene& scene, const std::string& relativePath, const glm::vec3* position);
        static void FlushDestroyQueue(Scene& scene);

        // Variaveis publicas: roda o arquivo num ambiente descartavel e devolve
        // os valores padrao da tabela global `Properties` (so numero/bool/
        // string), ordenados por nome. Vazio se o script nao declara
        // `Properties` ou nao carrega. Usada pelo painel Propriedades.
        static std::vector<ScriptPropertyValue> ReadScriptProperties(const std::filesystem::path& scriptAbsolutePath);

    private:
        static void RegisterAPI();

        // Le, executa e resolve os callbacks de um script (sem registrar nem chamar
        // OnCreate). Usado por LoadScript e ReloadScript.
        static bool BuildScript(Entity entity, const std::filesystem::path& scriptAbsolutePath, LoadedScript& out);

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

        inline static float s_DeltaTime = 0.0f;
        inline static double s_Elapsed = 0.0;
        inline static uint64_t s_FrameCount = 0;
        struct PendingDestroy {
            const void* SceneKey;
            entt::entity Handle;
        };
        inline static std::vector<PendingDestroy> s_PendingDestroy;

        inline static bool s_HotReload = true;
        inline static std::chrono::steady_clock::time_point s_LastHotReloadPoll{};

        inline static Scope<sol::state> s_Lua;
        inline static std::unordered_map<ScriptKey, LoadedScript, ScriptKeyHash> s_LoadedScripts;
    };

}
