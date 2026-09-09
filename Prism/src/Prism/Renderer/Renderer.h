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
#include "../Scene/Components.h"
#include "Shader.h"
#include "Mesh.h"
#include "ShadowMap.h"
#include "GeometryBuffer.h"
#include "SSAO.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <utility> 

struct GLFWwindow;

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

        // Identidade da entidade EnTT dona deste LightComponent (ver
        // Renderer::CollectGPULights) - existe SO para permitir achar de
        // volta, dentro de 'lights', qual elemento corresponde a qual
        // entidade (ver Renderer::DrawScene, que usa isto para casar o
        // resultado de RenderShadowPass - que sabe a entidade shadow
        // caster, nao so o Type - com o indice certo em u_Lights[]). Sem
        // isto, uma cena com DUAS luzes Directional (so uma delas com
        // CastShadows=true) aplicaria a sombra na luz errada se a
        // primeira Directional da lista nao fosse a shadow caster.
        uint32_t SourceEntityId = 0;
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
        // 'shadowMap'/'lightSpaceMatrix' sao opcionais (ambos nullptr por
        // padrao = "sem sombra", comportamento identico a antes desta
        // feature existir): quando fornecidos, o fragment shader amostra
        // 'shadowMap' para decidir se cada fragmento esta na sombra da luz
        // Directional que gerou 'lightSpaceMatrix' (ver CalculateShadow em
        // s_FragmentSrc, Renderer.cpp). DrawScene() preenche os dois
        // automaticamente a partir de RenderShadowPass(); chamadores
        // manuais de DrawMesh (ex: previews avulsas) continuam
        // funcionando sem eles, so sem sombra na preview.
        // 'shadowCasterLightIndex' identifica, dentro de 'lights', qual
        // luz corresponde a 'shadowMap' (ver comentario em UploadLights) -
        // ignorado se 'shadowMap' for nullptr.
        // 'ssao' e opcional (nullptr por padrao = "sem AO", identico ao
        // comportamento antes desta feature existir): quando fornecida, o
        // termo de luz ambiente e multiplicado por (1 - oclusao) lida da
        // textura JA SUAVIZADA (BindBlurredForReading) de 'ssao' (ver
        // u_AOMap/u_HasAO em s_FragmentSrc, Renderer.cpp). DrawScene()
        // preenche isto automaticamente a partir de RenderSSAOPass +
        // RenderSSAOBlurPass.
        static void DrawMesh(PrimitiveMesh mesh, const float* viewProjection, const float* model, const float* color = nullptr, const std::vector<GPULight>* lights = nullptr, const ShadowMap* shadowMap = nullptr, int shadowCasterLightIndex = -1, const SSAO* ssao = nullptr);

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
        //
        // 'view' e 'projection' SEPARADAS (nao uma unica viewProjection
        // combinada, como antes desta funcao ganhar SSAO) - necessario
        // porque RenderGeometryPrePass/RenderSSAOPass precisam de cada
        // matriz individualmente: 'view' para transformar normais para
        // view-space, 'projection' para reconstruir posicao 3D a partir
        // da profundidade (unproject). viewProjection = projection * view
        // e calculada internamente, ja que o pass de cor final ainda
        // precisa dela combinada.
        static void DrawScene(class Scene& scene, const float* view, const float* projection, const float* cameraWorldPos);

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

        // --- Shadow mapping (directional, uma luz por vez) --------------
        // Ver comentario grande em Renderer.cpp acima de RenderShadowPass
        // para o pipeline completo. Chamado automaticamente de dentro de
        // DrawScene() - nenhum chamador (EditorLayer, PlayWindow) precisa
        // saber que este pass existe ou chama-lo separadamente.
        //
        // Publica (nao so uso interno) pelo mesmo motivo de
        // CollectGPULights: uma ferramenta de editor (ex: um futuro painel
        // de debug que mostra o shadow map como imagem, tipo o "shadow map
        // viewer" de engines maiores) pode precisar do resultado sem
        // duplicar a logica de "qual luz projeta sombra e qual matriz ela
        // usa".
        //
        // Retorna nullptr se nenhuma luz da cena tem
        // LightComponent::CastShadows == true (comportamento identico a
        // antes desta feature existir: sem shadow map, sem sombra
        // nenhuma).
        static ShadowMap* RenderShadowPass(class Scene& scene);

        // --- SSAO (Screen-Space Ambient Occlusion) -----------------------
        // Ver comentario grande em SSAO.h para o pipeline completo (3
        // sub-passes: geometria -> SSAO bruto -> blur). Todos chamados
        // automaticamente de dentro de DrawScene() na ordem certa -
        // nenhum chamador externo precisa orquestrar isso manualmente.
        //
        // 'view'/'projection' SEPARADAS (nao combinadas) pelo mesmo motivo
        // documentado em DrawScene() acima.
        //
        // Publicas pelo mesmo motivo de RenderShadowPass/CollectGPULights:
        // permitir que ferramentas de editor (ex: um futuro "AO buffer
        // viewer" de debug) reusem o resultado sem duplicar logica.
        static GeometryBuffer* RenderGeometryPrePass(class Scene& scene, const float* view, const float* projection, uint32_t width, uint32_t height);
        static SSAO* RenderSSAOPass(GeometryBuffer& gBuffer, const float* projection, uint32_t width, uint32_t height);
        static void RenderSSAOBlurPass(SSAO& ssao);

        // Envia o array 'lights' para o shader atualmente bindado (deve
        // ser chamado depois de s_BasicShader->Bind()) como os uniforms
        // u_LightCount + u_Lights[i].*. Uso interno de DrawMesh/DrawScene,
        // mas exposta para o caso raro de um caller externo precisar
        // desenhar com s_BasicShader fora do fluxo normal de DrawMesh.
        // 'shadowCasterLightIndex' e o indice dentro de 'lights' da UNICA
        // luz (identificada por entidade, ver GPULight::SourceEntityId e
        // Renderer::DrawScene) cuja sombra foi desenhada em
        // RenderShadowPass (-1 = nenhuma, valor padrao) - repassado ao
        // shader para CalculateLight() saber em qual luz aplicar
        // CalculateShadow().
        static void UploadLights(const std::vector<GPULight>& lights, int shadowCasterLightIndex = -1);

    private:
        static Ref<Shader> s_BasicShader;
        static Ref<Shader> s_LineShader;

        // Shader "depth-only" usado exclusivamente por RenderShadowPass -
        // so precisa de posicao (nem normal, nem cor) e nao tem fragment
        // shader com logica nenhuma (so existe para a GPU ter algo para
        // linkar - ver s_ShadowDepthFragmentSrc em Renderer.cpp). Programa
        // SEPARADO de s_BasicShader, nao um "modo" dele, porque os dois
        // tem conjuntos de uniforms/atributos completamente diferentes.
        static Ref<Shader> s_ShadowDepthShader;

        // Um unico ShadowMap, reusado a cada frame (recriado so se a
        // engine ganhar shadow maps por-luz/CSM no futuro - ver comentario
        // em ShadowMap.h). Alocado sob demanda na primeira vez que
        // RenderShadowPass encontra uma luz com CastShadows=true (nao em
        // Init()), para nao gastar os ~16MB de VRAM em cenas que nunca
        // usam sombra.
        static Scope<ShadowMap> s_ShadowMap;

        // Identidade EnTT (ver GPULight::SourceEntityId) da entidade cuja
        // luz foi desenhada em s_ShadowMap neste frame - preenchido por
        // RenderShadowPass, lido por DrawScene logo em seguida para achar
        // o indice correto dentro do 'lights' ja coletado (ver comentario
        // em GPULight::SourceEntityId sobre o bug que isto evita).
        // s_HasShadowCasterEntity distingue "nenhuma luz encontrada" de
        // "encontrada, id == 0" sem depender do valor numerico exato de
        // entt::null (que nao e garantido ser 0 em toda versao do EnTT).
        static uint32_t s_ShadowCasterEntityId;
        static bool s_HasShadowCasterEntity;

        // --- SSAO: shaders e recursos, mesmo padrao de s_ShadowDepthShader/
        // s_ShadowMap acima -----------------------------------------------

        // Depth+normal-only, usado por RenderGeometryPrePass - variante do
        // s_ShadowDepthShader mas com uma segunda saida (normal em
        // view-space) e usando a camera PRINCIPAL, nao a da luz.
        static Ref<Shader> s_GeometryPrePassShader;

        // Fullscreen-quad, calcula oclusao a partir do GeometryBuffer.
        static Ref<Shader> s_SSAOShader;

        // Fullscreen-quad, box blur sobre o resultado bruto de s_SSAOShader.
        static Ref<Shader> s_SSAOBlurShader;

        // Alocados sob demanda (mesma logica de s_ShadowMap) - so na
        // primeira vez que alguma cena/camera realmente usa SSAO. Ao
        // contrario de s_ShadowMap (resolucao fixa), os dois acompanham a
        // resolucao da viewport sendo desenhada a cada chamada de
        // DrawScene (ver GeometryBuffer::Resize/SSAO::Resize).
        static Scope<GeometryBuffer> s_GeometryBuffer;
        static Scope<SSAO> s_SSAO;

        // VAO vazio (sem VBO/atributos) usado pelo fullscreen quad de
        // s_SSAOShader/s_SSAOBlurShader - os 3 vertices sao gerados
        // inteiramente dentro do vertex shader via gl_VertexID (ver
        // s_FullscreenQuadVertexSrc, Renderer.cpp), tecnica classica do
        // "big triangle" que cobre a tela toda sem precisar upload nenhum
        // de dados de vertice. OpenGL Core Profile exige um VAO bindado
        // para qualquer glDrawArrays, mesmo sem nenhum atributo habilitado
        // nele - por isso ainda precisamos de UM VAO, so que vazio.
        //
        // BUG HISTORICO consertado aqui: uma versao anterior desta feature
        // usava um UNICO VAO estatico (criado uma vez, no contexto do
        // editor, dentro de Init()) para TODOS os contextos GLFW - isso
        // produzia lixo visual (padrao de listras/ruido, cores como
        // magenta) quando SSAO rodava dentro da Play Window (PlayWindow.cpp),
        // que usa um GLFWwindow/contexto SEPARADO do editor. VAOs (Vertex
        // Array Objects) sao objetos de "container" no OpenGL e, ao
        // contrario de texturas/buffers/shaders, NAO SAO COMPARTILHADOS
        // entre contextos mesmo quando os contextos compartilham a share
        // list (glfwCreateWindow(..., sharedContextWindow) - ver
        // PlayWindow::Open) - um VAO valido num contexto e invalido/vazio
        // em outro. Mesh ja resolvia exatamente este problema (ver
        // Mesh::BindForCurrentContext, Mesh.cpp/.h) muito antes do SSAO
        // existir; GetFullscreenQuadVAOForCurrentContext() abaixo aplica a
        // mesma tecnica (um VAO por GLFWwindow*, criado sob demanda e
        // cacheado) para o VAO do fullscreen quad.
        static uint32_t GetFullscreenQuadVAOForCurrentContext();
        static std::vector<std::pair<::GLFWwindow*, uint32_t>> s_FullscreenQuadVAOsByContext;

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
