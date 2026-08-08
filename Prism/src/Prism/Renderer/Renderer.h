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
        //
        // Internamente descarta (no fragment shader) a face de qualquer
        // triangulo que esteja de costas para SetCameraPosition() - ver
        // comentario grande em Renderer.cpp acima de s_VertexSrc. Chame
        // SetCameraPosition() antes de qualquer DrawMesh() do frame (ou da
        // preview) para esse efeito funcionar corretamente; sem chamar,
        // usa (0,0,0) por padrao.
        static void DrawMesh(PrimitiveMesh mesh, const float* viewProjection, const float* model, const float* color = nullptr);

        // Define a posicao (world space, 3 floats xyz) da camera usada
        // pelo teste de "face interna transparente" dentro de DrawMesh() -
        // ver comentario la. Chamar uma vez por framebuffer renderizado
        // (viewport principal e preview da camera usam posicoes
        // diferentes - ver EditorLayer::RenderScene/RenderCameraPreview),
        // antes de qualquer DrawMesh() daquele framebuffer.
        static void SetCameraPosition(const float* worldPos);

        // Desenha uma lista de segmentos de linha soltos (cada par de
        // pontos consecutivos em 'points' e um segmento - GL_LINES, nao
        // GL_LINE_STRIP) em espaco de mundo, sem shading (cor solida via
        // uniform, sem luz). Usado hoje so para gizmos de edicao (ex: o
        // frustum do CameraComponent na viewport - ver
        // EditorLayer::RenderCameraGizmos) - nao participa da geometria
        // "de jogo" desenhada por DrawMesh. pointCount deve ser par.
        static void DrawLines(const float* points, uint32_t pointCount, const float* viewProjection, const float* color);

    private:
        static Ref<Shader> s_BasicShader;
        static Ref<Shader> s_LineShader;
        static float s_CameraWorldPos[3];

        // VAO/VBO dedicados ao DrawLines() - o buffer e reescrito
        // (glBufferData) a cada chamada, ja que gizmos mudam de forma
        // frame a frame (ex: FOV editado ao vivo). Volume de dados e
        // minusculo (poucas dezenas de linhas), entao nao vale a pena
        // otimizar isso agora - ver comentario em Renderer.cpp.
        static uint32_t s_LineVAO;
        static uint32_t s_LineVBO;

        // Uma malha de GPU por primitiva embutida - indexadas pelo mesmo
        // enum PrimitiveMesh usado em MeshRendererComponent, entao
        // DrawMesh() so precisa de um lookup, sem switch gigante.
        static Scope<Mesh> s_Meshes[5];
    };

}
