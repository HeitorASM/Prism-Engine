#include "NativeScripting.h"
#include "ScriptEngine.h"
#include "../Scene/Scene.h"
#include "../Physics/PhysicsEngine.h"
#include "../Audio/AudioEngine.h"
#include "../Particles/ParticleSystem.h"
#include "../Core/Input.h"
#include "../Core/Log.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <unistd.h>
    #if defined(__APPLE__)
        #include <mach-o/dyld.h>
    #endif
#endif

namespace Prism {

    NativeScripting::ModuleState NativeScripting::s_Module;
    std::vector<std::string> NativeScripting::s_ClassNames;
    std::unordered_map<std::string, NativeScripting::ClassInfo> NativeScripting::s_Classes;
    std::unordered_map<NativeScripting::InstanceKey, NativeScripting::Instance, NativeScripting::InstanceKeyHash> NativeScripting::s_Instances;
    unsigned NativeScripting::s_LoadCounter = 0;

    namespace {

        // --- Tabela de API entregue aos modulos --------------------------------
        void ApiLog(int level, const char* message) {
            LogLevel mapped = LogLevel::Info;
            if (level <= 0) mapped = LogLevel::Trace;
            else if (level == 2) mapped = LogLevel::Warn;
            else if (level >= 3) mapped = LogLevel::Error;
            Log::AppLog(mapped, "[C++] ", message ? message : "");
        }

        bool ApiIsKeyDown(int key) { return Input::IsKeyDown(key); }
        bool ApiIsKeyPressed(int key) { return Input::IsKeyPressed(key); }
        bool ApiIsMouseButtonDown(int button) { return Input::IsMouseButtonDown(button); }
        float ApiMouseDeltaX() { return Input::GetMouseDeltaX(); }
        float ApiMouseDeltaY() { return Input::GetMouseDeltaY(); }

        // Mesmo relogio de Time.* do Lua (ver ScriptEngine::BeginFrame).
        float ApiDeltaTime() { return ScriptEngine::GetDeltaTime(); }
        double ApiElapsed() { return ScriptEngine::GetElapsed(); }

        entt::entity ApiFindEntity(Scene* scene, const char* name) {
            if (!scene || !name)
                return entt::null;
            auto view = scene->GetRegistry().view<TagComponent>();
            for (auto handle : view) {
                if (view.get<TagComponent>(handle).Tag == name)
                    return handle;
            }
            return entt::null;
        }

        entt::entity ApiCreateEntity(Scene* scene, const char* name) {
            if (!scene)
                return entt::null;
            return scene->CreateEntity(name ? name : "Entity").GetHandle();
        }

        entt::entity ApiInstantiate(Scene* scene, const char* path, const glm::vec3* position) {
            if (!scene || !path)
                return entt::null;
            Entity created = ScriptEngine::InstantiateRuntime(*scene, path, position);
            return created.IsValid() ? created.GetHandle() : entt::null;
        }

        void ApiDestroyEntity(Scene* scene, entt::entity entity) {
            if (scene)
                ScriptEngine::QueueDestroy(Entity(entity, scene));
        }

        void ApiApplyImpulse(Scene* scene, entt::entity entity, glm::vec3 impulse) {
            if (scene)
                PhysicsEngine::ApplyLinearImpulse(*scene, Entity(entity, scene), impulse);
        }
        void ApiSetVelocity(Scene* scene, entt::entity entity, glm::vec3 velocity) {
            if (scene)
                PhysicsEngine::SetLinearVelocity(*scene, Entity(entity, scene), velocity);
        }
        glm::vec3 ApiGetVelocity(Scene* scene, entt::entity entity) {
            return scene ? PhysicsEngine::GetLinearVelocity(*scene, Entity(entity, scene)) : glm::vec3(0.0f);
        }

