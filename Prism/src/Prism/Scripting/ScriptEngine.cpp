#include "ScriptEngine.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Core/Log.h"

#include <fstream>
#include <sstream>

namespace Prism {

    void ScriptEngine::Init() {
        // Uma unica VM Lua para o processo inteiro (nao uma por entidade/
        // script) - scripts diferentes ainda ficam isolados um do outro
        // porque cada um roda dentro do seu proprio sol::environment (ver
        // LoadScript abaixo), nao por terem VMs separadas. Isso e mais
        // barato (nao paga o custo de inicializar libs padrao do Lua N
        // vezes) e e o approach padrao usado por engines com scripting Lua
        // (ex: o mesmo modelo do LOVE2D/Defold).
        s_Lua = CreateScope<sol::state>();

        // Bibliotecas padrao do Lua abertas: base (print, pairs, etc),
        // math, string, table. NAO abrimos io/os/package/debug de
        // proposito - um script de gameplay nao deveria conseguir ler/
        // escrever arquivos do disco ou rodar comandos do SO so por
        // existir dentro de uma Scene; se isso vier a ser necessario no
        // futuro (ex: um script explicitamente marcado como "de sistema"),
        // e uma decisao para abrir essas libs caso a caso, nao por padrao.
        s_Lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

        RegisterAPI();

        PRISM_CORE_INFO("ScriptEngine: Lua inicializado (", LUA_RELEASE, ").");
    }

    void ScriptEngine::Shutdown() {
        UnloadAll();
        s_Lua.reset();
    }

    void ScriptEngine::RegisterAPI() {
        sol::state& lua = *s_Lua;

        // --- glm::vec3 -------------------------------------------------
        // Exposto como um usertype simples com x/y/z - scripts leem/
        // escrevem campo por campo (script.Translation.x = 5) em vez de
        // reatribuir o vec3 inteiro toda vez, o que bate com o jeito que
        // sol2 idiomaticamente expoe structs POD.
        lua.new_usertype<glm::vec3>("Vec3",
            sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
            "x", &glm::vec3::x,
            "y", &glm::vec3::y,
            "z", &glm::vec3::z
        );

        // --- TransformComponent -----------------------------------------
        // So os tres campos editaveis (Translation/Rotation/Scale) -
        // GetTransform() (a matriz 4x4 calculada) nao e exposta porque
        // scripts nao tem uso pratico para uma glm::mat4 crua ainda (sem
        // bindings de matriz, sem necessidade concreta ate fisica/
        // multiplas cameras via script existirem).
        lua.new_usertype<TransformComponent>("Transform",
            "Translation", &TransformComponent::Translation,
            "Rotation", &TransformComponent::Rotation, // graus, Euler XYZ - mesmo formato que a Properties panel usa
            "Scale", &TransformComponent::Scale
        );

        // --- Entity ------------------------------------------------------
        // API minima deliberada: so a propria Transform da entidade (ver
        // comentario de escopo no topo do .h). GetTransform()/SetPosition
        // sao a forma de um script se mover - "entity:GetTransform().
        // Translation.x = entity:GetTransform().Translation.x + 1" seria
        // verboso, entao SetPosition/Translate cobrem o caso comum
        // (mover por um delta, comum em scripts de movimento por frame)
        // sem obrigar o script a reconstruir um Vec3 inteiro para um
        // ajuste pequeno.
        lua.new_usertype<Entity>("Entity",
            "GetTransform", [](Entity& e) -> TransformComponent& { return e.GetComponent<TransformComponent>(); },
            "GetName", [](Entity& e) -> const std::string& { return e.GetComponent<TagComponent>().Tag; },
            "SetPosition", [](Entity& e, float x, float y, float z) {
                e.GetComponent<TransformComponent>().Translation = { x, y, z };
            },
            "Translate", [](Entity& e, float dx, float dy, float dz) {
                e.GetComponent<TransformComponent>().Translation += glm::vec3(dx, dy, dz);
            }
        );

        // --- log() ---------------------------------------------------------
        // Funcao global (nao Entity:Log(), de proposito - um script deve
        // poder logar algo sem precisar de "self" a mao) que escreve no
        // MESMO Console panel que o resto da engine usa (ver Log.h/
        // LogBuffer.h) - assim um erro de gameplay aparece lado a lado com
        // logs do editor/engine, um unico lugar para olhar.
        lua["log"] = [](const std::string& message) {
            Log::AppLog(LogLevel::Info, "[Lua] ", message);
        };
        lua["log_warn"] = [](const std::string& message) {
            Log::AppLog(LogLevel::Warn, "[Lua] ", message);
        };
        lua["log_error"] = [](const std::string& message) {
            Log::AppLog(LogLevel::Error, "[Lua] ", message);
        };

        // TODO(fisica): quando Box3D for integrado (proximo item do
        // roadmap - ver README), expor aqui algo como
        // entity:ApplyForce(x,y,z) / callbacks OnCollisionEnter, seguindo
        // o mesmo padrao acima.
        // TODO(input): expor uma tabela global `Input` (Input.IsKeyDown(...))
        // quando a engine tiver um sistema de Input por polling (ver nota
        // ja existente em EditorLayer::OnEvent sobre isso faltar).
    }

