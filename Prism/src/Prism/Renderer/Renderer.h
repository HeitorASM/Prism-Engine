#pragma once

// ============================================================================
// Renderer.h
// API de desenho MINIMA da engine. Sabe desenhar as primitivas embutidas
// definidas em PrimitiveMesh (ver Components.h: Cube, Sphere, Capsule,
// Cylinder, Plane) com um shader simples (cor solida + luz direcional
// fake) - o suficiente para qualquer MeshRendererComponent aparecer na
// viewport.
//
// Isto NAO e o "Renderizador principal" definitivo mencionado no guia do
// prototipo (esse ainda esta em aberto - forward vs deferred, batching,
// materiais de verdade, etc). E o primeiro tijolo: uma API estatica
// parecida com a de engines tipo Hazel/Sokol. Quem decide O QUE desenhar
// (percorrer as entidades da Scene) e o EditorLayer, chamando DrawMesh()
// uma vez por entidade com MeshRendererComponent - ver
// EditorLayer::RenderScene().
//
// Geometria de cada primitiva e gerada uma unica vez em Init() (ver
// PrimitiveMeshFactory) e mantida em memoria de GPU (Mesh) pelo resto da
// sessao - nao ha alocacao/geracao por frame nem por entidade.
// ============================================================================

#include "../Core/Base.h"
#include "../Scene/Components.h" // PrimitiveMesh
#include "Shader.h"
#include "Mesh.h"
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

        // Desenha a primitiva 'mesh'. viewProjection e model sao matrizes
        // 4x4 column-major (16 floats, layout glm::value_ptr) -
        // viewProjection ja deve vir como Projection * View combinadas.
        // color e RGB linear (0..1); nullptr usa uma cor padrao.
        static void DrawMesh(PrimitiveMesh mesh, const float* viewProjection, const float* model, const float* color = nullptr);

    private:
        static Ref<Shader> s_BasicShader;

        // Uma malha de GPU por primitiva embutida - indexadas pelo mesmo
        // enum PrimitiveMesh usado em MeshRendererComponent, entao
        // DrawMesh() so precisa de um lookup, sem switch gigante.
        static Scope<Mesh> s_Meshes[5];
    };

}
