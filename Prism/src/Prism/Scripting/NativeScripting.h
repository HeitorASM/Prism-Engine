#pragma once

// ============================================================================
// NativeScripting.h
// Lado da ENGINE dos scripts em C++ (o lado do modulo/usuario e NativeScript.h):
//  - carrega/descarrega o modulo do projeto (DLL/.so com os scripts);
//  - cria, atualiza e destroi as instancias de ScriptableEntity durante o Play;
//  - gera os arquivos do projeto CMake dos scripts (menu "Scripts C++").
// ============================================================================

#include "NativeScript.h"
#include "../Scene/Entity.h"
#include "../Project/Project.h"
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Prism {

    class Scene;

    class NativeScripting {
    public:
        // Chamados uma vez, junto com ScriptEngine::Init/Shutdown.
        static void Init();
        static void Shutdown();

        // --- Modulo -----------------------------------------------------------
        // Carrega <Projeto>/Binaries/NativeScripts.(dll|so|dylib) se existir.
        // Recarrega se ja havia um carregado. Recusado durante instancias vivas.
        // O arquivo e COPIADO para Cache/NativeModule antes de carregar, para o
        // compilador poder sobrescrever o original sem o SO travar o arquivo.
        static bool LoadProjectModule(const Project& project);
        static void UnloadModule();
        static bool IsModuleLoaded() { return s_Module.Handle != nullptr; }
        static const std::vector<std::string>& GetClassNames() { return s_ClassNames; }
        static bool HasClass(const std::string& name);

        // Variaveis publicas declaradas pela classe em OnProperties, com os valores
        // PADRAO (os iniciais dos membros). Cria um objeto temporario sem entidade.
        // Em cache ate o modulo ser recarregado. Vazio se a classe nao existe/nao declara.
        static const std::vector<ScriptPropertyValue>& ReadClassProperties(const std::string& className);

        // Hot reload do modulo DURANTE o Play: se o .dll/.so do projeto mudou (o botao
        // Compilar do editor, ou um build por fora), troca o codigo sem parar o jogo.
        // Os scripts C++ em execucao sao destruidos (OnDestroy) e recriados com o codigo
        // novo (OnProperties + OnCreate) - o estado do MUNDO (entidades, fisica) fica; o
        // estado dos MEMBROS dos scripts recomeca. Chamado por Scene::OnUpdate (~2x/s).
        // Se o modulo novo for invalido, o antigo segue rodando.
        static void PollHotReload(Scene& scene);
        static void SetHotReloadEnabled(bool enabled) { s_HotReload = enabled; }
        // Jogo distribuido (PrismRuntime): abre Binaries/NativeScripts.* direto, sem copiar para
        // Cache/NativeModule. A copia existe so para o compilador do editor poder sobrescrever o
        // arquivo com o jogo aberto; num jogo instalado a pasta pode ser somente leitura
        // (ex: Program Files) e nada recompila o modulo. Desligado por padrao (editor).
        static void SetDirectLoad(bool enabled) { s_DirectLoad = enabled; }

        static std::filesystem::path GetModulePath(const Project& project);
        static std::filesystem::path GetSourceDirectory(const Project& project) { return project.GetProjectDirectory() / "Source"; }

        // PrismSDK.cmake (gerado no build da engine, copiado ao lado do
        // executavel do editor): caminhos de include da engine, GLM e EnTT.
        static std::filesystem::path GetSdkFile();

        // Cria <Projeto>/Source com CMakeLists.txt, PrismModuleEntry.cpp e um
        // script de exemplo, sem sobrescrever o que ja existe. false se faltar
        // o PrismSDK.cmake. 'message' recebe o motivo/resultado.
        static bool GenerateProjectFiles(const Project& project, std::string& message);

        // --- Instancias (chamado por Scene durante o Play) -----------------------
        static bool LoadScript(Entity entity);
        static void UpdateScript(Entity entity, float deltaTime);
        static void FixedUpdateScript(Entity entity, float fixedDeltaTime);
        static void UnloadScript(Entity entity);
        static void UnloadAll();
        static void DispatchContactEvent(Entity self, Entity other, bool enter, bool trigger);
        // Chama ScriptableEntity::OnClick da entidade (clique em botao de UI).
        static void DispatchUIClick(Entity self);
        // Chama ScriptableEntity::OnAnimationEvent(nome) da entidade.
        static void DispatchAnimationEvent(Entity self, const std::string& name);
        static bool HasInstances() { return !s_Instances.empty(); }

    private:
        struct ModuleState {
            void* Handle = nullptr;
            std::filesystem::path LoadedCopy;
        };
        struct ClassInfo {
            NativeModule::CreateFn Create = nullptr;
            NativeModule::DestroyFn Destroy = nullptr;
        };
        struct Instance {
            ScriptableEntity* Object = nullptr;
            NativeModule::DestroyFn Destroy = nullptr;
            bool Faulted = false; // excecao em algum callback: para de chamar (evita spam)
        };
        struct InstanceKey {
            const void* SceneKey;
            uint32_t EntityHandle;
            bool operator==(const InstanceKey& o) const { return SceneKey == o.SceneKey && EntityHandle == o.EntityHandle; }
        };
        struct InstanceKeyHash {
            size_t operator()(const InstanceKey& k) const {
                return std::hash<const void*>()(k.SceneKey) ^ (std::hash<uint32_t>()(k.EntityHandle) << 1);
            }
        };

        // Modulo ja aberto e validado, ainda NAO instalado (ver OpenModule/InstallModule).
        struct LoadedModule {
            void* Handle = nullptr;
            std::filesystem::path LoadedCopy;
            std::filesystem::file_time_type SourceWriteTime{};
            std::vector<std::pair<std::string, ClassInfo>> Classes;
        };
        static bool OpenModule(const Project& project, LoadedModule& out);
        static void InstallModule(LoadedModule& module);
        static void ApplyProperties(ScriptableEntity& object, const NativeScriptComponent& component);

        static ModuleState s_Module;
        static std::unordered_map<std::string, std::vector<ScriptPropertyValue>> s_PropertyCache;
        static std::filesystem::file_time_type s_LoadedWriteTime;
        static std::filesystem::file_time_type s_PendingWriteTime;
        static std::chrono::steady_clock::time_point s_LastHotReloadPoll;
        static bool s_HotReload;
        static bool s_DirectLoad;
        static std::vector<std::string> s_ClassNames;
        static std::unordered_map<std::string, ClassInfo> s_Classes;
        static std::unordered_map<InstanceKey, Instance, InstanceKeyHash> s_Instances;
        static unsigned s_LoadCounter;
    };

}