        bool ApiRaycast(Scene* scene, glm::vec3 origin, glm::vec3 direction, float maxDistance, entt::entity ignore, NativeRayHit* outHit) {
            if (!scene || !outHit)
                return false;
            std::vector<entt::entity> ignoreList;
            if (ignore != entt::null)
                ignoreList.push_back(ignore);
            RaycastHit hit = PhysicsEngine::Raycast(*scene, origin, direction, maxDistance, ignoreList);
            outHit->Hit = hit.Hit;
            if (hit.Hit) {
                outHit->Entity = hit.Entity;
                outHit->Point = hit.Point;
                outHit->Normal = hit.Normal;
                outHit->Distance = hit.Distance;
            }
            return hit.Hit;
        }

        void ApiPlaySound(Scene* scene, entt::entity entity) {
            if (scene)
                AudioEngine::PlayEntity(Entity(entity, scene));
        }
        void ApiStopSound(Scene* scene, entt::entity entity) {
            if (scene)
                AudioEngine::StopEntity(Entity(entity, scene));
        }

        void ApiPlayParticles(Scene* scene, entt::entity entity) {
            if (scene)
                ParticleSystem::Play(Entity(entity, scene));
        }
        void ApiStopParticles(Scene* scene, entt::entity entity) {
            if (scene)
                ParticleSystem::Stop(Entity(entity, scene));
        }

        const NativeEngineAPI& GetApiTable() {
            static const NativeEngineAPI api = {
                kNativeApiVersion,
                &ApiLog,
                &ApiIsKeyDown, &ApiIsKeyPressed, &ApiIsMouseButtonDown, &ApiMouseDeltaX, &ApiMouseDeltaY,
                &ApiDeltaTime, &ApiElapsed,
                &ApiFindEntity, &ApiCreateEntity, &ApiInstantiate, &ApiDestroyEntity,
                &ApiApplyImpulse, &ApiSetVelocity, &ApiGetVelocity, &ApiRaycast,
                &ApiPlaySound, &ApiStopSound,
                &ApiPlayParticles, &ApiStopParticles
            };
            return api;
        }

        // --- Biblioteca dinamica ---------------------------------------------------
        void* OpenLibrary(const std::filesystem::path& path) {
#if defined(_WIN32)
            return (void*)LoadLibraryW(path.wstring().c_str());
#else
            return dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        }
        void CloseLibrary(void* handle) {
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#else
            dlclose(handle);
#endif
        }
        void* FindSymbol(void* handle, const char* name) {
#if defined(_WIN32)
            return (void*)GetProcAddress((HMODULE)handle, name);
#else
            return dlsym(handle, name);
#endif
        }

        std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
            wchar_t buffer[MAX_PATH];
            DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            return std::filesystem::path(std::wstring(buffer, length)).parent_path();
#elif defined(__APPLE__)
            char buffer[4096];
            uint32_t size = sizeof(buffer);
            if (_NSGetExecutablePath(buffer, &size) == 0)
                return std::filesystem::path(buffer).parent_path();
            return std::filesystem::current_path();
#else
            char buffer[4096];
            ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
            if (length > 0)
                return std::filesystem::path(std::string(buffer, (size_t)length)).parent_path();
            return std::filesystem::current_path();
#endif
        }

        const char* ModuleExtension() {
#if defined(_WIN32)
            return ".dll";
#elif defined(__APPLE__)
            return ".dylib";
#else
            return ".so";
#endif
        }

