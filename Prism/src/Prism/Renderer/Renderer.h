#pragma once

// ============================================================================
// Renderer.h
// API de desenho MINIMA da engine. Sabe desenhar um unico tipo de
// primitiva (cubo unitario) com um shader simples (cor solida + luz
// direcional fake) - o suficiente para MeshRendererComponent::Mesh ==
// PrimitiveMesh::Cube (ver Components.h) aparecer na viewport.
//
// Isto NAO e o "Renderizador principal" definitivo mencionado no guia do
// prototipo (esse ainda esta em aberto - forward vs deferred, batching,
// materiais de verdade, etc). E o primeiro tijolo: uma API estatica
// parecida com a de engines tipo Hazel/Sokol. Quem decide O QUE desenhar
// (percorrer as entidades da Scene) e o EditorLayer, chamando
// DrawTestCube() uma vez por entidade com MeshRendererComponent - ver
// EditorLayer::RenderScene().
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
        // color e RGB linear (0..1) - usado como base color do shader.
        static void DrawTestCube(const float* viewProjection, const float* model, const float* color = nullptr);

    private:
        static Ref<Shader> s_BasicShader;
        static uint32_t s_CubeVAO;
        static uint32_t s_CubeVBO;
        static uint32_t s_CubeEBO;
    };

}
