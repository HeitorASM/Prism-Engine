#pragma once

// ============================================================================
// ExtensionAPI.h
// Lado da EXTENSAO (dll/so). Uma extensao e uma biblioteca compilada com o MESMO
// compilador/configuracao da engine (como os scripts C++ do projeto), que a engine
// carrega a partir de uma pasta com `extension.ini` (ver ExtensionManifest.h).
//
// A dll NAO enxerga os simbolos da engine (a engine e uma lib estatica dentro do
// executavel): tudo que ela precisa chamar passa pela tabela `ExtensionHost` que a
// engine entrega em PrismExtension_Init. Use os helpers abaixo (RegisterComponent,
// RegisterSystem, Log), nunca ComponentRegistry/SystemRegistry diretamente.
//
// Exemplo minimo:
//
//     static bool Init(const Prism::ExtensionHost& host) {
//         Prism::ExtensionComponentDesc d;  d.Name = "Health"; ...
//         Prism::RegisterComponent<Health>(host, d);
//         Prism::RegisterSystem<HealthSystem>(host);
//         return true;
//     }
//     PRISM_EXTENSION(Init)
//
// Regras: (1) compile com a mesma versao de EnTT/GLM/C++ e o mesmo runtime (CRT) da engine;
// (2) a engine nunca descarrega a dll antes de sair do processo (objetos da dll vivem em cenas);
// (3) para o inspetor e paineis, use host.Ui / host.UiEx (DragFloat, DragInt, Checkbox, InputText, Text): nao exige
//     linkar o ImGui. Quem preferir o ImGui completo precisa linka-lo na dll e chamar
//     ImGui::SetCurrentContext((ImGuiContext*)host.ImGuiContext) no Init.
// ============================================================================

