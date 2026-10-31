#pragma once

// ============================================================================
// Base.h
// Macros e definicoes fundamentais usadas em toda a engine.
// ============================================================================

#include <memory>
#include <sstream>
#include <tuple>
#include <utility>
#include <cstdlib>
#include "Log.h" // header-only e sem dependencias: nao cria ciclo com Base.h

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

// Interrompe a execucao NO PONTO do erro. Antes, so o MSVC tinha implementacao:
// em GCC/Clang a macro era vazia e o PRISM_ASSERT nao fazia absolutamente nada,
// nem em Debug.
#if defined(_MSC_VER)
    #define PRISM_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define PRISM_DEBUGBREAK() __builtin_trap()
#else
    #define PRISM_DEBUGBREAK() std::abort()
#endif

namespace Prism::Detail {

    // Loga a expressao, arquivo:linha e a mensagem opcional do PRISM_ASSERT.
    // A mensagem chega como tupla de referencias (std::forward_as_tuple) para que
    // PRISM_ASSERT(cond) sem mensagem e PRISM_ASSERT(cond, "a", x, "b") funcionem
    // sem depender de ##__VA_ARGS__ (extensao nao padrao).
    template<typename Tuple>
    inline void ReportAssertFailure(const char* expression, const char* file, int line, Tuple&& message) {
        std::ostringstream text;
        std::apply([&text](auto&&... part) { ((text << part), ...); }, std::forward<Tuple>(message));
        PRISM_CORE_ERROR("ASSERT FALHOU: ", expression, " em ", file, ":", line, text.str().empty() ? "" : " - ", text.str());
    }

}

// So em builds com PRISM_DEBUG (ver CMakeLists.txt raiz); em Release nao avalia nada.
#ifdef PRISM_DEBUG
    #define PRISM_ASSERT(x, ...) \
        do { \
            if (!(x)) { \
                ::Prism::Detail::ReportAssertFailure(#x, __FILE__, __LINE__, std::forward_as_tuple(__VA_ARGS__)); \
                PRISM_DEBUGBREAK(); \
            } \
        } while (0)
#else
    #define PRISM_ASSERT(x, ...) ((void)0)
#endif
