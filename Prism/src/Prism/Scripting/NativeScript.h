#pragma once

// ============================================================================
// NativeScript.h
// SDK de scripts em C++. Este e o UNICO header que um modulo de scripts do
// projeto (DLL/.so em <Projeto>/Source) precisa incluir:
//
//     #include <Prism/Scripting/NativeScript.h>
//     using namespace Prism;
//
//     class Spinner : public ScriptableEntity {
//     public:
//         void OnUpdate(float dt) override {
//             GetTransform().Rotation.y += 90.0f * dt;
//         }
//     };
//     PRISM_REGISTER_SCRIPT(Spinner)
//
// Como funciona:
//  - O projeto vira uma biblioteca compartilhada (NativeScripts.dll/.so) que o
//    editor compila (menu "Scripts C++") e carrega em runtime.
//  - O modulo NAO linka com a engine. Tudo que precisa de codigo da engine
//    (log, input, criar/destruir entidades, fisica...) passa pela tabela
//    NativeEngineAPI, entregue ao modulo quando ele e carregado. Os helpers
//    de ScriptableEntity (Log, IsKeyDown, FindEntity...) ja usam essa tabela.
//  - Components (GetComponent<T>) sao header-only e funcionam direto, desde
//    que o modulo seja compilado com o MESMO compilador, configuracao
//    (Debug/Release) e versoes de EnTT/GLM da engine - o editor gera o
//    projeto CMake ja com essas configuracoes.
//  - Nao chame funcoes da engine que nao estejam aqui (ex: Scene::CreateEntity
//    direto): em Windows isso da erro de link. Use os helpers.
// ============================================================================

#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Core/Input.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
    #define PRISM_NATIVE_EXPORT extern "C" __declspec(dllexport)