        // Escreve 'content' em 'path' so se o arquivo ainda nao existir.
        bool WriteIfMissing(const std::filesystem::path& path, const std::string& content) {
            std::error_code ec;
            if (std::filesystem::exists(path, ec))
                return false;
            std::ofstream file(path, std::ios::binary);
            file << content;
            return true;
        }
    }

    void NativeScripting::Init() {
        // Nada a preparar por enquanto: o modulo e carregado sob demanda
        // (LoadProjectModule) quando um projeto e aberto.
    }

    void NativeScripting::Shutdown() {
        UnloadAll();
        UnloadModule();
    }

    std::filesystem::path NativeScripting::GetModulePath(const Project& project) {
        return project.GetProjectDirectory() / "Binaries" / (std::string("NativeScripts") + ModuleExtension());
    }

    std::filesystem::path NativeScripting::GetSdkFile() {
        return ExecutableDirectory() / "PrismSDK.cmake";
    }

    bool NativeScripting::HasClass(const std::string& name) {
        return s_Classes.find(name) != s_Classes.end();
    }

    bool NativeScripting::LoadProjectModule(const Project& project) {
        if (!s_Instances.empty()) {
            PRISM_CORE_ERROR("NativeScripting: nao e possivel recarregar o modulo com scripts em execucao (pare o Play).");
            return false;
        }
        UnloadModule();

        std::filesystem::path source = GetModulePath(project);
        std::error_code ec;
        if (!std::filesystem::exists(source, ec))
            return false; // projeto sem scripts C++ compilados ainda - nao e erro

        // Copia para Cache/NativeModule: o original fica livre para o compilador.
        std::filesystem::path cacheDirectory = project.GetProjectDirectory() / "Cache" / "NativeModule";
        std::filesystem::create_directories(cacheDirectory, ec);
        for (auto& entry : std::filesystem::directory_iterator(cacheDirectory, ec)) {
            std::error_code ignored;
            std::filesystem::remove(entry.path(), ignored); // copias antigas (as travadas ficam)
        }
        std::filesystem::path copy = cacheDirectory / ("NativeScripts_" + std::to_string(++s_LoadCounter) + ModuleExtension());
        std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            PRISM_CORE_ERROR("NativeScripting: falha ao copiar o modulo: ", ec.message());
            return false;
        }

        void* handle = OpenLibrary(copy);
        if (!handle) {
            PRISM_CORE_ERROR("NativeScripting: nao foi possivel carregar '", source.string(), "' (compilador/configuracao diferentes da engine?).");
            return false;
        }

        using VersionFn = uint32_t (*)();
        using InitFn = void (*)(const NativeEngineAPI*);
        using CountFn = uint32_t (*)();
        using GetClassFn = const NativeModule::ClassEntry* (*)(uint32_t);

        auto versionFn = (VersionFn)FindSymbol(handle, "PrismNative_ApiVersion");
        auto initFn = (InitFn)FindSymbol(handle, "PrismNative_Init");
        auto countFn = (CountFn)FindSymbol(handle, "PrismNative_ClassCount");
        auto classFn = (GetClassFn)FindSymbol(handle, "PrismNative_GetClass");
        if (!versionFn || !initFn || !countFn || !classFn) {
            PRISM_CORE_ERROR("NativeScripting: o modulo nao exporta os pontos de entrada (falta PRISM_NATIVE_MODULE_ENTRY()).");
            CloseLibrary(handle);
            return false;
        }
        if (versionFn() != kNativeApiVersion) {
            PRISM_CORE_ERROR("NativeScripting: modulo compilado para outra versao da API (modulo=", versionFn(), ", engine=", kNativeApiVersion, "). Recompile.");
            CloseLibrary(handle);
            return false;
        }

        initFn(&GetApiTable());
        s_Module.Handle = handle;
        s_Module.LoadedCopy = copy;
        uint32_t count = countFn();
        for (uint32_t i = 0; i < count; i++) {
            const NativeModule::ClassEntry* entry = classFn(i);
            if (!entry || !entry->Name || !entry->Create || !entry->Destroy)
                continue;
            s_Classes[entry->Name] = ClassInfo{ entry->Create, entry->Destroy };
            s_ClassNames.push_back(entry->Name);
        }
        std::sort(s_ClassNames.begin(), s_ClassNames.end());
        PRISM_CORE_INFO("NativeScripting: modulo carregado (", s_ClassNames.size(), " classe(s)).");
        return true;
    }

    void NativeScripting::UnloadModule() {
        UnloadAll(); // instancias apontam para codigo do modulo
        s_Classes.clear();
        s_ClassNames.clear();
        if (s_Module.Handle) {
            CloseLibrary(s_Module.Handle);
            s_Module.Handle = nullptr;
        }
        s_Module.LoadedCopy.clear();
    }

    bool NativeScripting::LoadScript(Entity entity) {
        if (!entity.HasComponent<NativeScriptComponent>())
            return false;
        const std::string& className = entity.GetComponent<NativeScriptComponent>().ClassName;
        if (className.empty())
            return false;

        auto classIt = s_Classes.find(className);
        if (classIt == s_Classes.end()) {
            PRISM_WARN("Script C++ '", className, "' nao encontrado no modulo (", entity.GetComponent<TagComponent>().Tag,
                       "). Compile o modulo em Scripts C++ > Compilar.");
            return false;
        }

        InstanceKey key{ entity.GetScene(), (uint32_t)entity.GetHandle() };
        if (s_Instances.find(key) != s_Instances.end())
            UnloadScript(entity);

        Instance instance;
        instance.Destroy = classIt->second.Destroy;
        try {
            instance.Object = classIt->second.Create();
            instance.Object->PrismBind(entity);
        }
        catch (const std::exception& error) {
            PRISM_ERROR("Script C++ '", className, "': excecao ao criar: ", error.what());
            return false;
        }
        if (!instance.Object)
            return false;

        Instance& stored = (s_Instances[key] = instance);
        try {
            stored.Object->OnCreate();
        }
        catch (const std::exception& error) {
            PRISM_ERROR("Script C++ '", className, "': excecao em OnCreate: ", error.what());
            stored.Faulted = true;
        }
        return true;
    }

    void NativeScripting::UpdateScript(Entity entity, float deltaTime) {
        auto it = s_Instances.find(InstanceKey{ entity.GetScene(), (uint32_t)entity.GetHandle() });
        if (it == s_Instances.end() || it->second.Faulted)
            return;
        try {
            it->second.Object->OnUpdate(deltaTime);
        }
        catch (const std::exception& error) {
            PRISM_ERROR("Script C++: excecao em OnUpdate: ", error.what());
            it->second.Faulted = true;
        }
    }

    void NativeScripting::UnloadScript(Entity entity) {
        auto it = s_Instances.find(InstanceKey{ entity.GetScene(), (uint32_t)entity.GetHandle() });
        if (it == s_Instances.end())
            return;
        Instance instance = it->second;
        s_Instances.erase(it);
        try {
            if (!instance.Faulted)
                instance.Object->OnDestroy();
        }
        catch (const std::exception& error) {
            PRISM_ERROR("Script C++: excecao em OnDestroy: ", error.what());
        }
        instance.Destroy(instance.Object);
    }

    void NativeScripting::UnloadAll() {
        // Move para fora: OnDestroy pode chamar a API (que so enfileira) sem mexer no mapa.
        std::unordered_map<InstanceKey, Instance, InstanceKeyHash> instances;
        instances.swap(s_Instances);
        for (auto& [key, instance] : instances) {
            try {
                if (!instance.Faulted)
                    instance.Object->OnDestroy();
            }
            catch (const std::exception& error) {
                PRISM_ERROR("Script C++: excecao em OnDestroy: ", error.what());
            }
            instance.Destroy(instance.Object);
        }
    }

    void NativeScripting::DispatchContactEvent(Entity self, Entity other, bool enter, bool trigger) {
        if (s_Instances.empty())
            return;
        auto it = s_Instances.find(InstanceKey{ self.GetScene(), (uint32_t)self.GetHandle() });
        if (it == s_Instances.end() || it->second.Faulted)
            return;
        ScriptableEntity* object = it->second.Object;
        try {
            if (trigger)
                enter ? object->OnTriggerEnter(other) : object->OnTriggerExit(other);
            else
                enter ? object->OnCollisionEnter(other) : object->OnCollisionExit(other);
        }
        catch (const std::exception& error) {
            PRISM_ERROR("Script C++: excecao em callback de colisao/trigger: ", error.what());
            it->second.Faulted = true;
        }
    }

    bool NativeScripting::GenerateProjectFiles(const Project& project, std::string& message) {
        std::filesystem::path sdk = GetSdkFile();
        std::error_code ec;
        if (!std::filesystem::exists(sdk, ec)) {
            message = "PrismSDK.cmake nao encontrado ao lado do editor (" + sdk.string() + "). Recompile a engine.";
            return false;
        }

        std::filesystem::path directory = GetSourceDirectory(project);
        std::filesystem::create_directories(directory, ec);
        if (ec) {
            message = "Nao foi possivel criar " + directory.string() + ": " + ec.message();
            return false;
        }
        std::filesystem::create_directories(project.GetProjectDirectory() / "Binaries", ec);

        std::string sdkPath = sdk.generic_string(); // barras normais: CMake aceita em todas as plataformas

        std::string cmake =
            "# Gerado pela Prism Engine (Scripts C++ > Gerar projeto). Pode editar.\n"
            "cmake_minimum_required(VERSION 3.20)\n"
            "project(PrismNativeScripts LANGUAGES CXX)\n"
            "\n"
            "# Caminhos de include da engine/GLM/EnTT (gerado no build da engine).\n"
            "include(\"" + sdkPath + "\")\n"
            "\n"
            "file(GLOB_RECURSE SCRIPT_SOURCES CONFIGURE_DEPENDS\n"
            "    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp\n"
            "    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)\n"
            "\n"
            "add_library(NativeScripts SHARED ${SCRIPT_SOURCES})\n"
            "target_include_directories(NativeScripts PRIVATE ${PRISM_SDK_INCLUDE_DIRS})\n"
            "target_compile_features(NativeScripts PRIVATE cxx_std_20)\n"
            "\n"
            "# Saida: <Projeto>/Binaries/NativeScripts.(dll|so|dylib)\n"
            "set(OUT_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/../Binaries\")\n"
            "set_target_properties(NativeScripts PROPERTIES\n"
            "    PREFIX \"\"\n"
            "    CXX_VISIBILITY_PRESET hidden\n"
            "    VISIBILITY_INLINES_HIDDEN ON\n"
            "    LIBRARY_OUTPUT_DIRECTORY \"${OUT_DIR}\"\n"
            "    RUNTIME_OUTPUT_DIRECTORY \"${OUT_DIR}\")\n"
            "foreach(CONFIG_NAME ${CMAKE_CONFIGURATION_TYPES})\n"
            "    string(TOUPPER ${CONFIG_NAME} CONFIG_UPPER)\n"
            "    set_target_properties(NativeScripts PROPERTIES\n"
            "        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} \"${OUT_DIR}\"\n"
            "        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} \"${OUT_DIR}\")\n"
            "endforeach()\n"
            "\n"
            "if(MSVC)\n"
            "    target_compile_options(NativeScripts PRIVATE /utf-8)\n"
            "endif()\n";

        std::string entry =
            "// Pontos de entrada do modulo - nao precisa editar.\n"
            "#include <Prism/Scripting/NativeScript.h>\n"
            "\n"
            "PRISM_NATIVE_MODULE_ENTRY()\n";

        std::string example =
            "#include <Prism/Scripting/NativeScript.h>\n"
            "\n"
            "using namespace Prism;\n"
            "\n"
            "// Exemplo: gira a entidade no eixo Y. Adicione o component \"Native Script\"\n"
            "// a uma entidade e escolha \"Spinner\".\n"
            "class Spinner : public ScriptableEntity {\n"
            "public:\n"
            "    float SpeedDegreesPerSecond = 90.0f;\n"
            "\n"
            "    void OnUpdate(float deltaTime) override {\n"
            "        GetTransform().Rotation.y += SpeedDegreesPerSecond * deltaTime;\n"
            "    }\n"
            "};\n"
            "PRISM_REGISTER_SCRIPT(Spinner)\n";

        // CMakeLists sempre regravado (o caminho do SDK muda se a engine mudar de pasta);
        // os demais so se nao existirem, para nunca apagar codigo do usuario.
        {
            std::ofstream file(directory / "CMakeLists.txt", std::ios::binary);
            file << cmake;
        }
        WriteIfMissing(directory / "PrismModuleEntry.cpp", entry);
        bool createdExample = WriteIfMissing(directory / "Spinner.cpp", example);

        message = "Projeto de scripts C++ pronto em " + directory.string() + (createdExample ? " (com o exemplo Spinner)." : ".");
        return true;
    }

}
