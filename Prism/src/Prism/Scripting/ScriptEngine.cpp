#include "ScriptEngine.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Physics/PhysicsEngine.h"
#include "../Core/Input.h"
#include "../Core/Log.h"

#include <fstream>
#include <sstream>
#include <vector>

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

        // --- RaycastHit ------------------------------------------------
        // Retorno de Physics.Raycast (ver abaixo) - exposto como usertype
        // simples (so leitura de campos, sem metodos) para o script poder
        // fazer "local hit = Physics.Raycast(...); if hit.Hit then ...".
        // Entity aqui e o entt::entity CRU (ver RaycastHit::Entity,
        // PhysicsEngine.h) - convertido para um Prism::Entity de verdade
        // (utilizavel com :GetTransform() etc) via RaycastHit:GetEntity(),
        // que precisa da Scene para reconstruir o wrapper (ver comentario
        // no lambda abaixo) - por isso nao e um campo direto, e um metodo.
        lua.new_usertype<RaycastHit>("RaycastHit",
            "Hit", &RaycastHit::Hit,
            "Point", &RaycastHit::Point,
            "Normal", &RaycastHit::Normal,
            "Distance", &RaycastHit::Distance
        );

        // --- Fisica (Jolt via PhysicsEngine) -----------------------------
        // Expostas como metodos de Entity, no mesmo padrao de Transform
        // acima - o script nunca ve um JPH::BodyID ou qualquer tipo do Jolt
        // diretamente, so o vocabulario da propria engine. Todas silenciosas
        // se a entidade nao tiver um corpo fisico ativo (fora do modo Play,
        // ou sem RigidBodyComponent+ColliderComponent - ver
        // PhysicsEngine::ApplyForce etc, que ja tratam isso).
        lua.new_usertype<Entity>("Entity",
            "GetTransform", [](Entity& e) -> TransformComponent& { return e.GetComponent<TransformComponent>(); },
            "GetName", [](Entity& e) -> const std::string& { return e.GetComponent<TagComponent>().Tag; },
            // --- Direcoes de mundo derivadas da rotacao ---------------------
            // Mesma tecnica ja usada pelo Renderer para a direcao de luzes
            // Spot/Directional (ver Renderer::CollectGPULights,
            // Renderer.cpp: "rotaciona o eixo -Z/+X local pela matriz de
            // rotacao") - rotaciona o eixo local convencionado pela matriz
            // 3x3 (so rotacao+escala, sem translacao) de
            // TransformComponent::GetTransform(). Existe para scripts NAO
            // precisarem reimplementar a trigonometria de yaw manualmente
            // (facil de errar o sinal) sempre que precisarem mover/mirar
            // "na direcao que a entidade esta olhando" - o uso mais comum
            // sendo um controller de camera/player que rotaciona o vetor
            // de movimento de WASD (espaco local) para espaco de mundo
            // antes de chamar SetVelocity (ver
            // example_player_input_raycast.lua). GetTransform() (local, SEM
            // hierarquia de pai) e suficiente aqui - mesma limitacao ja
            // documentada em PhysicsEngine::Simulate para parenting+fisica.
            "GetForward", [](Entity& e) -> glm::vec3 {
                glm::mat3 rotation = glm::mat3(e.GetComponent<TransformComponent>().GetTransform());
                return glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
            },
            "GetRight", [](Entity& e) -> glm::vec3 {
                glm::mat3 rotation = glm::mat3(e.GetComponent<TransformComponent>().GetTransform());
                return glm::normalize(rotation * glm::vec3(1.0f, 0.0f, 0.0f));
            },
            // --- Variantes de MUNDO (com hierarquia) ---------------------
            // GetForward/GetRight/GetTransform acima sao LOCAIS (ignoram
            // rotacao/posicao acumulada de qualquer pai - ver comentario
            // acima) - para uma entidade FILHA (ex: uma camera de player,
            // ver Entity:GetChild()), isso da a direcao/posicao ERRADA
            // sempre que o pai tiver rotacao/posicao propria (ex: o corpo
            // de um player girando em yaw - a camera filha, com so pitch
            // local, "perderia" o yaw do pai se usasse GetForward comum).
            // Estas 3 funcoes usam Scene::GetWorldTransform (mesma logica
            // que o Renderer usa para desenhar a entidade no lugar certo -
            // ver Renderer::DrawScene) para uma direcao/posicao que ja
            // inclui TODOS os ancestrais, correta em qualquer profundidade
            // de hierarquia.
            "GetWorldPosition", [](Entity& e) -> glm::vec3 {
                glm::mat4 world = e.GetScene()->GetWorldTransform(e);
                return glm::vec3(world[3]);
            },
            "GetWorldForward", [](Entity& e) -> glm::vec3 {
                glm::mat3 rotation = glm::mat3(e.GetScene()->GetWorldTransform(e));
                return glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
            },
            "GetWorldRight", [](Entity& e) -> glm::vec3 {
                glm::mat3 rotation = glm::mat3(e.GetScene()->GetWorldTransform(e));
                return glm::normalize(rotation * glm::vec3(1.0f, 0.0f, 0.0f));
            },
            "SetPosition", [](Entity& e, float x, float y, float z) {
                e.GetComponent<TransformComponent>().Translation = { x, y, z };
            },
            "Translate", [](Entity& e, float dx, float dy, float dz) {
                e.GetComponent<TransformComponent>().Translation += glm::vec3(dx, dy, dz);
            },
            "ApplyForce", [](Entity& e, float x, float y, float z) {
                PhysicsEngine::ApplyForce(*e.GetScene(), e, glm::vec3(x, y, z));
            },
            "ApplyImpulse", [](Entity& e, float x, float y, float z) {
                PhysicsEngine::ApplyLinearImpulse(*e.GetScene(), e, glm::vec3(x, y, z));
            },
            "GetVelocity", [](Entity& e) -> glm::vec3 {
                return PhysicsEngine::GetLinearVelocity(*e.GetScene(), e);
            },
            "SetVelocity", [](Entity& e, float x, float y, float z) {
                PhysicsEngine::SetLinearVelocity(*e.GetScene(), e, glm::vec3(x, y, z));
            },
            // --- Hierarquia (pai/filhos) -------------------------------
            // Ver comentario grande em Entity::GetParent/GetChild
            // (Entity.h) - motivado por example_player_input_raycast.lua
            // precisar de um "corpo" (gira so em Y/yaw) e uma "camera"
            // filha (gira so em X/pitch) para nao quebrar o movimento ao
            // olhar para cima/baixo (ver esse script para o uso completo).
            // ATENCAO no Lua: um Entity "nao encontrado" NAO vira nil (e
            // um usertype de verdade, sol2 nunca o transforma em nil so
            // por ser invalido) - "if child then" e SEMPRE true aqui,
            // mesmo para uma Entity invalida. Scripts devem checar
            // 'child:IsValid()' explicitamente antes de usar o resultado
            // de GetParent/GetChild/GetChildAt.
            "GetParent", [](Entity& e) -> Entity { return e.GetParent(); },
            "GetChildCount", [](Entity& e) -> size_t { return e.GetChildCount(); },
            "GetChildAt", [](Entity& e, size_t index) -> Entity { return e.GetChildAt(index); },
            "GetChild", [](Entity& e, const std::string& name) -> Entity { return e.GetChild(name); },
            "IsValid", [](Entity& e) -> bool { return e.IsValid(); },
            // Compara pelo handle+Scene (ver Entity::operator==,
            // Entity.h) - necessario para um script poder checar
            // "hit:GetEntity() == entity" (ver RaycastHit:GetEntity()
            // abaixo) sem precisar comparar campo a campo manualmente.
            sol::meta_function::equal_to, [](const Entity& a, const Entity& b) { return a == b; }
        );

        // GetEntity() de RaycastHit precisa da Scene "atual" para
        // reconstruir um Prism::Entity de verdade a partir do
        // entt::entity cru guardado em RaycastHit::Entity (ver
        // PhysicsEngine.h) - Entity nao existe sozinha, sempre precisa de
        // um Scene* dono (ver Entity.h). Como todo script ja tem a
        // variavel global `entity` (ver LoadScript - "self" implicito),
        // usamos GetScene() dela mesma: um raycast disparado de dentro de
        // um script sempre quer entidades da MESMA Scene que esta rodando,
        // nunca de outra. Adicionado como um metodo separado (nao um
        // campo) do proprio usertype RaycastHit, com a Scene amarrada por
        // closure no momento em que Physics.Raycast e chamado (ver
        // Physics.Raycast abaixo) - user nunca passa a Scene manualmente.
        // Implementado dentro de Physics.Raycast em vez de aqui.

        // --- Physics.Raycast --------------------------------------------
        // Tabela global `Physics` (nao Entity:Raycast(), de proposito - um
        // raycast nao pertence logicamente a uma entidade especifica, e
        // uma query livre no mundo - ver PhysicsEngine::Raycast).
        // Assinatura em Lua:
        //   local hit = Physics.Raycast(originVec3, directionVec3, maxDistance)
        //   if hit.Hit then log(hit:GetEntity():GetName()) end
        // maxDistance e opcional (default 1000, mesmo default de
        // PhysicsEngine::Raycast no C++) - sol2 resolve isso via
        // sol::optional aqui.
        sol::table physicsTable = lua.create_table();
        physicsTable["Raycast"] = [](sol::this_environment thisEnv, glm::vec3 origin, glm::vec3 direction, sol::optional<float> maxDistance, sol::optional<Entity> ignoreEntity) -> sol::table {
            // sol::this_environment injeta o sol::environment de QUEM
            // CHAMOU esta funcao (o environment isolado do script, ver
            // LoadScript - "entity" e uma variavel local a ELE, nunca uma
            // global de _G) - e o jeito certo de recuperar "a entidade
            // deste script" de dentro de uma funcao registrada
            // globalmente, sem precisar que o script passe `entity` como
            // parametro manualmente toda vez que chama Physics.Raycast.
            // ATENCAO: lua["entity"] (a globals table) NAO funcionaria
            // aqui - "entity" so existe dentro do environment do script
            // (ver env["entity"] em LoadScript), nao em _G.
            sol::environment& env = thisEnv;
            sol::state_view luaView(env.lua_state());
            sol::table result = luaView.create_table();

            Entity callerEntity = env["entity"];
            Scene* scene = callerEntity ? callerEntity.GetScene() : nullptr;
            if (!scene) {
                result["Hit"] = false;
                return result;
            }

            // 'ignoreEntity' (opcional, ver assinatura acima) - um unico
            // atalho comum para "nao acerte esta entidade especifica" (ex:
            // um script disparando um raio a partir de uma arma/mao que
            // nao deveria acertar a si mesma) - para excluir mais de uma
            // entidade de uma vez, ver RaycastComponent::IgnoreParentAndSiblings
            // (Components.h), que ja resolve o caso de hierarquia
            // pai/irmas automaticamente sem o script precisar montar essa
            // lista manualmente.
            std::vector<entt::entity> ignoreEntities;
            if (ignoreEntity.has_value() && ignoreEntity->IsValid())
                ignoreEntities.push_back(ignoreEntity->GetHandle());

            RaycastHit hit = PhysicsEngine::Raycast(*scene, origin, direction, maxDistance.value_or(1000.0f), ignoreEntities);
            result["Hit"] = hit.Hit;
            result["Point"] = hit.Point;
            result["Normal"] = hit.Normal;
            result["Distance"] = hit.Distance;
            // GetEntity() como funcao dentro da table de resultado (nao
            // um campo Entity direto) - permite retornar um
            // Prism::Entity valido (precisa de 'scene', capturado aqui
            // por closure) so quando/se o script realmente pedir por ele,
            // em vez de reconstruir sempre, mesmo quando Hit == false
            // (onde OtherEntity nao faz sentido nenhum).
            result["GetEntity"] = [scene, hit]() -> Entity {
                return hit.Hit ? Entity(hit.Entity, scene) : Entity();
            };
            return result;
        };
        lua["Physics"] = physicsTable;

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

        // --- Input -----------------------------------------------------
        // Tabela global `Input` (polling - ver Core/Input.h para o porque
        // deste modelo em vez de callbacks/eventos) + tabela `Key` com
        // nomes legiveis para os codigos de tecla mais comuns (WASD,
        // Espaco, setas, etc - ver Prism::Key). Um script tipico:
        //   if Input.IsKeyDown(Key.W) then entity:Translate(0, 0, -1 * dt) end
        //   if Input.IsKeyPressed(Key.Space) then entity:ApplyImpulse(0, 5, 0) end
        //   local dx = Input.GetMouseDeltaX() -- para camera FPS/TPS, ver CursorMode abaixo
        // So funciona (retorna sempre false/0) durante o modo Play, ja que
        // Input::SetContext so aponta para uma janela de verdade quando a
        // PlayWindow esta aberta (ver PlayWindow.cpp) - chamar isto fora
        // do Play (o que normalmente nao deveria acontecer, scripts so
        // rodam durante Play) e seguro, so nao retorna nada util.
        sol::table inputTable = lua.create_table();
        inputTable["IsKeyDown"] = [](int keyCode) { return Input::IsKeyDown(keyCode); };
        inputTable["IsKeyPressed"] = [](int keyCode) { return Input::IsKeyPressed(keyCode); };
        inputTable["IsMouseButtonDown"] = [](int buttonCode) { return Input::IsMouseButtonDown(buttonCode); };
        inputTable["GetMouseX"] = []() { return Input::GetMouseX(); };
        inputTable["GetMouseY"] = []() { return Input::GetMouseY(); };
        // Delta de mouse (ver Input::GetMouseDeltaX/Y, Core/Input.h) - o
        // que uma camera FPS/TPS de "olhar ao redor" realmente consome,
        // ao inves de GetMouseX/Y (posicao absoluta, que fica presa/
        // inutil quando o cursor esta travado - ver CursorMode.Locked
        // abaixo).
        inputTable["GetMouseDeltaX"] = []() { return Input::GetMouseDeltaX(); };
        inputTable["GetMouseDeltaY"] = []() { return Input::GetMouseDeltaY(); };
        // Captura/libera o cursor do SO (ver Input::SetCursorMode,
        // Core/Input.h) - uso tipico: Input.SetCursorMode(CursorMode.Locked)
        // uma vez em OnCreate() de um controller de camera FPS/TPS, e
        // Input.SetCursorMode(CursorMode.Normal) ao abrir um menu de
        // pausa (ou em OnDestroy()).
        inputTable["SetCursorMode"] = [](int mode) { Input::SetCursorMode((CursorMode)mode); };
        inputTable["GetCursorMode"] = []() { return (int)Input::GetCursorMode(); };
        lua["Input"] = inputTable;

        // Tabela CursorMode: os tres valores de Prism::CursorMode (ver
        // Core/Input.h) - script usa CursorMode.Locked em vez do inteiro
        // cru, mesmo espirito da tabela Key abaixo.
        sol::table cursorModeTable = lua.create_table();
        cursorModeTable["Normal"] = (int)CursorMode::Normal;
        cursorModeTable["Hidden"] = (int)CursorMode::Hidden;
        cursorModeTable["Locked"] = (int)CursorMode::Locked;
        lua["CursorMode"] = cursorModeTable;

        // Tabela Key: so os nomes mais usados em gameplay (ver enum Key,
        // Core/Input.h) - qualquer outro codigo GLFW_KEY_* nao listado
        // aqui ainda funciona passando o numero cru para Input.IsKeyDown
        // (ex: Input.IsKeyDown(65) equivale a Input.IsKeyDown(Key.A)) -
        // esta tabela e so conveniencia de leitura, nao uma whitelist.
        sol::table keyTable = lua.create_table();
        keyTable["Space"] = (int)Key::Space;
        keyTable["Enter"] = (int)Key::Enter;
        keyTable["Escape"] = (int)Key::Escape;
        keyTable["Tab"] = (int)Key::Tab;
        keyTable["LeftShift"] = (int)Key::LeftShift;
        keyTable["LeftControl"] = (int)Key::LeftControl;
        keyTable["Up"] = (int)Key::Up;
        keyTable["Down"] = (int)Key::Down;
        keyTable["Left"] = (int)Key::Left;
        keyTable["Right"] = (int)Key::Right;
        // A-Z geradas em loop (evita 26 linhas repetitivas) - Key::A ate
        // Key::Z sao contiguas no enum (ver Core/Input.h), mesma ordem do
        // alfabeto/ASCII.
        for (char c = 'A'; c <= 'Z'; c++) {
            std::string name(1, c);
            keyTable[name] = (int)Key::A + (c - 'A');
        }
        // 0-9 tambem contiguas (Key::D0 .. Key::D9).
        for (char c = '0'; c <= '9'; c++) {
            std::string name(1, c);
            keyTable[name] = (int)Key::D0 + (c - '0');
        }
        lua["Key"] = keyTable;

        // TODO: expor callbacks OnCollisionEnter/OnCollisionExit chamados
        // pelo PhysicsEngine::Simulate quando ocorrem eventos de contato
        // do Jolt envolvendo esta entidade (ver o TODO em
        // PhysicsEngine::Simulate, no bloco de eventos de colisao).
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