    bool ScriptEngine::LoadScript(Entity entity, const std::filesystem::path& scriptAbsolutePath) {
        if (!s_Lua) {
            PRISM_CORE_ERROR("ScriptEngine::LoadScript chamado antes de ScriptEngine::Init().");
            return false;
        }

        ScriptKey key{ entity.GetScene(), (uint32_t)entity.GetHandle() };

        // Recarregar: se ja havia um script carregado para esta entidade,
        // desliga ele primeiro (chama OnDestroy do script ANTIGO) antes de
        // montar a instancia nova - evita duas instancias do "mesmo"
        // script vivas ao mesmo tempo apos um reload.
        if (s_LoadedScripts.find(key) != s_LoadedScripts.end())
            UnloadScript(entity);

        std::ifstream file(scriptAbsolutePath);
        if (!file.is_open()) {
            PRISM_CORE_ERROR("ScriptEngine: arquivo de script nao encontrado: ", scriptAbsolutePath.string());
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();

        // Cada script roda dentro do seu proprio sol::environment (uma
        // tabela Lua separada usada como escopo global daquele chunk) -
        // isso e o que impede um script de sobrescrever variaveis globais
        // de outro sem querer (ex: dois scripts diferentes ambos usando
        // uma variavel chamada "speed" no top-level do arquivo). O
        // environment herda de _G (extend_type = true) entao ainda enxerga
        // Vec3/Entity/log/etc registrados em RegisterAPI().
        sol::environment env(*s_Lua, sol::create, s_Lua->globals());

        // Usamos load_buffer + set_environment + chamada explicita, em vez
        // de sol::state::safe_script(codigo, env, chunkname), de proposito:
        // aquela sobrecarga de safe_script/script tem um bug de resolucao
        // de overload conhecido no MSVC (SFINAE do sol2 escolhe a
        // sobrecarga errada quando os tipos dos argumentos nao batem
        // EXATAMENTE com o que o template espera - ver
        // github.com/ThePhD/sol2/issues/572), que se manifesta como
        // "C2064: term does not evaluate to a function taking 2 arguments"
        // em state_view.hpp - nao e um erro no NOSSO codigo, e uma
        // limitacao do sol2 nessa API especifica. load()+chamada manual e
        // uma API mais simples/direta do sol2 que nao passa por essa
        // sobrecarga problematica.
        sol::load_result loadResult = s_Lua->load(source, scriptAbsolutePath.string());
        if (!loadResult.valid()) {
            sol::error err = loadResult;
            PRISM_CORE_ERROR("ScriptEngine: erro ao carregar '", scriptAbsolutePath.string(), "': ", err.what());
            return false;
        }

        sol::protected_function scriptChunk = loadResult;
        // Liga o chunk carregado ao environment isolado deste script ANTES
        // de rodar - equivalente ao que safe_script(codigo, env, ...)
        // faria por dentro, so que passo a passo (ver comentario acima).
        sol::set_environment(env, scriptChunk);

        sol::protected_function_result result = scriptChunk();
        if (!result.valid()) {
            sol::error err = result;
            PRISM_CORE_ERROR("ScriptEngine: erro ao carregar '", scriptAbsolutePath.string(), "': ", err.what());
            return false;
        }

        // Convencao do script: o arquivo .lua e uma sequencia de
        // definicoes de funcao no escopo global DELE (OnCreate/OnUpdate/
        // OnDestroy) - nao uma table literal retornada. Pegamos essas tres
        // funcoes de dentro do environment depois do script rodar. Nenhuma
        // delas e obrigatoria (um script so com OnUpdate, por exemplo, e
        // valido).
        LoadedScript loaded;
        loaded.Instance = env;
        loaded.OnCreate = env["OnCreate"];
        loaded.OnUpdate = env["OnUpdate"];
        loaded.OnDestroy = env["OnDestroy"];

        s_LoadedScripts[key] = std::move(loaded);

        // "self" implicito: um script chama entity:GetTransform() etc
        // recebendo a PROPRIA entidade como variavel global `entity`
        // dentro do seu environment - mais simples para quem esta
        // comecando a escrever scripts do que exigir OnCreate(self).
        env["entity"] = entity;

        LoadedScript& script = s_LoadedScripts[key];
        if (script.OnCreate.valid()) {
            sol::protected_function_result createResult = script.OnCreate();
            if (!createResult.valid()) {
                sol::error err = createResult;
                PRISM_CORE_ERROR("ScriptEngine: erro em OnCreate() de '", scriptAbsolutePath.string(), "': ", err.what());
                script.HasRuntimeError = true;
            }
        }

        return true;
    }

    void ScriptEngine::UpdateScript(Entity entity, float deltaTime) {
        ScriptKey key{ entity.GetScene(), (uint32_t)entity.GetHandle() };
        auto it = s_LoadedScripts.find(key);
        if (it == s_LoadedScripts.end())
            return;

        LoadedScript& script = it->second;
        // Uma vez com erro, para de chamar OnUpdate todo frame - sem isso,
        // um script quebrado spammaria o Console com o MESMO erro a 60fps,
        // tornando o Console inutil ate o script ser corrigido e
        // recarregado (o que reseta HasRuntimeError, ver LoadScript acima).
        if (script.HasRuntimeError || !script.OnUpdate.valid())
            return;

        sol::protected_function_result result = script.OnUpdate(deltaTime);
        if (!result.valid()) {
            sol::error err = result;
            PRISM_CORE_ERROR("ScriptEngine: erro em OnUpdate(): ", err.what());
            script.HasRuntimeError = true;
        }
    }

    void ScriptEngine::UnloadScript(Entity entity) {
        ScriptKey key{ entity.GetScene(), (uint32_t)entity.GetHandle() };
        auto it = s_LoadedScripts.find(key);
        if (it == s_LoadedScripts.end())
            return;

        LoadedScript& script = it->second;
        if (script.OnDestroy.valid()) {
            sol::protected_function_result result = script.OnDestroy();
            if (!result.valid()) {
                sol::error err = result;
                PRISM_CORE_ERROR("ScriptEngine: erro em OnDestroy(): ", err.what());
            }
        }

        s_LoadedScripts.erase(it);
    }

    void ScriptEngine::UnloadAll() {
        // Copia as chaves antes de iterar - UnloadScript apaga do mapa
        // por dentro, entao iterar o mapa original enquanto ele muda seria
        // um iterator invalidado.
        std::vector<ScriptKey> keys;
        keys.reserve(s_LoadedScripts.size());
        for (auto& [key, script] : s_LoadedScripts)
            keys.push_back(key);

        for (auto& key : keys) {
            auto it = s_LoadedScripts.find(key);
            if (it == s_LoadedScripts.end())
                continue;
            LoadedScript& script = it->second;
            if (script.OnDestroy.valid()) {
                sol::protected_function_result result = script.OnDestroy();
                if (!result.valid()) {
                    sol::error err = result;
                    PRISM_CORE_ERROR("ScriptEngine: erro em OnDestroy() (UnloadAll): ", err.what());
                }
            }
        }

        s_LoadedScripts.clear();
    }

}