#else
    #define PRISM_NATIVE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace Prism {

    // Sobe sempre que a struct NativeEngineAPI ou o contrato do modulo mudar;
    // um modulo compilado com outra versao e recusado ao carregar.
    constexpr uint32_t kNativeApiVersion = 8;

    struct NativeRayHit {
        bool Hit = false;
        entt::entity Entity = entt::null;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        float Distance = 0.0f;
    };

    // Funcoes da engine disponiveis para o modulo. Preenchida pela engine
    // (NativeScripting.cpp); o modulo so le.
    struct NativeEngineAPI {
        uint32_t Version;

        void (*Log)(int level, const char* message); // 0 trace, 1 info, 2 warn, 3 error

        bool (*IsKeyDown)(int key);
        bool (*IsKeyPressed)(int key);
        bool (*IsMouseButtonDown)(int button);
        float (*GetMouseDeltaX)();
        float (*GetMouseDeltaY)();

        float (*GetDeltaTime)();
        double (*GetElapsed)();

        entt::entity (*FindEntity)(Scene* scene, const char* name);
        entt::entity (*CreateEntity)(Scene* scene, const char* name);
        entt::entity (*Instantiate)(Scene* scene, const char* projectRelativePath, const glm::vec3* position);
        void (*DestroyEntity)(Scene* scene, entt::entity entity); // adiado para o fim do frame

        void (*ApplyImpulse)(Scene* scene, entt::entity entity, glm::vec3 impulse);
        void (*SetVelocity)(Scene* scene, entt::entity entity, glm::vec3 velocity);
        glm::vec3 (*GetVelocity)(Scene* scene, entt::entity entity);
        bool (*Raycast)(Scene* scene, glm::vec3 origin, glm::vec3 direction, float maxDistance, entt::entity ignore, NativeRayHit* outHit);

        void (*PlaySound)(Scene* scene, entt::entity entity);
        void (*StopSound)(Scene* scene, entt::entity entity);

        // v2: particulas (a configuracao do emissor se edita pelo ParticleEmitterComponent).
        void (*PlayParticles)(Scene* scene, entt::entity entity);
        void (*StopParticles)(Scene* scene, entt::entity entity);

        // v4: paridade com a API Lua.
        float (*GetFixedDeltaTime)();
        uint64_t (*GetFrameCount)();
        glm::mat4 (*GetWorldTransform)(Scene* scene, entt::entity entity);
        bool (*SetParent)(Scene* scene, entt::entity child, entt::entity parent); // parent == entt::null: vira raiz
        void (*ApplyForce)(Scene* scene, entt::entity entity, glm::vec3 force);
        entt::entity (*CreateMeshEntity)(Scene* scene, const char* name, int primitive, glm::vec3 position); // primitive = (int)PrimitiveMesh
        uint32_t (*FindAllEntities)(Scene* scene, const char* name, entt::entity* out, uint32_t capacity); // devolve o total achado

        float (*GetMouseX)();
        float (*GetMouseY)();
        void (*SetCursorMode)(int mode); // (int)CursorMode
        int (*GetCursorMode)();
        bool (*IsPointerOverUI)();

        // Audio: caminhos relativos a pasta do projeto; bus = (int)AudioBus.
        void (*PlaySoundFile)(const char* projectRelativePath, float volume, float pitch, int bus);
        void (*PlaySoundFile3D)(const char* projectRelativePath, glm::vec3 position, float volume, float pitch, int bus);
        void (*PreloadSound)(const char* projectRelativePath);
        void (*SetBusVolume)(int bus, float volume);
        float (*GetBusVolume)(int bus);
        void (*SetBusMuted)(int bus, bool muted);
        bool (*IsBusMuted)(int bus);
        void (*SetMasterVolume)(float volume);
        float (*GetMasterVolume)();

        // v5 - animacao (AnimatorComponent). loop: -1 = o do Animator, 0/1 = forca.
        bool (*PlayAnimation)(Scene* scene, entt::entity entity, const char* clip, float fadeSeconds, int loop);
        void (*StopAnimation)(Scene* scene, entt::entity entity);
        void (*SetAnimationPaused)(Scene* scene, entt::entity entity, bool paused);
        void (*SetAnimationSpeed)(Scene* scene, entt::entity entity, float speed);
        bool (*IsAnimationPlaying)(Scene* scene, entt::entity entity);

        // v6 - troca de cena (aplicada pela engine DEPOIS do frame atual).
        void (*LoadScene)(Scene* scene, const char* mapPath); // relativo a pasta de mapas, com ou sem .prismmap
        void (*ReloadScene)(Scene* scene);

        // v7 - maquina de estados do Animator.
        bool (*SetAnimFloat)(Scene* scene, entt::entity entity, const char* name, float value);
        bool (*SetAnimBool)(Scene* scene, entt::entity entity, const char* name, bool value);
        bool (*SetAnimTrigger)(Scene* scene, entt::entity entity, const char* name);
        bool (*SetAnimState)(Scene* scene, entt::entity entity, const char* state, float fadeSeconds);
        const char* (*GetAnimState)(Scene* scene, entt::entity entity); // valido ate a proxima chamada

        // v8 - documentos de UI (.prismui, UIDocumentComponent). 'nodeId' e o atributo id do elemento.
        // Todas devolvem false se a entidade nao tem documento ou o id nao existe.
        bool (*UISetText)(Scene* scene, entt::entity entity, const char* nodeId, const char* text);
        const char* (*UIGetText)(Scene* scene, entt::entity entity, const char* nodeId); // valido ate a proxima chamada; "" se nao achou
        bool (*UISetVisible)(Scene* scene, entt::entity entity, const char* nodeId, bool visible);
        bool (*UISetInteractable)(Scene* scene, entt::entity entity, const char* nodeId, bool interactable);
        bool (*UISetColor)(Scene* scene, entt::entity entity, const char* nodeId, float r, float g, float b, float a);
        bool (*UISetTextColor)(Scene* scene, entt::entity entity, const char* nodeId, float r, float g, float b, float a);
    };

    // --- Variaveis publicas (editaveis no painel Propriedades) ---------------
    // O visitante e implementado pela ENGINE; o modulo so o chama, via
    // NativeProperties abaixo. Cada chamada recebe o valor atual do membro e
    // devolve o override do editor (se houver).
    class NativePropertyVisitor {
    public:
        virtual bool Number(const char* name, double current, double* overrideValue) = 0;
        virtual bool Bool(const char* name, bool current, bool* overrideValue) = 0;
        virtual const char* String(const char* name, const char* current) = 0; // nullptr = sem override
    protected:
        ~NativePropertyVisitor() = default;
    };

    // Passado a ScriptableEntity::OnProperties. Exemplo:
    //   void OnProperties(NativeProperties& p) override {
    //       p.Float("speed", m_Speed);
    //       p.Bool("canJump", m_CanJump);
    //   }
    // O valor inicial do membro vira o PADRAO mostrado no editor. Chamado antes de
    // OnCreate (e no editor, num objeto SEM entidade - nao use GetEntity() aqui).
    class NativeProperties {
    public:
        explicit NativeProperties(NativePropertyVisitor& visitor) : m_Visitor(visitor) {}

        void Float(const char* name, float& value) { double o; if (m_Visitor.Number(name, (double)value, &o)) value = (float)o; }
        void Double(const char* name, double& value) { double o; if (m_Visitor.Number(name, value, &o)) value = o; }
        void Int(const char* name, int& value) { double o; if (m_Visitor.Number(name, (double)value, &o)) value = (int)(o < 0.0 ? o - 0.5 : o + 0.5); }
        void Bool(const char* name, bool& value) { bool o; if (m_Visitor.Bool(name, value, &o)) value = o; }
        void String(const char* name, std::string& value) { if (const char* o = m_Visitor.String(name, value.c_str())) value = o; }

    private:
        NativePropertyVisitor& m_Visitor;
    };

    // Classe base de um script nativo. Uma instancia por entidade que tenha
    // um NativeScriptComponent apontando para a classe.
    class ScriptableEntity {
    public:
        virtual ~ScriptableEntity() = default;

        // Ciclo de vida (todos opcionais).
        virtual void OnCreate() {}
        virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
        // Passo FIXO (1/60 s, o mesmo da fisica): chamado 0 ou mais vezes por frame,
        // ANTES de cada passo da fisica - o lugar certo para ApplyForce/SetVelocity.
        virtual void OnFixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }
        virtual void OnDestroy() {}
        virtual void OnCollisionEnter(Entity other) { (void)other; }
        virtual void OnCollisionExit(Entity other) { (void)other; }
        virtual void OnTriggerEnter(Entity other) { (void)other; }
        virtual void OnTriggerExit(Entity other) { (void)other; }
        // Botao de UI (UIElementComponent::Button) desta entidade foi clicado.
        virtual void OnClick() {}
        // Botao de um documento .prismui (UIDocumentComponent) desta entidade clicado. 'handler' e o
        // atributo onClick do botao ("" se nao tem) e 'nodeId' o id dele. O padrao chama OnClick()
        // quando o botao nao tem onClick; sobrescreva para tratar por nome.
        virtual void OnUIClick(const char* handler, const char* nodeId) {
            (void)nodeId;
            if (!handler || !*handler)
                OnClick();
        }
        // Evento de animacao (AnimatorComponent::Events): 'name' e o nome do evento.
        virtual void OnAnimationEvent(const char* name) { (void)name; }
        // Declara as variaveis publicas (ver NativeProperties). Opcional.
        virtual void OnProperties(NativeProperties& properties) { (void)properties; }

        // Uso da engine: associa a instancia a entidade antes de OnCreate.
        void PrismBind(Entity entity) { m_Entity = entity; }

        Entity GetEntity() const { return m_Entity; }
        Scene& GetScene() { return *m_Entity.GetScene(); }

        template<typename T>
        T& GetComponent() { return m_Entity.GetComponent<T>(); }
        template<typename T>
        bool HasComponent() { return m_Entity.HasComponent<T>(); }
        TransformComponent& GetTransform() { return m_Entity.GetComponent<TransformComponent>(); }

        // --- Helpers (passam pela NativeEngineAPI) ------------------------
        void LogInfo(const std::string& message) const { Api().Log(1, message.c_str()); }
        void LogWarn(const std::string& message) const { Api().Log(2, message.c_str()); }
        void LogError(const std::string& message) const { Api().Log(3, message.c_str()); }

        bool IsKeyDown(Key key) const { return Api().IsKeyDown((int)key); }
        bool IsKeyPressed(Key key) const { return Api().IsKeyPressed((int)key); }
        bool IsMouseButtonDown(MouseButton button) const { return Api().IsMouseButtonDown((int)button); }
        float GetMouseDeltaX() const { return Api().GetMouseDeltaX(); }
        float GetMouseDeltaY() const { return Api().GetMouseDeltaY(); }

        float GetDeltaTime() const { return Api().GetDeltaTime(); }
        double GetElapsedTime() const { return Api().GetElapsed(); }
        float GetFixedDeltaTime() const { return Api().GetFixedDeltaTime(); }
        uint64_t GetFrameCount() const { return Api().GetFrameCount(); }

        float GetMouseX() const { return Api().GetMouseX(); }
        float GetMouseY() const { return Api().GetMouseY(); }
        void SetCursorMode(CursorMode mode) const { Api().SetCursorMode((int)mode); }
        CursorMode GetCursorMode() const { return (CursorMode)Api().GetCursorMode(); }
        // O mouse esta sobre um elemento de UI que barra o ponteiro? (UI.IsPointerOverUI no Lua)
        bool IsPointerOverUI() const { return Api().IsPointerOverUI(); }

        // --- Transform de mundo / hierarquia ---------------------------------
        glm::mat4 GetWorldTransform() { return Api().GetWorldTransform(m_Entity.GetScene(), m_Entity.GetHandle()); }
        glm::vec3 GetWorldPosition() { return glm::vec3(GetWorldTransform()[3]); }
        glm::vec3 GetWorldForward() { return glm::normalize(glm::mat3(GetWorldTransform()) * glm::vec3(0.0f, 0.0f, -1.0f)); }
        glm::vec3 GetWorldRight() { return glm::normalize(glm::mat3(GetWorldTransform()) * glm::vec3(1.0f, 0.0f, 0.0f)); }
        // 'parent' invalido (Entity()) torna a entidade raiz. false se recusado (ciclo).
        bool SetParent(Entity parent) {
            return Api().SetParent(m_Entity.GetScene(), m_Entity.GetHandle(), parent.IsValid() ? parent.GetHandle() : entt::entity(entt::null));
        }
        // Todas as entidades com este nome (Tag). Equivale a Scene.FindAll do Lua.
        std::vector<Entity> FindAllEntities(const std::string& name) {
            std::vector<entt::entity> handles(64, entt::entity(entt::null));
            uint32_t total = Api().FindAllEntities(m_Entity.GetScene(), name.c_str(), handles.data(), (uint32_t)handles.size());
            if (total > handles.size()) {
                handles.assign(total, entt::entity(entt::null));
                Api().FindAllEntities(m_Entity.GetScene(), name.c_str(), handles.data(), total);
            }
            std::vector<Entity> result;
            for (uint32_t i = 0; i < total && i < handles.size(); ++i)
                result.emplace_back(handles[i], m_Entity.GetScene());
            return result;
        }
        // primitive: PrimitiveMesh::Cube/Sphere/Capsule/Cylinder/Plane (Scene.CreateMesh do Lua).
        Entity CreateMeshEntity(const std::string& name, PrimitiveMesh primitive, const glm::vec3& position = glm::vec3(0.0f)) {
            entt::entity handle = Api().CreateMeshEntity(m_Entity.GetScene(), name.c_str(), (int)primitive, position);
            return handle == entt::null ? Entity() : Entity(handle, m_Entity.GetScene());
        }

        // Entidade invalida (Entity::IsValid() == false) se nao achar.
        Entity FindEntity(const std::string& name) {
            entt::entity handle = Api().FindEntity(m_Entity.GetScene(), name.c_str());
            return handle == entt::null ? Entity() : Entity(handle, m_Entity.GetScene());
        }
        Entity CreateEntity(const std::string& name) {
            entt::entity handle = Api().CreateEntity(m_Entity.GetScene(), name.c_str());
            return handle == entt::null ? Entity() : Entity(handle, m_Entity.GetScene());
        }
        // Caminho relativo a pasta do projeto, ex: "Assets/Prefabs/Bala.prismprefab".
        Entity Instantiate(const std::string& prefabPath, const glm::vec3& position) {
            entt::entity handle = Api().Instantiate(m_Entity.GetScene(), prefabPath.c_str(), &position);
            return handle == entt::null ? Entity() : Entity(handle, m_Entity.GetScene());
        }
        Entity Instantiate(const std::string& prefabPath) {
            entt::entity handle = Api().Instantiate(m_Entity.GetScene(), prefabPath.c_str(), nullptr);
            return handle == entt::null ? Entity() : Entity(handle, m_Entity.GetScene());
        }
        // Destroi no fim do frame (seguro de chamar de qualquer callback).
        void Destroy(Entity entity) { Api().DestroyEntity(m_Entity.GetScene(), entity.GetHandle()); }
        void Destroy() { Destroy(m_Entity); }

        // Forca continua (use em OnFixedUpdate); ApplyImpulse e instantaneo.
        void ApplyForce(const glm::vec3& force) { Api().ApplyForce(m_Entity.GetScene(), m_Entity.GetHandle(), force); }
        void ApplyImpulse(const glm::vec3& impulse) { Api().ApplyImpulse(m_Entity.GetScene(), m_Entity.GetHandle(), impulse); }
        void SetVelocity(const glm::vec3& velocity) { Api().SetVelocity(m_Entity.GetScene(), m_Entity.GetHandle(), velocity); }
        glm::vec3 GetVelocity() { return Api().GetVelocity(m_Entity.GetScene(), m_Entity.GetHandle()); }
        NativeRayHit Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1000.0f, bool ignoreSelf = true) {
            NativeRayHit hit;
            Api().Raycast(m_Entity.GetScene(), origin, direction, maxDistance, ignoreSelf ? m_Entity.GetHandle() : entt::entity(entt::null), &hit);
            return hit;
        }

        void PlaySound() { Api().PlaySound(m_Entity.GetScene(), m_Entity.GetHandle()); }
        void StopSound() { Api().StopSound(m_Entity.GetScene(), m_Entity.GetHandle()); }

        // Sons soltos (caminho relativo a pasta do projeto) e canais de volume.
        void PlayAudio(const std::string& path, float volume = 1.0f, float pitch = 1.0f, AudioBus bus = AudioBus::SFX) const {
            Api().PlaySoundFile(path.c_str(), volume, pitch, (int)bus);
        }
        void PlayAudio3D(const std::string& path, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f, AudioBus bus = AudioBus::SFX) const {
            Api().PlaySoundFile3D(path.c_str(), position, volume, pitch, (int)bus);
        }
        void PreloadAudio(const std::string& path) const { Api().PreloadSound(path.c_str()); }
        void SetBusVolume(AudioBus bus, float volume) const { Api().SetBusVolume((int)bus, volume); }
        float GetBusVolume(AudioBus bus) const { return Api().GetBusVolume((int)bus); }
        void SetBusMuted(AudioBus bus, bool muted) const { Api().SetBusMuted((int)bus, muted); }
        bool IsBusMuted(AudioBus bus) const { return Api().IsBusMuted((int)bus); }
        void SetMasterVolume(float volume) const { Api().SetMasterVolume(volume); }
        float GetMasterVolume() const { return Api().GetMasterVolume(); }

        // Animacao (precisa de um Animator na entidade). PlayAnimation("Walk", 0.2f) troca de
        // clipe com crossfade de 0.2 s; chamar com o clipe que ja toca nao reinicia.
        // loop: -1 = usa o do Animator, 0 = uma vez, 1 = repete.
        bool PlayAnimation(const std::string& clip, float fadeSeconds = 0.0f, int loop = -1) {
            return Api().PlayAnimation(m_Entity.GetScene(), m_Entity.GetHandle(), clip.c_str(), fadeSeconds, loop);
        }
        void StopAnimation() { Api().StopAnimation(m_Entity.GetScene(), m_Entity.GetHandle()); }
        void PauseAnimation(bool paused = true) { Api().SetAnimationPaused(m_Entity.GetScene(), m_Entity.GetHandle(), paused); }
        void SetAnimationSpeed(float speed) { Api().SetAnimationSpeed(m_Entity.GetScene(), m_Entity.GetHandle(), speed); }
        bool IsAnimationPlaying() { return Api().IsAnimationPlaying(m_Entity.GetScene(), m_Entity.GetHandle()); }

        // Maquina de estados do Animator (grafo montado no editor). Parametros por nome.
        bool SetAnimFloat(const std::string& name, float value) { return Api().SetAnimFloat(m_Entity.GetScene(), m_Entity.GetHandle(), name.c_str(), value); }
        bool SetAnimBool(const std::string& name, bool value) { return Api().SetAnimBool(m_Entity.GetScene(), m_Entity.GetHandle(), name.c_str(), value); }
        bool SetAnimTrigger(const std::string& name) { return Api().SetAnimTrigger(m_Entity.GetScene(), m_Entity.GetHandle(), name.c_str()); }
        bool SetAnimState(const std::string& state, float fadeSeconds = 0.0f) { return Api().SetAnimState(m_Entity.GetScene(), m_Entity.GetHandle(), state.c_str(), fadeSeconds); }
        std::string GetAnimState() { return Api().GetAnimState(m_Entity.GetScene(), m_Entity.GetHandle()); }

        // UI em documento (.prismui): elementos por id. Retornam false se nao achar.
        bool UISetText(const std::string& nodeId, const std::string& text) { return Api().UISetText(m_Entity.GetScene(), m_Entity.GetHandle(), nodeId.c_str(), text.c_str()); }
        std::string UIGetText(const std::string& nodeId) { return Api().UIGetText(m_Entity.GetScene(), m_Entity.GetHandle(), nodeId.c_str()); }
        bool UISetVisible(const std::string& nodeId, bool visible) { return Api().UISetVisible(m_Entity.GetScene(), m_Entity.GetHandle(), nodeId.c_str(), visible); }
        bool UISetInteractable(const std::string& nodeId, bool interactable) { return Api().UISetInteractable(m_Entity.GetScene(), m_Entity.GetHandle(), nodeId.c_str(), interactable); }
        bool UISetColor(const std::string& nodeId, float r, float g, float b, float a = 1.0f) { return Api().UISetColor(m_Entity.GetScene(), m_Entity.GetHandle(), nodeId.c_str(), r, g, b, a); }
        bool UISetTextColor(const std::string& nodeId, float r, float g, float b, float a = 1.0f) { return Api().UISetTextColor(m_Entity.GetScene(), m_Entity.GetHandle(), nodeId.c_str(), r, g, b, a); }

        // Troca de cena: carrega o mapa (relativo a pasta de mapas) DEPOIS do frame atual.
        // Todos os scripts e entidades da cena atual sao destruidos.
        void LoadScene(const std::string& mapPath) { Api().LoadScene(m_Entity.GetScene(), mapPath.c_str()); }
        void ReloadScene() { Api().ReloadScene(m_Entity.GetScene()); }

        void PlayParticles() { Api().PlayParticles(m_Entity.GetScene(), m_Entity.GetHandle()); }
        void StopParticles() { Api().StopParticles(m_Entity.GetScene(), m_Entity.GetHandle()); }

        // Acesso a tabela (preenchida em PrismNative_Init). Definida aqui
        // para ser uma variavel POR MODULO: cada DLL tem a sua copia.
        static const NativeEngineAPI*& ApiPointer() {
            static const NativeEngineAPI* api = nullptr;
            return api;
        }

    private:
        static const NativeEngineAPI& Api() { return *ApiPointer(); }

        Entity m_Entity;
    };

    // --- Registro de classes (lado do modulo) ---------------------------------
    namespace NativeModule {
        using CreateFn = ScriptableEntity* (*)();
        using DestroyFn = void (*)(ScriptableEntity*);

        struct ClassEntry {
            const char* Name;
            CreateFn Create;
            DestroyFn Destroy; // deletar no PROPRIO modulo (heap do modulo)
        };

        inline std::vector<ClassEntry>& LocalClasses() {
            static std::vector<ClassEntry> classes;
            return classes;
        }

        struct Registrar {
            Registrar(const char* name, CreateFn create, DestroyFn destroy) {
                LocalClasses().push_back({ name, create, destroy });
            }
        };
    }

}