#include "EngineSystem.h"
#include "ExtensionHooks.h"
#include "ExtensionUi.h"
#include "../Scene/ComponentRegistry.h"
#include "../Scene/Entity.h"

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
    #define PRISM_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
    #define PRISM_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace Prism {

    // Suba quando a tabela abaixo (ou o contrato de carga) mudar.
    //  v2: paineis/janelas e menus (RegisterPanel/RegisterMenuItem), hooks de component e de estagio
    //      (RegisterComponentHook/RegisterStageHook), host.UiEx, BindImGui.
    // A tabela ExtensionHost so CRESCE no final: uma extensao compilada para a v1 continua carregando
    // (a engine aceita de kExtensionApiMinVersion ate kExtensionApiVersion). Extensao v2 em engine v1
    // e recusada (api maior que a da engine).
    constexpr uint32_t kExtensionApiVersion = 2;
    constexpr uint32_t kExtensionApiMinVersion = 1;

    // Widgets do inspetor SEM precisar linkar o ImGui na dll (ImGui tem estado global por modulo).
    // Todos devolvem true quando o valor mudou. Use dentro de ExtensionComponentDesc::DrawInspector.
    struct ExtensionUi {
        bool (*DragFloat)(const char* label, float* value, float speed, float minValue, float maxValue) = nullptr;
        bool (*DragInt)(const char* label, int* value, float speed, int minValue, int maxValue) = nullptr;
        bool (*Checkbox)(const char* label, bool* value) = nullptr;
        bool (*InputText)(const char* label, char* buffer, size_t bufferSize) = nullptr;
        void (*Text)(const char* text) = nullptr;
    };

    // Widgets extras (v2), para paineis e inspetores sem linkar o ImGui. Mesma convencao de ExtensionUi:
    // devolvem true quando o usuario interagiu/mudou o valor. Preenchido pela engine; todos nao-nulos.
    struct ExtensionUiEx {
        bool (*Button)(const char* label) = nullptr;
        void (*SameLine)() = nullptr;
        void (*Separator)() = nullptr;
        void (*Spacing)() = nullptr;
        void (*TextWrapped)(const char* text) = nullptr;
        void (*TextDisabled)(const char* text) = nullptr;
        void (*TextColored)(float r, float g, float b, float a, const char* text) = nullptr;
        bool (*SliderFloat)(const char* label, float* value, float minValue, float maxValue) = nullptr;
        bool (*SliderInt)(const char* label, int* value, int minValue, int maxValue) = nullptr;
        bool (*InputFloat)(const char* label, float* value) = nullptr;
        bool (*InputInt)(const char* label, int* value) = nullptr;
        bool (*InputTextMultiline)(const char* label, char* buffer, size_t bufferSize, float width, float height) = nullptr;
        bool (*Combo)(const char* label, int* current, const char* itemsSeparatedByNul) = nullptr; // "A\0B\0C\0\0"
        bool (*ColorEdit4)(const char* label, float* rgba) = nullptr;
        bool (*Selectable)(const char* label, bool selected) = nullptr;
        bool (*CollapsingHeader)(const char* label) = nullptr;
        bool (*TreeNode)(const char* label) = nullptr;   // se true, chame TreePop() no fim
        void (*TreePop)() = nullptr;
        void (*BeginDisabled)(bool disabled) = nullptr;
        void (*EndDisabled)() = nullptr;
        void (*PushId)(const char* id) = nullptr;
        void (*PopId)() = nullptr;
        void (*SetTooltipOnHover)(const char* text) = nullptr; // tooltip do ULTIMO widget, so se estiver sob o mouse
        void (*ProgressBar)(float fraction, const char* overlay) = nullptr;
        void (*GetContentRegionAvail)(float* width, float* height) = nullptr;
    };

    struct ExtensionHost {
        uint32_t ApiVersion = 0;
        uint32_t StructSize = 0;                 // sizeof(ExtensionHost) do lado da engine
        const char* EngineVersion = "";
        const char* ExtensionName = "";          // 'name' do manifesto
        const char* ExtensionDirectory = "";     // pasta da extensao (onde esta o extension.ini)
        const char* ProjectDirectory = "";       // vazio se a extensao e global (nao do projeto)
        bool IsEditor = false;
        void* ImGuiContext = nullptr;            // ImGuiContext* da engine (so para quem linka o ImGui por conta propria)
        ExtensionUi Ui;                          // copie por valor para dentro dos lambdas do inspetor

        // Nao chame direto: use RegisterComponent / RegisterSystem / Log.
        bool (*RegisterComponentRaw)(ExtensionComponentDesc* desc, ExtensionComponentOps* ops) = nullptr;
        bool (*RegisterSystemRaw)(IEngineSystem* system, const char* source) = nullptr; // assume a posse de 'system'
        void (*LogRaw)(int level, const char* message) = nullptr;                       // 0 info, 1 aviso, 2 erro

        // ---- v2 (so existe a partir daqui; extensoes v1 nunca leem alem de LogRaw) ----------------
        ExtensionUiEx UiEx;
        // Nao chame direto: use RegisterPanel / RegisterMenuItem / SetPanelOpen / RegisterComponentHook / RegisterStageHook.
        bool (*RegisterPanelRaw)(ExtensionPanelDesc* desc, const char* source) = nullptr;
        bool (*RegisterMenuItemRaw)(ExtensionMenuItemDesc* desc, const char* source) = nullptr;
        bool (*SetPanelOpenRaw)(const char* source, const char* id, bool open) = nullptr;
        bool (*RegisterComponentHookRaw)(ExtensionComponentHookDesc* desc, const char* source) = nullptr;
        bool (*RegisterStageHookRaw)(ExtensionStageHookDesc* desc, const char* source) = nullptr;
        // Alocador do ImGui da engine (para BindImGui, so de quem linka o ImGui na dll).
        void* ImGuiAllocFn = nullptr;
        void* ImGuiFreeFn = nullptr;
        void* ImGuiAllocUserData = nullptr;
    };

    // --- v2: interface no editor ----------------------------------------------------------------
    // Painel: aparece em Janela > Extensoes > <nome da extensao> e (Docked) acopla como os demais.
    // No runtime (jogo exportado) nada e desenhado: o registro e aceito e ignorado.
    inline bool RegisterPanel(const ExtensionHost& host, ExtensionPanelDesc desc) {
        return host.RegisterPanelRaw && host.RegisterPanelRaw(&desc, host.ExtensionName);
    }
    inline bool RegisterMenuItem(const ExtensionHost& host, ExtensionMenuItemDesc desc) {
        return host.RegisterMenuItemRaw && host.RegisterMenuItemRaw(&desc, host.ExtensionName);
    }
    // Abre/fecha um painel desta extensao pelo codigo (ex: um item de menu que abre uma janela).
    inline bool SetPanelOpen(const ExtensionHost& host, const char* panelId, bool open) {
        return host.SetPanelOpenRaw && host.SetPanelOpenRaw(host.ExtensionName, panelId, open);
    }

    // --- v2: modificar o que a engine ja faz (ver ExtensionHooks.h) ------------------------------
    inline bool RegisterComponentHook(const ExtensionHost& host, ExtensionComponentHookDesc desc) {
        return host.RegisterComponentHookRaw && host.RegisterComponentHookRaw(&desc, host.ExtensionName);
    }
    inline bool RegisterStageHook(const ExtensionHost& host, ExtensionStageHookDesc desc) {
        return host.RegisterStageHookRaw && host.RegisterStageHookRaw(&desc, host.ExtensionName);
    }

    // Para quem linka o ImGui na propria dll: inclua <imgui.h> ANTES deste header e chame no Init.
    // Liga o contexto e o alocador da engine (sem isso o ImGui da dll tem estado proprio e crasha).
