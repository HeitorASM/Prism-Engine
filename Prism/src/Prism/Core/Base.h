#pragma once

// ============================================================================
// Base.h
// Macros e definicoes fundamentais usadas em toda a engine.
// ============================================================================

#include <memory>

namespace Prism {

    // Ponteiros inteligentes com nomes curtos, no estilo usado por Hazel/Godot.
    // Usamos isso em vez de ponteiros crus em qualquer lugar que possua um objeto.
    template<typename T>
    using Scope = std::unique_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Scope<T> CreateScope(Args&& ... args) {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    using Ref = std::shared_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Ref<T> CreateRef(Args&& ... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

}

// Bit helper para flags (usado em eventos, categorias, etc.)
#define PRISM_BIT(x) (1 << x)

// Bind de metodo de instancia para uso em std::function (callbacks de eventos).
#define PRISM_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

#ifdef _MSC_VER
    #define PRISM_DEBUGBREAK() __debugbreak()
#else
    #define PRISM_DEBUGBREAK()
#endif

#ifdef PRISM_DEBUG
    #define PRISM_ASSERT(x, ...) { if(!(x)) { PRISM_DEBUGBREAK(); } }
#else
    #define PRISM_ASSERT(x, ...)
#endif
