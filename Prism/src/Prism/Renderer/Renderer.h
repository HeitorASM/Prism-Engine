#pragma once

// ============================================================================
// Renderer.h
// API de desenho MINIMA da engine. Objetivo unico agora: provar que o
// caminho Framebuffer -> Shader -> geometria -> ImGui::Image funciona de
// ponta a ponta, desenhando um cubo colorido dentro da Viewport do editor.
//
// Isto NAO e o "Renderizador principal" definitivo mencionado no guia do
// prototipo (esse ainda esta em aberto - forward vs deferred, etc). E so o
// primeiro tijolo: uma API estatica parecida com a de engines tipo Hazel/
// Sokol, facil de trocar por algo mais robusto (RenderCommand + RenderQueue,
// batching, materiais) sem afetar quem so chama Renderer::Clear()/DrawTestCube().
// ============================================================================

#include "../Core/Base.h"
#include "Shader.h"
#include <cstdint>

namespace Prism {

    class Renderer {
    public:
        // Deve ser chamado uma vez, com um contexto OpenGL ja valido (ou
        // seja, depois de GraphicsContext::Init()).
        static void Init();
        static void Shutdown();

        static void Clear(float r = 0.05f, float g = 0.05f, float b = 0.07f, float a = 1.0f);
        static void SetViewport(uint32_t width, uint32_t height);

        // Desenha um cubo unitario. viewProjection e model sao matrizes 4x4
        // column-major (16 floats), no layout que glm::value_ptr produz -
        // viewProjection ja deve vir como Projection * View combinadas.
        static void DrawTestCube(const float* viewProjection, const float* model);

    private:
        static Ref<Shader> s_BasicShader;
        static uint32_t s_CubeVAO;
        static uint32_t s_CubeVBO;
        static uint32_t s_CubeEBO;
    };

}
