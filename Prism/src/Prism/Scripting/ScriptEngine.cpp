#include "ScriptEngine.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Physics/PhysicsEngine.h"
#include "../Core/Input.h"
#include "../Audio/AudioEngine.h"
#include "../Animation/AnimationSystem.h"
#include "../Project/Project.h"
#include "../Scene/PrefabSerializer.h"
#include "NativeScripting.h"
#include "../Particles/ParticleSystem.h"
#include "../UI/UISystem.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cstdio>
#include <system_error>
#include <fstream>
#include <sstream>
#include <vector>

namespace Prism {

    namespace {
        // Poe "em jogo" uma entidade criada durante o Play (Scene.Instantiate):
        // o que Scene::OnScriptsStart faz para as entidades originais -
        // corpo fisico, audio PlayOnStart e script (OnCreate) - para ela e
        // toda a sua subarvore.
        void ActivateRuntimeEntity(Scene& scene, Entity entity) {
            PhysicsEngine::CreateBodyForEntity(scene, entity);
            if (entity.HasComponent<AudioSourceComponent>() && entity.GetComponent<AudioSourceComponent>().PlayOnStart)
                AudioEngine::PlayEntity(entity);
            if (entity.HasComponent<NativeScriptComponent>())
                NativeScripting::LoadScript(entity);
            if (entity.HasComponent<ScriptComponent>()) {
                const std::string& scriptPath = entity.GetComponent<ScriptComponent>().ScriptPath;
                auto project = Project::GetActive();
                if (!scriptPath.empty() && project)
                    ScriptEngine::LoadScript(entity, project->GetScriptDirectory() / scriptPath);
            }
            // Copia dos filhos antes de recursar: o OnCreate de um script pode mexer na hierarquia.
            std::vector<Entity> children;
            for (size_t i = 0; i < entity.GetChildCount(); i++)
                children.push_back(entity.GetChildAt(i));
            for (Entity& child : children)
                if (child.IsValid())
                    ActivateRuntimeEntity(scene, child);
        }
    }

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
        // Operadores (+, -, *, /, unario -, ==), tostring e metodos
        // (Length, Normalized, Dot, Cross, Distance): permitem escrever
        //   local dir = (alvo:GetWorldPosition() - entity:GetWorldPosition()):Normalized()
        //   entity:SetVelocity(dir.x * 5, 0, dir.z * 5)
        lua.new_usertype<glm::vec3>("Vec3",
            sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
            "x", &glm::vec3::x,
            "y", &glm::vec3::y,
            "z", &glm::vec3::z,
            "Length", [](const glm::vec3& v) { return glm::length(v); },
            "Normalized", [](const glm::vec3& v) {
                float length = glm::length(v);
                return length > 1e-8f ? v / length : glm::vec3(0.0f); // vetor nulo nao vira NaN
            },
            "Dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
            "Cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); },
            "Distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); },
            sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
            sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
            sol::meta_function::multiplication, sol::overload(
                [](const glm::vec3& v, float s) { return v * s; },
                [](float s, const glm::vec3& v) { return s * v; }),
            sol::meta_function::division, [](const glm::vec3& v, float s) { return s != 0.0f ? v / s : glm::vec3(0.0f); },
            sol::meta_function::unary_minus, [](const glm::vec3& v) { return -v; },
            sol::meta_function::equal_to, [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
            sol::meta_function::to_string, [](const glm::vec3& v) {
                char buffer[96];
                std::snprintf(buffer, sizeof(buffer), "Vec3(%.3f, %.3f, %.3f)", v.x, v.y, v.z);
                return std::string(buffer);
            }
        );

        // --- Components acessiveis por script ---------------------------------
        // Obtidos por entity:GetMeshRenderer()/GetMaterial()/GetLight()/
        // GetCamera()/GetAudioSource() (nil se a entidade nao tiver o
        // component). Editam o component direto - valem no proximo frame.
        // (RigidBody de proposito fora: o corpo Jolt so le esses campos ao
        // ser criado, mudar em runtime nao faria nada.)
        lua.new_usertype<MeshRendererComponent>("MeshRenderer",
            "Color", &MeshRendererComponent::Color
        );
        lua.new_usertype<MaterialComponent>("Material",
            "AlbedoTint", &MaterialComponent::AlbedoTint,
            "RoughnessFactor", &MaterialComponent::RoughnessFactor,
            "MetallicFactor", &MaterialComponent::MetallicFactor,
            "AlbedoPath", &MaterialComponent::AlbedoPath,
            "NormalPath", &MaterialComponent::NormalPath,
            "RoughnessMetallicPath", &MaterialComponent::RoughnessMetallicPath
        );
        lua.new_usertype<LightComponent>("Light",
            "Color", &LightComponent::Color,
            "Intensity", &LightComponent::Intensity,
            "Range", &LightComponent::Range,
            "SpotAngle", &LightComponent::SpotAngle,
            "InnerSpotAngle", &LightComponent::InnerSpotAngle
        );
        lua.new_usertype<CameraComponent>("Camera",
            "FOV", &CameraComponent::FOV,
            "OrthoSize", &CameraComponent::OrthoSize,
            "NearClip", &CameraComponent::NearClip,
            "FarClip", &CameraComponent::FarClip,
            "Primary", &CameraComponent::Primary,
            // Opcoes de render da camera (v20). ClearMode: 0 = skybox, 1 = cor chapada.
            "ClearMode", sol::property(
                [](CameraComponent& c) { return (int)c.ClearMode; },
                [](CameraComponent& c, int v) { c.ClearMode = (v == 1) ? CameraClearMode::SolidColor : CameraClearMode::Skybox; }),
            "ClearColor", &CameraComponent::ClearColor,
            "Exposure", &CameraComponent::Exposure,
            "SSAO", &CameraComponent::SSAO,
            // Pos-processamento (o Renderer limita os valores).
            "Bloom", &CameraComponent::Bloom,
            "BloomThreshold", &CameraComponent::BloomThreshold,
            "BloomIntensity", &CameraComponent::BloomIntensity,
            "FXAA", &CameraComponent::FXAA,
            "Saturation", &CameraComponent::Saturation,
            "Contrast", &CameraComponent::Contrast
        );
        // Ambiente da cena (ciclo dia/noite etc). Sky: 0 = cor, 1 = gradiente, 2 = panorama.
        // O Renderer limita os valores, entao um script nao deixa a cena preta/NaN.
        lua.new_usertype<EnvironmentComponent>("Environment",
            "Ambient", &EnvironmentComponent::Ambient,
            "Zenith", &EnvironmentComponent::Zenith,
            "Horizon", &EnvironmentComponent::Horizon,
            "Ground", &EnvironmentComponent::Ground,
            "Sky", sol::property(
                [](EnvironmentComponent& c) { return (int)c.Sky; },
                [](EnvironmentComponent& c, int v) { c.Sky = (SkyMode)std::min(std::max(v, 0), 2); }),
            "SkyColor", &EnvironmentComponent::SkyColor,
            "SkyRotation", &EnvironmentComponent::SkyRotation,
            "SkyIntensity", &EnvironmentComponent::SkyIntensity
        );
        lua.new_usertype<ParticleEmitterComponent>("ParticleEmitter",
            "Enabled", &ParticleEmitterComponent::Enabled,
            "Loop", &ParticleEmitterComponent::Loop,
            "Additive", &ParticleEmitterComponent::Additive,
            "Duration", &ParticleEmitterComponent::Duration,
            "EmissionRate", &ParticleEmitterComponent::EmissionRate,
            "BurstCount", &ParticleEmitterComponent::BurstCount,
            "LifetimeMin", &ParticleEmitterComponent::LifetimeMin,
            "LifetimeMax", &ParticleEmitterComponent::LifetimeMax,
            "SpeedMin", &ParticleEmitterComponent::SpeedMin,
            "SpeedMax", &ParticleEmitterComponent::SpeedMax,
            "SpreadAngle", &ParticleEmitterComponent::SpreadAngle,
            "GravityScale", &ParticleEmitterComponent::GravityScale,
            "SizeStart", &ParticleEmitterComponent::SizeStart,
            "SizeEnd", &ParticleEmitterComponent::SizeEnd,
            // vec4 nao e um usertype: cores por funcao (r, g, b, a de 0 a 1).
            "SetColorStart", [](ParticleEmitterComponent& c, float r, float g, float b, float a) { c.ColorStart = { r, g, b, a }; },
            "SetColorEnd", [](ParticleEmitterComponent& c, float r, float g, float b, float a) { c.ColorEnd = { r, g, b, a }; }
        );
        // Elemento de UI (entity:GetUI()). Cores por funcao (r, g, b, a de 0 a 1).
        lua.new_usertype<UIElementComponent>("UIElement",
            "Visible", &UIElementComponent::Visible,
            "Text", &UIElementComponent::Text,
            "FontSize", &UIElementComponent::FontSize,
            "Interactable", &UIElementComponent::Interactable,
            "Layer", &UIElementComponent::Layer,
            "TexturePath", &UIElementComponent::TexturePath,
            "SetOffset", [](UIElementComponent& c, float x, float y) { c.Offset = { x, y }; },
            "SetSize", [](UIElementComponent& c, float w, float h) { c.Size = { w, h }; },
            "SetColor", [](UIElementComponent& c, float r, float g, float b, float a) { c.Color = { r, g, b, a }; },
            "SetTextColor", [](UIElementComponent& c, float r, float g, float b, float a) { c.TextColor = { r, g, b, a }; }
        );
        lua.new_usertype<AudioSourceComponent>("AudioSource",
            "Volume", &AudioSourceComponent::Volume,
            "Pitch", &AudioSourceComponent::Pitch,
            "Loop", &AudioSourceComponent::Loop,
            "Spatial", &AudioSourceComponent::Spatial,
            "MinDistance", &AudioSourceComponent::MinDistance,
            "MaxDistance", &AudioSourceComponent::MaxDistance,
            // Canal de volume como texto: "sfx", "music", "ui" ou "ambient".
            "Bus", sol::property(
                [](const AudioSourceComponent& c) -> std::string { return AudioBusName(c.Bus); },
                [](AudioSourceComponent& c, const std::string& name) {
                    AudioBus bus;
                    if (AudioBusFromName(name, bus))
                        c.Bus = bus;
                    else
                        Log::AppLog(LogLevel::Warn, "[Lua] Bus de audio desconhecido: '", name, "' (use sfx, music, ui ou ambient).");
                })
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
            // --- Componentes (nil se a entidade nao tiver) -------------------
            "GetMeshRenderer", [](Entity& e) -> MeshRendererComponent* { return e.HasComponent<MeshRendererComponent>() ? &e.GetComponent<MeshRendererComponent>() : nullptr; },
            "GetMaterial", [](Entity& e) -> MaterialComponent* { return e.HasComponent<MaterialComponent>() ? &e.GetComponent<MaterialComponent>() : nullptr; },
            "GetLight", [](Entity& e) -> LightComponent* { return e.HasComponent<LightComponent>() ? &e.GetComponent<LightComponent>() : nullptr; },
            "GetCamera", [](Entity& e) -> CameraComponent* { return e.HasComponent<CameraComponent>() ? &e.GetComponent<CameraComponent>() : nullptr; },
            "GetEnvironment", [](Entity& e) -> EnvironmentComponent* { return e.HasComponent<EnvironmentComponent>() ? &e.GetComponent<EnvironmentComponent>() : nullptr; },
            "GetAudioSource", [](Entity& e) -> AudioSourceComponent* { return e.HasComponent<AudioSourceComponent>() ? &e.GetComponent<AudioSourceComponent>() : nullptr; },
            "GetUI", [](Entity& e) -> UIElementComponent* { return e.HasComponent<UIElementComponent>() ? &e.GetComponent<UIElementComponent>() : nullptr; },
            "GetParticleEmitter", [](Entity& e) -> ParticleEmitterComponent* { return e.HasComponent<ParticleEmitterComponent>() ? &e.GetComponent<ParticleEmitterComponent>() : nullptr; },
            // Particulas: PlayParticles reinicia (e solta o burst); StopParticles
            // para de emitir (as vivas terminam); ClearParticles remove tudo.
            "PlayParticles", [](Entity& e) { ParticleSystem::Play(e); },
            "StopParticles", [](Entity& e) { ParticleSystem::Stop(e); },
            "ClearParticles", [](Entity& e) { ParticleSystem::Clear(e); },
            "IsParticlesPlaying", [](Entity& e) { return ParticleSystem::IsPlaying(e); },
            "GetParticleCount", [](Entity& e) { return ParticleSystem::GetAliveCount(e); },
            // --- Edicao basica -----------------------------------------------
            "SetName", [](Entity& e, const std::string& name) { e.GetComponent<TagComponent>().Tag = name; },
            "SetRotation", [](Entity& e, float x, float y, float z) { e.GetComponent<TransformComponent>().Rotation = { x, y, z }; }, // graus
            "SetScale", [](Entity& e, float x, float y, float z) { e.GetComponent<TransformComponent>().Scale = { x, y, z }; },
            // Pai invalido (ex: o resultado de Scene.Find que nao achou) vira raiz.
            "SetParent", [](Entity& e, Entity parent) { e.GetScene()->SetParent(e, parent); },
            // Destruicao adiada para o fim do frame (ver ScriptEngine::QueueDestroy).
            "Destroy", [](Entity& e) { QueueDestroy(e); },
            // O "ambiente" do script desta entidade (suas funcoes/variaveis
            // globais), para scripts conversarem: Scene.Find("Inimigo"):GetScript().TomarDano(10).
            // nil se a entidade nao tem script rodando.
            "GetScript", [](Entity& e) -> sol::object {
                auto found = s_LoadedScripts.find(ScriptKey{ e.GetScene(), (uint32_t)e.GetHandle() });
                if (found == s_LoadedScripts.end())
                    return sol::make_object(*s_Lua, sol::lua_nil);
                return sol::make_object(*s_Lua, found->second.Instance);
            },
            "IsValid", [](Entity& e) -> bool { return e.IsValid(); },
            // --- Audio (AudioSourceComponent) ---------------------------------
            // So funcionam em entidades com Audio Source; sem ele, silenciosas.
            // Volume/pitch escrevem no component e valem no mesmo frame
            // (AudioEngine::OnSceneUpdate relê todo frame).
            "PlaySound", [](Entity& e) { AudioEngine::PlayEntity(e); },
            "StopSound", [](Entity& e) { AudioEngine::StopEntity(e); },
            "IsSoundPlaying", [](Entity& e) -> bool { return AudioEngine::IsEntityPlaying(e); },
            // --- Animacao (AnimatorComponent) ---------------------------------
            // entity:PlayAnimation("Walk")            - troca de clipe (nao reinicia se ja toca)
            // entity:PlayAnimation("Walk", 0.2)       - com crossfade de 0.2 s
            // entity:PlayAnimation("Attack", 0.1, false) - 3o arg: loop (omitido = o do Animator)
            // Devolve false se a entidade nao tem Animator ou o clipe nao existe.
            "PlayAnimation", [](Entity& e, std::string clip, sol::optional<float> fade, sol::optional<bool> loop) -> bool {
                return AnimationSystem::Play(e, clip, fade.value_or(0.0f), loop ? (*loop ? 1 : 0) : -1);
            },
            "StopAnimation", [](Entity& e) { AnimationSystem::Stop(e); },
            "PauseAnimation", [](Entity& e, sol::optional<bool> paused) { AnimationSystem::SetPaused(e, paused.value_or(true)); },
            "SetAnimationSpeed", [](Entity& e, float speed) { AnimationSystem::SetSpeed(e, speed); },
            "IsAnimationPlaying", [](Entity& e) -> bool { return AnimationSystem::IsPlaying(e); },
            "GetAnimation", [](Entity& e) -> std::string { return AnimationSystem::GetCurrentClip(e); },
            "GetAnimationTime", [](Entity& e) -> float { return AnimationSystem::GetTime(e); },
            "GetAnimationDuration", [](Entity& e) -> float { return AnimationSystem::GetDuration(e); },
            "SetSoundVolume", [](Entity& e, float volume) {
                if (e.HasComponent<AudioSourceComponent>())
                    e.GetComponent<AudioSourceComponent>().Volume = volume;
            },
            "SetSoundPitch", [](Entity& e, float pitch) {
                if (e.HasComponent<AudioSourceComponent>())
                    e.GetComponent<AudioSourceComponent>().Pitch = pitch;
            },
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

        // --- Audio -----------------------------------------------------
        // Tabela global `Audio`: sons soltos (efeitos) e volume global.
        // 'path' e RELATIVO a pasta do projeto, como os caminhos de textura
        // (ex: "Assets/Sounds/tiro.wav"). Formatos: .wav, .mp3, .ogg, .flac.
        //   Audio.Play("Assets/Sounds/tiro.wav")                  -- 2D, volume 1
        //   Audio.Play("Assets/Sounds/tiro.wav", 0.5, 1.2)        -- volume, pitch
        //   Audio.Play3D("Assets/Sounds/boom.wav", x, y, z, 1.0)  -- na posicao do mundo
        //   Audio.Play("Assets/Music/tema.ogg", 0.8, 1.0, "music") -- no canal "music"
        //   Audio.SetBusVolume("music", 0.5)   Audio.SetBusMuted("sfx", true)
        //   Audio.SetMasterVolume(0.8)
        sol::table audioTable = lua.create_table();
        auto resolveProjectPath = [](const std::string& relative) -> std::filesystem::path {
            auto project = Project::GetActive();
            if (!project)
                return {};
            return project->GetProjectDirectory() / relative;
        };
        // Bus opcional (ultimo argumento): "sfx" (padrao), "music", "ui", "ambient".
        auto parseBus = [](const sol::optional<std::string>& name) -> AudioBus {
            AudioBus bus = AudioBus::SFX;
            if (name && !AudioBusFromName(*name, bus))
                Log::AppLog(LogLevel::Warn, "[Lua] Bus de audio desconhecido: '", *name, "' (use sfx, music, ui ou ambient).");
            return bus;
        };
        audioTable["Play"] = [resolveProjectPath, parseBus](const std::string& path, sol::optional<float> volume, sol::optional<float> pitch, sol::optional<std::string> bus) {
            AudioPlayOptions options;
            options.Volume = volume.value_or(1.0f);
            options.Pitch = pitch.value_or(1.0f);
            options.Bus = parseBus(bus);
            AudioEngine::PlayOneShot(resolveProjectPath(path), options);
        };
        audioTable["Play3D"] = [resolveProjectPath, parseBus](const std::string& path, float x, float y, float z, sol::optional<float> volume, sol::optional<float> pitch, sol::optional<std::string> bus) {
            AudioPlayOptions options;
            options.Volume = volume.value_or(1.0f);
            options.Pitch = pitch.value_or(1.0f);
            options.Spatial = true;
            options.Position = { x, y, z };
            options.Bus = parseBus(bus);
            AudioEngine::PlayOneShot(resolveProjectPath(path), options);
        };
        // Carrega antes um efeito curto (sem engasgo no primeiro disparo).
        audioTable["Preload"] = [resolveProjectPath](const std::string& path) { AudioEngine::Preload(resolveProjectPath(path)); };
        audioTable["SetBusVolume"] = [parseBus](const std::string& bus, float volume) { AudioEngine::SetBusVolume(parseBus(bus), volume); };
        audioTable["GetBusVolume"] = [parseBus](const std::string& bus) { return AudioEngine::GetBusVolume(parseBus(bus)); };
        audioTable["SetBusMuted"] = [parseBus](const std::string& bus, bool muted) { AudioEngine::SetBusMuted(parseBus(bus), muted); };
        audioTable["IsBusMuted"] = [parseBus](const std::string& bus) { return AudioEngine::IsBusMuted(parseBus(bus)); };
        audioTable["SetMasterVolume"] = [](float volume) { AudioEngine::SetMasterVolume(volume); };
        audioTable["GetMasterVolume"] = []() { return AudioEngine::GetMasterVolume(); };
        lua["Audio"] = audioTable;

        // --- print -----------------------------------------------------
        // print() do Lua escreve na saida padrao do processo (invisivel no
        // editor) - redireciona para o Console, como log().
        lua["print"] = [](sol::this_state state, sol::variadic_args args) {
            sol::state_view view(state);
            sol::function toString = view["tostring"];
            std::string message;
            for (auto argument : args) {
                if (!message.empty())
                    message += "\t";
                message += toString(argument).get<std::string>();
            }
            Log::AppLog(LogLevel::Info, "[Lua] ", message);
        };

        // --- Time ------------------------------------------------------
        //   Time.DeltaTime()   segundos do ultimo frame (o mesmo de OnUpdate(dt))
        //   Time.Elapsed()     segundos desde o inicio do Play
        //   Time.FrameCount()  frames desde o inicio do Play
        sol::table timeTable = lua.create_table();
        timeTable["DeltaTime"] = []() { return s_DeltaTime; };
        timeTable["Elapsed"] = []() { return s_Elapsed; };
        timeTable["FrameCount"] = []() { return s_FrameCount; };
        timeTable["FixedDeltaTime"] = []() { return PhysicsEngine::GetFixedTimeStep(); };
        lua["Time"] = timeTable;

        // --- Mathf -----------------------------------------------------
        sol::table mathfTable = lua.create_table();
        mathfTable["Clamp"] = [](float value, float low, float high) { return std::clamp(value, low, high); };
        mathfTable["Lerp"] = [](float a, float b, float t) { return a + (b - a) * t; };
        mathfTable["Sign"] = [](float value) { return value < 0.0f ? -1.0f : 1.0f; };
        lua["Mathf"] = mathfTable;

        // --- Scene -----------------------------------------------------
        // Consultas e criacao na cena do script que chamou (nunca de outra).
        //   Scene.Find("Jogador")                      -> Entity (checar :IsValid())
        //   Scene.FindAll("Moeda")                     -> tabela de Entity
        //   Scene.Create("Vazio")                      -> Entity so com Transform
        //   Scene.CreateMesh("Caixa", "Cube", x, y, z) -> Entity visivel (Cube/Sphere/Capsule/Cylinder/Plane)
        //   Scene.Instantiate("Assets/Prefabs/Bala.prismprefab", Vec3.new(0, 1, 0))
        //   Scene.Destroy(entidade)                    -> destruida no fim do frame
        auto callerScene = [](sol::this_environment thisEnv) -> Scene* {
            if (!thisEnv.env)
                return nullptr;
            sol::environment& env = *thisEnv.env;
            Entity caller = env["entity"];
            return caller ? caller.GetScene() : nullptr;
        };
        sol::table sceneTable = lua.create_table();
        sceneTable["Find"] = [callerScene](sol::this_environment thisEnv, const std::string& name) -> Entity {
            Scene* scene = callerScene(thisEnv);
            if (!scene)
                return Entity();
            auto view = scene->GetRegistry().view<TagComponent>();
            for (auto handle : view) {
                if (view.get<TagComponent>(handle).Tag == name)
                    return Entity(handle, scene);
            }
            return Entity();
        };
        sceneTable["FindAll"] = [callerScene](sol::this_environment thisEnv, sol::this_state state, const std::string& name) -> sol::table {
            sol::state_view view(state);
            sol::table result = view.create_table();
            Scene* scene = callerScene(thisEnv);
            if (!scene)
                return result;
            int index = 1;
            auto tags = scene->GetRegistry().view<TagComponent>();
            for (auto handle : tags) {
                if (tags.get<TagComponent>(handle).Tag == name)
                    result[index++] = Entity(handle, scene);
            }
            return result;
        };
        sceneTable["Create"] = [callerScene](sol::this_environment thisEnv, const std::string& name) -> Entity {
            Scene* scene = callerScene(thisEnv);
            return scene ? scene->CreateEntity(name) : Entity();
        };
        sceneTable["CreateMesh"] = [callerScene](sol::this_environment thisEnv, const std::string& name, const std::string& primitive,
                                                  sol::optional<float> x, sol::optional<float> y, sol::optional<float> z) -> Entity {
            Scene* scene = callerScene(thisEnv);
            if (!scene)
                return Entity();
            Entity created = scene->CreateEntity(name);
            auto& meshRenderer = created.AddComponent<MeshRendererComponent>();
            if (primitive == "Sphere") meshRenderer.Mesh = PrimitiveMesh::Sphere;
            else if (primitive == "Capsule") meshRenderer.Mesh = PrimitiveMesh::Capsule;
            else if (primitive == "Cylinder") meshRenderer.Mesh = PrimitiveMesh::Cylinder;
            else if (primitive == "Plane") meshRenderer.Mesh = PrimitiveMesh::Plane;
            else meshRenderer.Mesh = PrimitiveMesh::Cube;
            created.GetComponent<TransformComponent>().Translation = { x.value_or(0.0f), y.value_or(0.0f), z.value_or(0.0f) };
            return created;
        };
        sceneTable["Instantiate"] = [callerScene](sol::this_environment thisEnv, const std::string& relativePath, sol::optional<glm::vec3> position) -> Entity {
            Scene* scene = callerScene(thisEnv);
            if (!scene)
                return Entity();
            return InstantiateRuntime(*scene, relativePath, position.has_value() ? &*position : nullptr);
        };
        // Scene.Load("Fase2")   -> troca para o mapa (relativo a pasta de mapas) DEPOIS do frame atual.
        // Scene.Reload()        -> recomeca a cena atual do zero.
        // Todos os scripts e entidades da cena atual sao destruidos; nada persiste entre cenas.
        sceneTable["Load"] = [callerScene](sol::this_environment thisEnv, const std::string& mapPath) {
            if (Scene* scene = callerScene(thisEnv))
                scene->RequestLoadScene(mapPath);
        };
        sceneTable["Reload"] = [callerScene](sol::this_environment thisEnv) {
            if (Scene* scene = callerScene(thisEnv))
                scene->RequestReloadScene();
        };
        sceneTable["Destroy"] = [](Entity entity) { QueueDestroy(entity); };
        lua["Scene"] = sceneTable;

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
        // Teclas de funcao (GLFW_KEY_F1 = 290 ... F12 = 301) e extras do enum.
        for (int i = 1; i <= 12; i++)
            keyTable["F" + std::to_string(i)] = 289 + i;
        keyTable["Backspace"] = (int)Key::Backspace;
        keyTable["LeftAlt"] = (int)Key::LeftAlt;
        keyTable["RightShift"] = (int)Key::RightShift;
        keyTable["RightControl"] = (int)Key::RightControl;
        keyTable["RightAlt"] = (int)Key::RightAlt;
        lua["Key"] = keyTable;

        // Botoes do mouse para Input.IsMouseButtonDown(Mouse.Left).
        sol::table mouseTable = lua.create_table();
        mouseTable["Left"] = (int)MouseButton::Left;
        mouseTable["Right"] = (int)MouseButton::Right;
        mouseTable["Middle"] = (int)MouseButton::Middle;
        lua["Mouse"] = mouseTable;

        // UI.IsPointerOverUI(): o mouse esta sobre um elemento de UI que barra o
        // ponteiro? Use para nao disparar/mirar no jogo ao clicar em um botao.
        sol::table uiTable = lua.create_table();
        uiTable["IsPointerOverUI"] = []() { return UISystem::IsPointerOverUI(); };
        lua["UI"] = uiTable;
    }

    // Le, executa e resolve os callbacks de um script - sem registrar no mapa nem
    // chamar OnCreate (ver LoadScript / ReloadScript).
    bool ScriptEngine::BuildScript(Entity entity, const std::filesystem::path& scriptAbsolutePath, LoadedScript& out) {
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
        // OnDestroy...) - nao uma table literal retornada. Pegamos essas
        // funcoes de dentro do environment depois do script rodar. Nenhuma
        // delas e obrigatoria (um script so com OnUpdate, por exemplo, e
        // valido).
        out = LoadedScript{};
        out.Instance = env;
        out.OnCreate = env["OnCreate"];
        out.OnUpdate = env["OnUpdate"];
        out.OnFixedUpdate = env["OnFixedUpdate"];
        out.OnDestroy = env["OnDestroy"];
        out.OnReload = env["OnReload"];
        out.OnCollisionEnter = env["OnCollisionEnter"];
        out.OnCollisionExit = env["OnCollisionExit"];
        out.OnTriggerEnter = env["OnTriggerEnter"];
        out.OnTriggerExit = env["OnTriggerExit"];
        out.OnClick = env["OnClick"];
        out.SourcePath = scriptAbsolutePath;
        std::error_code timeError;
        out.WriteTime = std::filesystem::last_write_time(scriptAbsolutePath, timeError);

        // "self" implicito: um script chama entity:GetTransform() etc
        // recebendo a PROPRIA entidade como variavel global `entity`
        // dentro do seu environment - mais simples para quem esta
        // comecando a escrever scripts do que exigir OnCreate(self).
        env["entity"] = entity;

        // Variaveis publicas: aplica os valores editados no painel sobre a
        // tabela `Properties` do script (so chaves que o script declara, com
        // o mesmo tipo - o resto e ignorado), ANTES do OnCreate.
        if (entity.HasComponent<ScriptComponent>()) {
            const auto& overrides = entity.GetComponent<ScriptComponent>().Properties;
            sol::object propertiesObject = env["Properties"];
            if (!overrides.empty() && propertiesObject.is<sol::table>()) {
                sol::table properties = propertiesObject;
                for (const ScriptPropertyValue& value : overrides) {
                    sol::type existing = properties[value.Name].get_type();
                    if (value.Type == ScriptPropertyType::Number && existing == sol::type::number)
                        properties[value.Name] = value.Number;
                    else if (value.Type == ScriptPropertyType::Bool && existing == sol::type::boolean)
                        properties[value.Name] = value.Bool;
                    else if (value.Type == ScriptPropertyType::String && existing == sol::type::string)
                        properties[value.Name] = value.String;
                }
            }
        }

        // Valores iniciais das globais simples (para o hot reload distinguir
        // "constante do arquivo" de "estado alterado em execucao").
        sol::table envTable = env;
        for (auto& [keyObject, valueObject] : envTable) {
            if (keyObject.get_type() != sol::type::string)
                continue;
            LoadedScript::InitialValue initial;
            initial.Type = valueObject.get_type();
            if (initial.Type == sol::type::number) initial.Number = valueObject.as<double>();
            else if (initial.Type == sol::type::boolean) initial.Bool = valueObject.as<bool>();
            else if (initial.Type == sol::type::string) initial.String = valueObject.as<std::string>();
            else continue;
            out.InitialValues[keyObject.as<std::string>()] = std::move(initial);
        }
        return true;
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

        LoadedScript loaded;
        if (!BuildScript(entity, scriptAbsolutePath, loaded))
            return false;
        s_LoadedScripts[key] = std::move(loaded);

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

    bool ScriptEngine::ReloadScript(Entity entity) {
        if (!s_Lua || !entity.IsValid())
            return false;
        ScriptKey key{ entity.GetScene(), (uint32_t)entity.GetHandle() };
        auto found = s_LoadedScripts.find(key);
        if (found == s_LoadedScripts.end())
            return false;

        const std::filesystem::path path = found->second.SourcePath;
        LoadedScript fresh;
        if (!BuildScript(entity, path, fresh)) {
            // Erro no arquivo novo: o script antigo segue rodando. Marca a data como
            // vista para o erro aparecer UMA vez por salvamento (nao a cada poll).
            auto stillThere = s_LoadedScripts.find(key);
            if (stillThere != s_LoadedScripts.end()) {
                std::error_code timeError;
                stillThere->second.WriteTime = std::filesystem::last_write_time(path, timeError);
            }
            PRISM_CORE_WARN("ScriptEngine: hot reload de '", path.filename().string(), "' falhou; o script anterior continua rodando.");
            return false;
        }

        found = s_LoadedScripts.find(key);
        if (found == s_LoadedScripts.end())
            return false; // a entidade foi descarregada durante a compilacao

        // Estado de execucao: copia do ambiente antigo para o novo tudo que o script
        // alterou/criou desde a carga (numero/bool/string diferente do valor inicial,
        // variaveis novas, tabelas e objetos). Constantes nao alteradas assumem o
        // valor novo do arquivo. 'Properties' e 'entity' sao refeitas pela carga.
        {
            LoadedScript& old = found->second;
            sol::table oldTable = old.Instance;
            sol::table newTable = fresh.Instance;
            for (auto& [keyObject, valueObject] : oldTable) {
                if (keyObject.get_type() != sol::type::string)
                    continue;
                const std::string name = keyObject.as<std::string>();
                if (name == "entity" || name == "Properties")
                    continue;
                const sol::type type = valueObject.get_type();
                if (type == sol::type::function)
                    continue; // funcoes vem do arquivo novo
                if (type == sol::type::number || type == sol::type::boolean || type == sol::type::string) {
                    auto initial = old.InitialValues.find(name);
                    if (initial != old.InitialValues.end() && initial->second.Type == type) {
                        bool unchanged = (type == sol::type::number && initial->second.Number == valueObject.as<double>())
                                      || (type == sol::type::boolean && initial->second.Bool == valueObject.as<bool>())
                                      || (type == sol::type::string && initial->second.String == valueObject.as<std::string>());
                        if (unchanged)
                            continue;
                    }
                }
                newTable[name] = valueObject;
            }
        }

        found->second = std::move(fresh);
        PRISM_CORE_INFO("ScriptEngine: script recarregado: ", path.filename().string());

        LoadedScript& script = found->second;
        if (script.OnReload.valid()) {
            sol::protected_function_result reloadResult = script.OnReload();
            if (!reloadResult.valid()) {
                sol::error err = reloadResult;
                PRISM_CORE_ERROR("ScriptEngine: erro em OnReload(): ", err.what());
                script.HasRuntimeError = true;
            }
        }
        return true;
    }

    void ScriptEngine::PollHotReload(Scene& scene) {
        if (!s_HotReload || !s_Lua || s_LoadedScripts.empty())
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now - s_LastHotReloadPoll < std::chrono::milliseconds(500))
            return;
        s_LastHotReloadPoll = now;

        std::vector<uint32_t> changed;
        for (auto& [key, script] : s_LoadedScripts) {
            if (key.SceneKey != &scene)
                continue;
            std::error_code timeError;
            auto writeTime = std::filesystem::last_write_time(script.SourcePath, timeError);
            if (!timeError && writeTime != script.WriteTime)
                changed.push_back(key.EntityHandle);
        }
        for (uint32_t handle : changed) {
            entt::entity entityHandle = (entt::entity)handle;
            if (scene.GetRegistry().valid(entityHandle))
                ReloadScript(Entity(entityHandle, &scene));
        }
    }

    void ScriptEngine::FixedUpdateScript(Entity entity, float fixedDeltaTime) {
        ScriptKey key{ entity.GetScene(), (uint32_t)entity.GetHandle() };
        auto it = s_LoadedScripts.find(key);
        if (it == s_LoadedScripts.end())
            return;

        LoadedScript& script = it->second;
        if (script.HasRuntimeError || !script.OnFixedUpdate.valid())
            return;

        sol::protected_function_result result = script.OnFixedUpdate(fixedDeltaTime);
        if (!result.valid()) {
            sol::error err = result;
            PRISM_CORE_ERROR("ScriptEngine: erro em OnFixedUpdate(): ", err.what());
            script.HasRuntimeError = true;
        }
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
        NativeScripting::UnloadAll(); // mesma regra para os scripts em C++
    }

    Entity ScriptEngine::InstantiateRuntime(Scene& scene, const std::string& relativePath, const glm::vec3* position) {
        auto project = Project::GetActive();
        if (!project)
            return Entity();
        std::filesystem::path absolutePath = project->GetProjectDirectory() / relativePath;
        Entity root = PrefabSerializer::Instantiate(scene, absolutePath);
        if (!root.IsValid()) {
            Log::AppLog(LogLevel::Error, "Instantiate: nao foi possivel instanciar '", relativePath, "'.");
            return Entity();
        }
        if (position)
            root.GetComponent<TransformComponent>().Translation = *position;
        ActivateRuntimeEntity(scene, root);
        return root;
    }

    void ScriptEngine::ResetTime() {
        s_DeltaTime = 0.0f;
        s_Elapsed = 0.0;
        s_FrameCount = 0;
        s_PendingDestroy.clear();
    }

    void ScriptEngine::BeginFrame(float deltaTime) {
        s_DeltaTime = deltaTime;
        s_Elapsed += deltaTime;
        s_FrameCount++;
    }

    void ScriptEngine::DispatchContactEvent(Entity self, Entity other, bool enter, bool trigger) {
        NativeScripting::DispatchContactEvent(self, other, enter, trigger);

        auto it = s_LoadedScripts.find(ScriptKey{ self.GetScene(), (uint32_t)self.GetHandle() });
        if (it == s_LoadedScripts.end())
            return;

        LoadedScript& script = it->second;
        if (script.HasRuntimeError)
            return;

        sol::protected_function& callback = trigger
            ? (enter ? script.OnTriggerEnter : script.OnTriggerExit)
            : (enter ? script.OnCollisionEnter : script.OnCollisionExit);
        if (!callback.valid())
            return;

        sol::protected_function_result result = callback(other);
        if (!result.valid()) {
            sol::error err = result;
            PRISM_CORE_ERROR("ScriptEngine: erro em callback de colisao/trigger: ", err.what());
            script.HasRuntimeError = true;
        }
    }

    void ScriptEngine::DispatchUIClick(Entity self) {
        NativeScripting::DispatchUIClick(self);

        auto it = s_LoadedScripts.find(ScriptKey{ self.GetScene(), (uint32_t)self.GetHandle() });
        if (it == s_LoadedScripts.end())
            return;
        LoadedScript& script = it->second;
        if (script.HasRuntimeError || !script.OnClick.valid())
            return;
        sol::protected_function_result result = script.OnClick();
        if (!result.valid()) {
            sol::error err = result;
            PRISM_CORE_ERROR("ScriptEngine: erro em OnClick: ", err.what());
            script.HasRuntimeError = true;
        }
    }

    void ScriptEngine::QueueDestroy(Entity entity) {
        if (!entity.IsValid())
            return;
        for (const PendingDestroy& pending : s_PendingDestroy)
            if (pending.SceneKey == entity.GetScene() && pending.Handle == entity.GetHandle())
                return; // ja na fila
        s_PendingDestroy.push_back({ entity.GetScene(), entity.GetHandle() });
    }

    void ScriptEngine::FlushDestroyQueue(Scene& scene) {
        // Move para fora: OnDestroy de um script pode enfileirar mais pedidos.
        std::vector<PendingDestroy> pending;
        pending.swap(s_PendingDestroy);
        for (const PendingDestroy& item : pending) {
            if (item.SceneKey != &scene)
                continue;
            Entity entity(item.Handle, &scene);
            if (entity.IsValid()) // pode ja ter sido destruida como filha de outra
                scene.DestroyEntity(entity);
        }
    }

    std::vector<ScriptPropertyValue> ScriptEngine::ReadScriptProperties(const std::filesystem::path& scriptAbsolutePath) {
        std::vector<ScriptPropertyValue> result;
        if (!s_Lua)
            return result;

        std::ifstream file(scriptAbsolutePath);
        if (!file.is_open())
            return result;
        std::stringstream buffer;
        buffer << file.rdbuf();

        sol::load_result loadResult = s_Lua->load(buffer.str(), scriptAbsolutePath.string());
        if (!loadResult.valid())
            return result;

        // Ambiente descartavel com entity = nil: so o corpo do arquivo roda
        // (OnCreate nao), e erros sao ignorados - o painel so quer os padroes.
        sol::environment env(*s_Lua, sol::create, s_Lua->globals());
        env["entity"] = sol::lua_nil;
        sol::protected_function chunk = loadResult;
        sol::set_environment(env, chunk);
        sol::protected_function_result run = chunk();
        if (!run.valid())
            return result;

        sol::object propertiesObject = env["Properties"];
        if (!propertiesObject.is<sol::table>())
            return result;
        sol::table properties = propertiesObject;
        for (auto& [keyObject, valueObject] : properties) {
            if (keyObject.get_type() != sol::type::string)
                continue;
            ScriptPropertyValue value;
            value.Name = keyObject.as<std::string>();
            if (valueObject.get_type() == sol::type::number) {
                value.Type = ScriptPropertyType::Number;
                value.Number = valueObject.as<double>();
            } else if (valueObject.get_type() == sol::type::boolean) {
                value.Type = ScriptPropertyType::Bool;
                value.Bool = valueObject.as<bool>();
            } else if (valueObject.get_type() == sol::type::string) {
                value.Type = ScriptPropertyType::String;
                value.String = valueObject.as<std::string>();
            } else {
                continue;
            }
            result.push_back(std::move(value));
        }
        std::sort(result.begin(), result.end(),
                  [](const ScriptPropertyValue& a, const ScriptPropertyValue& b) { return a.Name < b.Name; });
        return result;
    }

}
