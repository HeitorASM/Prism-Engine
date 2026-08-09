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
#include "../Scene/Components.h" // PrimitiveMesh, LightComponent, LightType
#include "Shader.h"
#include "Mesh.h"
#include <cstdint>
#include <vector>

namespace Prism {

    // Representacao de UMA luz PRONTA PARA A GPU - a "traducao" de um
    // LightComponent (dado de editor, ver Components.h) para o formato
    // generico que o shader consome via array de uniforms.
    //
    // Esta struct existe para que o shader NUNCA precise saber sobre
    // LightType especificamente: toda luz, seja qual for seu tipo, vira
    // um GPULight com os mesmos campos - o shader so olha 'Type' (como
    // int) para decidir COMO usar Position/Direction/Cone. Isso e o que
    // torna adicionar um tipo novo (ver comentario grande sobre Area/IES
    // em LightType, Components.h) uma mudanca pequena: 1) um novo case em
    // CollectGPULights preenchendo estes mesmos campos, 2) o shader
    // interpretando um novo valor de Type. Nenhuma outra funcao muda de
    // assinatura.
    //
    // Layout pensado para bater 1:1 com o std140 do array de uniforms no
    // shader (ver s_FragmentSrc em Renderer.cpp) - todo vec3 alinhado a
    // 16 bytes, sem "buracos" inesperados.
    struct GPULight {
        int   Type = 0;          // == (int)LightType - Point=0, Spot=1, Directional=2, ...
        glm::vec3 Position = { 0.0f, 0.0f, 0.0f };   // world space - ignorado por Directional
        glm::vec3 Direction = { 0.0f, -1.0f, 0.0f }; // world space, normalizada - so Spot/Directional (e Area no futuro)
        glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;
        float Range = 10.0f;               // Point/Spot - Directional ignora
        float CosOuterAngle = -1.0f;       // cos(SpotAngle) pre-calculado - so Spot
        float CosInnerAngle = -1.0f;       // cos(InnerSpotAngle) pre-calculado - so Spot
    };

    // Numero maximo de luzes simultaneas que uma cena pode enviar ao
    // shader em um unico frame - array de tamanho fixo no lado GLSL (ver
    // "uniform GPULight u_Lights[MAX_LIGHTS]" em s_FragmentSrc), entao
    // este valor DEVE bater exatamente com a constante espelhada la.
    // 16 e generoso para o tamanho de cena que este prototipo desenha
    // hoje; se um mapa precisar de mais luzes simultaneas no mesmo
    // draw call, este e o numero a aumentar (dos dois lados - C++ e
    // shader) - nao ha custo de convercao alem do maior array copiado
    // por frame.
    static constexpr uint32_t MAX_LIGHTS = 16;

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
        // 'lights' e opcional: se omitido (ou vetor vazio), o mesh e
        // desenhado so com uma pequena luz ambiente fixa (sem nenhuma luz
        // direta) - comportamento seguro para chamadores que ainda nao
        // tem uma lista de GPULight a mao (ex: previews avulsas). Passar
        // as luzes coletadas via CollectGPULights() para iluminacao de
        // verdade - ver DrawScene(), que ja faz isso automaticamente.
        static void DrawMesh(PrimitiveMesh mesh, const float* viewProjection, const float* model, const float* color = nullptr, const std::vector<GPULight>* lights = nullptr);

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

        // Desenha TODA entidade de 'scene' que tenha TransformComponent +
        // MeshRendererComponent, usando a mesma matriz view*projection para
        // todas (nao ha culling/batching ainda - ver comentario no topo do
        // arquivo). Extraido de EditorLayer::RenderSceneEntities para
        // dentro do Renderer (fora do editor) porque tanto a viewport do
        // editor quanto a futura Play Window (janela separada do SO
        // rodando o jogo de verdade - ver README) precisam do MESMO loop
        // de desenho, sem duplicar a logica em dois lugares. Chama
        // SetCameraPosition(cameraWorldPos) internamente antes de
        // qualquer DrawMesh() (ver comentario em SetCameraPosition acima)
        // - o chamador nao precisa fazer isso separadamente.
        static void DrawScene(class Scene& scene, const float* viewProjection, const float* cameraWorldPos);

        // Varre 'scene' por toda entidade com TransformComponent +
        // LightComponent e traduz cada uma para um GPULight (ver comentario
        // na struct acima) usando a posicao/rotacao de MUNDO da entidade
        // (Scene::GetWorldTransform - respeita parenting, igual DrawScene
        // ja faz para meshes). Limita a MAX_LIGHTS resultados: luzes alem
        // desse limite sao ignoradas silenciosamente (nao ha prioridade
        // por distancia/importancia ainda - TODO se algum mapa real
        // esbarrar nesse limite).
        //
        // Publica (nao so uso interno de DrawMesh/DrawScene) porque
        // ferramentas do editor - ex: um futuro gizmo que desenha o cone
        // de um Spot, ou um contador "N luzes ativas" numa status bar -
        // podem precisar da mesma lista sem duplicar a logica de
        // traducao/coleta.
        static std::vector<GPULight> CollectGPULights(class Scene& scene);

        // Envia o array 'lights' para o shader atualmente bindado (deve
        // ser chamado depois de s_BasicShader->Bind()) como os uniforms
        // u_LightCount + u_Lights[i].*. Uso interno de DrawMesh/DrawScene,
        // mas exposta para o caso raro de um caller externo precisar
        // desenhar com s_BasicShader fora do fluxo normal de DrawMesh.
        static void UploadLights(const std::vector<GPULight>& lights);

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