// Registra a classe (derivada de ScriptableEntity, com construtor padrao) para
// aparecer no combo do component "Native Script". O nome no editor e o nome
// da classe. Use no .cpp ou no .h, uma vez por classe.
#define PRISM_REGISTER_SCRIPT(ClassName)                                                              \
    namespace {                                                                                       \
        ::Prism::ScriptableEntity* PrismCreate_##ClassName() { return new ClassName(); }              \
        void PrismDestroy_##ClassName(::Prism::ScriptableEntity* script) { delete script; }           \
        const ::Prism::NativeModule::Registrar PrismRegistrar_##ClassName(                            \
            #ClassName, &PrismCreate_##ClassName, &PrismDestroy_##ClassName);                         \
    }

// Pontos de entrada exportados do modulo - use UMA vez por modulo (o projeto
// gerado pelo editor ja faz isso em PrismModuleEntry.cpp).
#define PRISM_NATIVE_MODULE_ENTRY()                                                                   \
    PRISM_NATIVE_EXPORT uint32_t PrismNative_ApiVersion() { return ::Prism::kNativeApiVersion; }      \
    PRISM_NATIVE_EXPORT void PrismNative_Init(const ::Prism::NativeEngineAPI* api) {                  \
        ::Prism::ScriptableEntity::ApiPointer() = api;                                                \
    }                                                                                                 \
    PRISM_NATIVE_EXPORT uint32_t PrismNative_ClassCount() {                                           \
        return (uint32_t)::Prism::NativeModule::LocalClasses().size();                                \
    }                                                                                                 \
    PRISM_NATIVE_EXPORT const ::Prism::NativeModule::ClassEntry* PrismNative_GetClass(uint32_t index) { \
        auto& classes = ::Prism::NativeModule::LocalClasses();                                        \
        return index < classes.size() ? &classes[index] : nullptr;                                    \
    }