#ifdef IMGUI_VERSION
    inline void BindImGui(const ExtensionHost& host) {
        if (host.ImGuiAllocFn && host.ImGuiFreeFn)
            ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)host.ImGuiAllocFn, (ImGuiMemFreeFunc)host.ImGuiFreeFn, host.ImGuiAllocUserData);
        ImGui::SetCurrentContext((ImGuiContext*)host.ImGuiContext);
    }
#endif

    template<typename T>
    bool RegisterComponent(const ExtensionHost& host, ExtensionComponentDesc desc) {
        static_assert(std::is_default_constructible_v<T>, "O component de extensao precisa de construtor default.");
        static_assert(std::is_copy_constructible_v<T>, "O component de extensao precisa ser copiavel.");
        if (!host.RegisterComponentRaw) return false;
        if (desc.Source.empty()) desc.Source = host.ExtensionName;
        ExtensionComponentOps ops;
        ops.TypeId = (uint32_t)entt::type_hash<T>::value();
        ops.Has = [](Entity e) { return e.HasComponent<T>(); };
        ops.AddDefault = [](Entity e) { e.AddComponent<T>(); };
        ops.Remove = [](Entity e) { e.RemoveComponent<T>(); };
        ops.Copy = [](Entity source, Entity destination) { destination.AddComponent<T>(source.GetComponent<T>()); };
        return host.RegisterComponentRaw(&desc, &ops);
    }

    template<typename S, typename... Args>
    bool RegisterSystem(const ExtensionHost& host, Args&&... args) {
        if (!host.RegisterSystemRaw) return false;
        return host.RegisterSystemRaw(new S(std::forward<Args>(args)...), host.ExtensionName);
    }

    // Strings no formato do arquivo (u32 tamanho + bytes), identico a ComponentRegistry::WriteString/
    // ReadString. Use ESTES dentro da dll: WriteString/ReadString da engine nao sao visiveis a ela
    // (WriteRaw/ReadRaw sao templates inline e podem ser usados normalmente).
    inline void WriteString(std::ostream& out, const std::string& text) {
        const uint32_t length = (uint32_t)text.size();
        out.write(reinterpret_cast<const char*>(&length), sizeof(length));
        if (length > 0) out.write(text.data(), length);
    }
    inline bool ReadString(std::istream& in, std::string& text) {
        uint32_t length = 0;
        in.read(reinterpret_cast<char*>(&length), sizeof(length));
        if (!in || length > 16u * 1024u * 1024u) return false;
        text.resize(length);
        if (length > 0) in.read(text.data(), length);
        return (bool)in;
    }

    inline void LogMessage(const ExtensionHost& host, int level, const char* message) {
        if (host.LogRaw) host.LogRaw(level, message);
    }

} // namespace Prism

// Pontos de entrada exportados pela dll. A engine confere a versao ANTES de chamar o resto.
#define PRISM_EXTENSION_WITH_SHUTDOWN(InitFunction, ShutdownFunction)                                \
    PRISM_EXTENSION_EXPORT uint32_t PrismExtension_ApiVersion() { return ::Prism::kExtensionApiVersion; } \
    PRISM_EXTENSION_EXPORT bool PrismExtension_Init(const ::Prism::ExtensionHost* host) {            \
        return host && InitFunction(*host);                                                          \
    }                                                                                                \
    PRISM_EXTENSION_EXPORT void PrismExtension_Shutdown() { ShutdownFunction(); }

#define PRISM_EXTENSION(InitFunction)                                                                \
    namespace { inline void PrismExtensionNoShutdown() {} }                                          \
    PRISM_EXTENSION_WITH_SHUTDOWN(InitFunction, PrismExtensionNoShutdown)
