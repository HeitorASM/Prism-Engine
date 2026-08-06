#pragma once
#include "../Core/Base.h"

struct GLFWwindow;

namespace Prism {

    // Contexto grafico: separado da Window de proposito. A Window sabe abrir
    // uma janela do SO e receber input; o GraphicsContext sabe inicializar
    // a API grafica (OpenGL hoje, possivelmente Vulkan no futuro) sobre essa
    // janela. Nenhuma das duas classes precisa saber os detalhes da outra.
    class GraphicsContext {
    public:
        virtual ~GraphicsContext() = default;

        virtual void Init() = 0;
        virtual void SwapBuffers() = 0;

        static Scope<GraphicsContext> Create(void* window);
    };

}
