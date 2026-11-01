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
// (3) para o inspetor, use host.Ui (DragFloat, DragInt, Checkbox, InputText, Text): nao exige
//     linkar o ImGui. Quem preferir o ImGui completo precisa linka-lo na dll e chamar
//     ImGui::SetCurrentContext((ImGuiContext*)host.ImGuiContext) no Init.
// ============================================================================

#include "EngineSystem.h"
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

    // Suba quando a tabela abaixo (ou o contrato de carga) mudar de forma incompativel.
    constexpr uint32_t kExtensionApiVersion = 1;

    // Widgets do inspetor SEM precisar linkar o ImGui na dll (ImGui tem estado global por modulo).
    // Todos devolvem true quando o valor mudou. Use dentro de ExtensionComponentDesc::DrawInspector.
    struct ExtensionUi {
        bool (*DragFloat)(const char* label, float* value, float speed, float minValue, float maxValue) = nullptr;
        bool (*DragInt)(const char* label, int* value, float speed, int minValue, int maxValue) = nullptr;
        bool (*Checkbox)(const char* label, bool* value) = nullptr;
        bool (*InputText)(const char* label, char* buffer, size_t bufferSize) = nullptr;
        void (*Text)(const char* text) = nullptr;
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
    };

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
