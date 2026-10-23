#pragma once

// ============================================================================
// Renderer.h
// API de desenho da engine (estatica). Desenha as primitivas embutidas
// definidas em PrimitiveMesh (ver Components.h: Cube, Sphere, Capsule,
// Cylinder, Plane) com um shader forward PBR (Cook-Torrance) multi-luz,
// materiais com textura, sombras direcionais e SSAO.
//
// DrawScene() e o ponto de entrada: percorre as entidades da Scene e
// executa todos os passes (sombra, pre-passe de geometria, SSAO, cor).
// Tanto o editor quanto a PlayWindow chamam so ele. DrawMesh() e a
// primitiva de baixo nivel, usada por DrawScene e por chamadores avulsos
// (ex: previews).
//
// Geometria de cada primitiva e gerada uma unica vez em Init() (ver
// PrimitiveMeshFactory) e mantida em memoria de GPU (Mesh) pelo resto da
// sessao - nao ha alocacao/geracao por frame nem por entidade.
// ============================================================================

#include "../Core/Base.h"
#include "../Scene/Components.h"
#include "../Assets/AssetID.h"
#include "Shader.h"
#include "Mesh.h"
#include "GPULight.h"
#include "ShadowSystem.h"
#include "EnvironmentIBL.h"
#include "GeometryBuffer.h"
#include "SSAO.h"
#include "Texture.h"
#include "SkyTexture.h"
#include "PostProcess.h"
#include "../Project/Project.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <utility> 
#include <string>
#include <unordered_map>
#include <filesystem>

struct GLFWwindow;

namespace Prism {


    // Opcoes de render de UMA camera, passadas a Renderer::DrawScene. O
    // Renderer NAO guarda configuracao do usuario: tudo que muda o visual
    // vem da CENA (EnvironmentComponent: ambiente e skybox) ou da CAMERA
    // (CameraComponent: fundo, exposicao, SSAO). Os padroes abaixo sao os do
    // CameraComponent - e o que a viewport livre do editor usa.
    struct CameraRenderSettings {
        CameraClearMode ClearMode = CameraClearMode::Skybox;
        float ClearColor[3] = { 0.05f, 0.05f, 0.07f }; // sRGB, so ClearMode == SolidColor
        float Exposure = 1.0f;
        bool SSAO = true;
        // Pos-processamento (ver PostProcess.h).
        bool Bloom = false;
        float BloomThreshold = 1.0f;
        float BloomIntensity = 0.15f;
        bool FXAA = true;
        float Saturation = 1.0f;
        float Contrast = 1.0f;

        // GUARDA DE LAYOUT (sempre o ULTIMO campo). Esta struct e passada por
        // referencia entre arquivos .cpp; se algum .cpp ficou compilado com uma
        // versao ANTIGA do header (build incremental que nao recompilou), o
        // DrawScene leria lixo da memoria alem do objeto (ex: saturacao 0 =
        // imagem cinza). Com a guarda, o DrawScene percebe, avisa no log e usa
        // valores neutros.
        static constexpr uint32_t kLayoutMagic = 0x50525345u;
        uint32_t LayoutMagic = kLayoutMagic;

        static CameraRenderSettings From(const CameraComponent& camera) {
            CameraRenderSettings r;
            r.ClearMode = camera.ClearMode;
            r.ClearColor[0] = camera.ClearColor.x;
            r.ClearColor[1] = camera.ClearColor.y;
            r.ClearColor[2] = camera.ClearColor.z;
            r.Exposure = camera.Exposure;
            r.SSAO = camera.SSAO;
            r.Bloom = camera.Bloom;
            r.BloomThreshold = camera.BloomThreshold;
            r.BloomIntensity = camera.BloomIntensity;
            r.FXAA = camera.FXAA;
            r.Saturation = camera.Saturation;
            r.Contrast = camera.Contrast;
            return r;
        }
    };

    class Renderer {
    public:
        // Deve ser chamado uma vez, com um contexto OpenGL ja valido (ou
        // seja, depois de GraphicsContext::Init()).
        static void Init();
        static void Shutdown();

        // Hot reload dos shaders (Prism/shaders/*): recompila os que mudaram
        // em disco; se a nova versao nao compilar, a anterior continua valendo
        // e o erro vai pro log. Consulta o disco no maximo a cada 0,5 s, entao
        // pode ser chamado todo frame (Application::Run ja faz). Precisa de um
        // contexto OpenGL atual.
        static void ReloadChangedShaders();
        // Ligado por padrao. Desligue num jogo distribuido se nao quiser
        // nenhuma consulta ao disco em tempo de execucao.
        static void SetShaderHotReload(bool enabled);

        static void Clear(float r = 0.05f, float g = 0.05f, float b = 0.07f, float a = 1.0f);
        static void SetViewport(uint32_t width, uint32_t height);

        // Desenha a primitiva 'mesh'. viewProjection e model sao matrizes
        // 4x4 column-major (16 floats, layout glm::value_ptr) -
        // viewProjection ja deve vir como Projection * View combinadas.
        // color e RGB linear (0..1); nullptr usa uma cor padrao.
        //
        // Internamente descarta (no fragment shader) a face de qualquer
        // triangulo que esteja de costas para SetCameraPosition() - ver
        // comentario grande no topo de shaders/basic.vert. Chame
        // SetCameraPosition() antes de qualquer DrawMesh() do frame (ou da
        // preview) para esse efeito funcionar corretamente; sem chamar,
        // usa (0,0,0) por padrao.
        // 'lights' e opcional: se omitido (ou vetor vazio), o mesh e
        // desenhado so com uma pequena luz ambiente fixa (sem nenhuma luz
        // direta) - comportamento seguro para chamadores que ainda nao
        // tem uma lista de GPULight a mao (ex: previews avulsas). Passar
        // as luzes coletadas via CollectGPULights() para iluminacao de
        // verdade - ver DrawScene(), que ja faz isso automaticamente.
        // 'shadows' e opcional (nullptr = sem sombra): mapas de sombra do quadro
        // (ver ShadowSystem.h). DrawScene() o preenche; chamadores avulsos
        // (previews) funcionam sem ele.
        // 'ssao' e opcional (nullptr por padrao = "sem AO"): quando
        // fornecida, o
        // termo de luz ambiente e multiplicado por (1 - oclusao) lida da
        // textura JA SUAVIZADA (BindBlurredForReading) de 'ssao' (ver
        // u_AOMap/u_HasAO em shaders/basic.frag). DrawScene()
        // preenche isto automaticamente a partir de RenderSSAOPass +
        // RenderSSAOBlurPass.
        // 'material' e opcional (nullptr = comportamento antigo exato,
        // sem nenhuma amostragem de textura, so 'color'/u_BaseColor solido)
        // - ver comentario grande em MaterialComponent (Components.h).
        // Quando fornecido, DrawMesh usa Renderer::GetOrLoadTexture (cache
        // interno por path, ver Renderer.cpp) para obter/(re)carregar cada
        // textura configurada (AlbedoPath/NormalPath/RoughnessMetallicPath)
        // e faz bind nos slots de textura 2/3/4 (0 e 1 sao reservados para
        // shadow map / AO, ver acima). 'color' continua sendo usado como
        // ANTES caso 'material' seja nullptr (MeshRendererComponent sem
        // MaterialComponent, ex: gizmos/previews) - quando 'material' E
        // fornecido, 'color' e ignorado em favor de material->AlbedoTint.
        static void DrawMesh(PrimitiveMesh mesh, const float* viewProjection, const float* model, const float* color = nullptr, const std::vector<GPULight>* lights = nullptr, const ShadowFrame* shadows = nullptr, const SSAO* ssao = nullptr, const MaterialComponent* material = nullptr);

        // Mesma funcao de DrawMesh acima, mas recebendo o Mesh (GPU) ja
        // resolvido diretamente, em vez de um PrimitiveMesh (enum) a
        // fazer lookup em s_Meshes[]. Usada para desenhar um modelo
        // IMPORTADO (ver MeshRendererComponent::ModelAsset e
        // GetOrLoadModelMesh abaixo) - DrawScene chama uma sobrecarga ou
        // outra dependendo de ModelAsset ser valido ou nao (ver
        // Renderer::ResolveMesh abaixo), mas todo o resto do pipeline
        // (shader/luzes/sombra/SSAO/material) e IDENTICO entre as duas -
        // por isso e uma sobrecarga (mesmo nome), nao uma funcao com nome
        // diferente. 'mesh' nulo e um no-op silencioso (mesmo
        // comportamento de "primitiva ainda nao carregada" da outra
        // sobrecarga).
        static void DrawMesh(Mesh* mesh, const float* viewProjection, const float* model, const float* color = nullptr, const std::vector<GPULight>* lights = nullptr, const ShadowFrame* shadows = nullptr, const SSAO* ssao = nullptr, const MaterialComponent* material = nullptr);

        // Retorna o Mesh (GPU) do modelo IMPORTADO identificado por
        // 'modelAsset' (ver MeshRendererComponent::ModelAsset,
        // Components.h), carregando e colocando em cache (por AssetID) na
        // primeira chamada - mesmo padrao de GetOrLoadTexture acima, so
        // que para geometria em vez de imagem. 'modelAsset' invalido
        // (IsValid()==false) devolve nullptr sempre, sem tocar disco nem
        // logar erro (mesmo significado de "sem modelo" usado por
        // MeshRendererComponent).
        //
        // Falha de importacao (arquivo corrompido, movido/apagado sem
        // atualizar o .meta, formato nao suportado) tambem devolve
        // nullptr, MAS fica em cache como "falhou" - chamadas
        // subsequentes com o MESMO AssetID nao tentam reimportar
        // (reler/parsear) o arquivo a cada frame so para falhar de novo;
        // ver comentario grande no .cpp sobre como o cache distingue "nao
        // tentei ainda" de "tentei e falhou" de "carregado com sucesso".
        // Recarrega automaticamente se o Model correspondente for
        // recarregado via InvalidateModelCache (ex: apos reimportar o
        // mesmo arquivo).
        //
        // 'submeshIndex' >= 0 devolve o submesh (um por material) com esse
        // indice - ver MeshRendererComponent::SubmeshIndex; -1 (ou fora do
        // intervalo) devolve a malha mesclada de todos os submeshes. A malha
        // mesclada de um modelo com varios submeshes so e construida (relendo
        // o arquivo) na primeira vez que alguem a pede, para nao gastar o
        // dobro de VRAM com modelos usados so por submesh.
        static Mesh* GetOrLoadModelMesh(AssetID modelAsset, int submeshIndex = -1);

        // Resolve QUAL Mesh (GPU) desenhar/testar picking para um
        // MeshRendererComponent: um modelo IMPORTADO (ModelAsset valido)
        // tem prioridade sobre a primitiva embutida 'Mesh' - mesma regra
        // de prioridade usada em TODO lugar que precisa saber "qual malha
        // esta entidade usa neste frame" (DrawScene, os passes de sombra/
        // pre-passe de geometria, e Scene::VisualRaycast para picking) -
        // centralizada aqui como metodo (nao funcao livre) porque precisa
        // ler s_Meshes[] (privado) para o caso de fallback. Publico (nao
        // privado) porque Scene::VisualRaycast, fora desta classe,
        // tambem precisa da MESMA resolucao para picking bater com o que
        // e desenhado - ver comentario grande em Scene.cpp.
        static Mesh* ResolveMesh(const MeshRendererComponent& meshRenderer);

        // Remove 'modelAsset' do cache acima (sucesso OU falha), forcando
        // a proxima GetOrLoadModelMesh com o mesmo ID a reler o arquivo
        // do zero. Chamada pelo editor quando o Content Browser detecta
        // que o arquivo de origem mudou no disco (ex: reexportado de um
        // DCC) - sem isto, a entidade continuaria mostrando a geometria
        // ANTIGA (em cache) ate o processo do editor ser reiniciado.
        static void InvalidateModelCache(AssetID modelAsset);

        // Retorna a Texture2D correspondente a 'path' (isSRGB conforme o
        // uso - ver comentario em Texture2D), carregando e colocando em
        // cache na primeira chamada com aquele path exato; chamadas
        // seguintes com o MESMO path retornam a mesma instancia sem
        // reler o disco. Path vazio ("") retorna nullptr sempre (mesmo
        // significado de "sem textura configurada" usado por
        // MaterialComponent - ver Components.h) sem logar erro nenhum,
        // ja que e o estado inicial normal de um Material novo.
        //
        // 'path' e RELATIVO a pasta do projeto ativo (mesma convencao de
        // MaterialComponent::AlbedoPath/SceneSerializer/ContentBrowserPanel)
        // - esta funcao resolve para absoluto internamente via
        // Project::GetActive() antes de tocar o disco; chamadores (editor
        // OU runtime) NUNCA devem resolver o path eles mesmos primeiro.
        //
        // Cache indexado por (path, isSRGB) - a MESMA imagem usada uma
        // vez como Albedo (isSRGB=true) e outra vez (hipoteticamente) como
        // mapa tecnico (isSRGB=false) precisa de DUAS Texture2D distintas
        // na GPU (formatos internos diferentes, ver Texture2D::Texture2D),
        // entao a chave do cache inclui isSRGB para nunca devolver a
        // instancia errada nesse caso raro.
        //
        // Publica (nao so uso interno de DrawMesh) para o painel de
        // Material no editor (PropertiesPanel::OnImGuiRender) poder
        // mostrar um preview/miniatura da textura carregada sem duplicar
        // o cache.
        static Texture2D* GetOrLoadTexture(const std::string& path, bool isSRGB);

        // Define a posicao (world space, 3 floats xyz) da camera usada
        // pelo teste de "face interna transparente" dentro de DrawMesh() -
        // ver comentario la. Chamar uma vez por framebuffer renderizado
        // (viewport principal e preview da camera usam posicoes
        // diferentes - ver ViewportPanel::RenderScene/RenderCameraPreview),
        // antes de qualquer DrawMesh() daquele framebuffer.
        static void SetCameraPosition(const float* worldPos);

        // (Exposicao, ambiente e skybox NAO sao mais configurados aqui: vem do
        // CameraRenderSettings e do EnvironmentComponent da cena passados a
        // DrawScene - ver o comentario dela. Valores invalidos/fora de faixa
        // sao limitados pelo Renderer, que nunca confia no dado.)

        // Desenha uma lista de segmentos de linha soltos (cada par de
        // pontos consecutivos em 'points' e um segmento - GL_LINES, nao
        // GL_LINE_STRIP) em espaco de mundo, sem shading (cor solida via
        // uniform, sem luz). Usado hoje so para gizmos de edicao (ex: o
        // frustum do CameraComponent na viewport - ver
        // EditorGizmos::RenderCameraGizmos) - nao participa da geometria
        // "de jogo" desenhada por DrawMesh. pointCount deve ser par.
        static void DrawLines(const float* points, uint32_t pointCount, const float* viewProjection, const float* color);

        // Desenha TODA entidade de 'scene' que tenha TransformComponent +
        // MeshRendererComponent, usando a mesma matriz view*projection para
        // todas (nao ha culling/batching ainda - ver comentario no topo do
        // arquivo). Extraido de EditorLayer::RenderSceneEntities para
        // dentro do Renderer (fora do editor) porque tanto a viewport do
        // editor quanto a Play Window (janela separada do SO rodando o
        // jogo) precisam do MESMO loop de desenho, sem duplicar a logica
        // em dois lugares. Chama
        // SetCameraPosition(cameraWorldPos) internamente antes de
        // qualquer DrawMesh() (ver comentario em SetCameraPosition acima)
        // - o chamador nao precisa fazer isso separadamente.
        //
        // 'view' e 'projection' SEPARADAS (nao uma unica viewProjection
        // combinada) - necessario porque RenderGeometryPrePass/RenderSSAOPass precisam de cada
        // matriz individualmente: 'view' para transformar normais para
        // view-space, 'projection' para reconstruir posicao 3D a partir
        // da profundidade (unproject). viewProjection = projection * view
        // e calculada internamente, ja que o pass de cor final ainda
        // precisa dela combinada.
        //
        // 'camera' = opcoes de render desta camera (fundo, exposicao, SSAO);
        // o ambiente (luz ambiente + skybox) vem do EnvironmentComponent da
        // cena (o primeiro encontrado; sem nenhum, valem os padroes do
        // component). Chamadores sem CameraComponent (viewport livre do
        // editor) usam o argumento padrao. NAO precisa de Clear() antes: DrawScene
        // limpa cor e profundidade do destino e o fundo cobre a tela inteira.
        static void DrawScene(class Scene& scene, const float* view, const float* projection, const float* cameraWorldPos, const CameraRenderSettings& camera = CameraRenderSettings{});

        // Desenha as particulas de todos os emissores da cena (billboards com
        // blend, sem escrever profundidade). Chamado no fim de DrawScene, depois
        // da geometria opaca; exposto para quem desenha a cena por conta propria.
        static void DrawParticles(class Scene& scene, const float* view, const float* projection);

        // UI in-game: um quad em espaco de tela (pixels, origem no canto superior
        // esquerdo). Layout igual ao struct UIQuad de ui.vert (std430).
        struct UIQuad {
            glm::vec4 Rect;   // x, y, largura, altura
            glm::vec4 UV;     // u0, v0, u1, v1
            glm::vec4 Color;
            glm::vec4 Params; // x = modo: 0 cor, 1 textura, 2 glifo de fonte
        };
        // Desenha 'count' quads por cima do que ja esta no framebuffer atual
        // (blend normal, sem profundidade). 'texture' serve aos quads de modo 1
        // (pode ser nullptr); 'fontTexture' e o id GL do atlas R8 dos de modo 2.
        // Chamado pelo UISystem; o chamador cuida do viewport.
        static void DrawUIQuads(const UIQuad* quads, uint32_t count, float screenWidth, float screenHeight,
                                const Texture2D* texture, uint32_t fontTexture);

        // Varre 'scene' por toda entidade com TransformComponent +
        // LightComponent e traduz cada uma para um GPULight (ver comentario
        // na struct acima) usando a posicao/rotacao de MUNDO da entidade
        // (Scene::GetWorldTransform - respeita parenting, igual DrawScene
        // ja faz para meshes). Limita a MAX_LIGHTS resultados: luzes alem
        // desse limite sao ignoradas silenciosamente (nao ha prioridade
        // por distancia/importancia).
        //
        // Publica (nao so uso interno de DrawMesh/DrawScene) porque
        // ferramentas do editor - ex: um futuro gizmo que desenha o cone
        // de um Spot, ou um contador "N luzes ativas" numa status bar -
        // podem precisar da mesma lista sem duplicar a logica de
        // traducao/coleta.
        static std::vector<GPULight> CollectGPULights(class Scene& scene);

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
        static void UploadLights(const std::vector<GPULight>& lights);

    private:
        static Ref<Shader> s_BasicShader;
        static Ref<Shader> s_LineShader;

        // Particulas: shader (particle.vert/.frag) + um SSBO reescrito por
        // emissor a cada frame com {posicao+tamanho, cor} de cada particula.
        // O quad e gerado no vertex shader (sem VBO), com um VAO vazio por contexto.
        static Ref<Shader> s_ParticleShader;
        static uint32_t s_ParticleSSBO;

        // UI in-game: shader (ui.vert/.frag) + SSBO reescrito a cada lote.
        static Ref<Shader> s_UIShader;
        static uint32_t s_UISSBO;

        // Estado POR QUADRO do ambiente e do fundo: reescrito por inteiro no
        // inicio de cada DrawScene (ApplyFrameState) a partir do
        // EnvironmentComponent da cena e do CameraRenderSettings. NAO e
        // configuracao do usuario - nada fora de DrawScene deve escreve-lo.
        // O panorama e carregado sob demanda e recarregado quando o caminho
        // ABSOLUTO muda (nao o AssetID: trocar de projeto com o mesmo ID nao
        // reaproveita a textura do projeto anterior).
        static void ApplyFrameState(class Scene& scene, const CameraRenderSettings& camera);
        static SkyTexture* ResolvePanorama(); // carrega sob demanda o panorama de s_SkyTexturePath (nullptr se invalido)
        static bool s_IBLActive;              // este quadro usa os mapas de EnvironmentIBL
        static void DrawSky(const float* view, const float* projection, const float* cameraWorldPos);
        static Ref<Shader> s_SkyShader;
        static SkyMode s_SkyMode;
        static float s_SkyColor[3];
        static std::string s_SkyTexturePath;  // caminho ABSOLUTO resolvido ("" = sem textura)
        static float s_SkyRotation;
        static float s_SkyIntensity;
        static Scope<SkyTexture> s_SkyTexture;
        static std::string s_SkyLoadedPath;   // caminho de s_SkyTexture ("" = nenhum tentado)


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
        // shaders/fullscreen_quad.vert), tecnica classica do
        // "big triangle" que cobre a tela toda sem precisar upload nenhum
        // de dados de vertice. OpenGL Core Profile exige um VAO bindado
        // para qualquer glDrawArrays, mesmo sem nenhum atributo habilitado
        // nele - por isso ainda precisamos de UM VAO, so que vazio.
        //
        // ATENCAO: VAOs (Vertex Array Objects) sao objetos de "container"
        // no OpenGL e, ao contrario de texturas/buffers/shaders, NAO SAO
        // COMPARTILHADOS entre contextos, mesmo com share list
        // (glfwCreateWindow(..., sharedContextWindow) - ver
        // PlayWindow::Open). Um VAO unico criado no contexto do editor e
        // invalido/vazio na Play Window e produz lixo visual (listras,
        // ruido, cores como magenta). GetFullscreenQuadVAOForCurrentContext()
        // aplica a mesma tecnica de Mesh::BindForCurrentContext: um VAO por
        // GLFWwindow*, criado sob demanda e cacheado.
        static uint32_t GetFullscreenQuadVAOForCurrentContext();
        static std::vector<std::pair<::GLFWwindow*, uint32_t>> s_FullscreenQuadVAOsByContext;

        static float s_CameraWorldPos[3];
        static float s_Exposure;
        // true so DENTRO de DrawScene com pos-processamento: os shaders de cena
        // (basic/sky/particle) escrevem HDR linear (u_OutputMode = 1) em vez da
        // imagem final. Fora disso (DrawMesh avulso) tudo sai como sempre.
        static bool s_OutputHDR;
        // O ceu do quadro precisa de exposicao/ACES (panorama HDR)? Ver DrawSky.
        static bool s_SkyNeedsToneMap;
        static float s_Ambient;
        static float s_EnvZenith[3];
        static float s_EnvHorizon[3];
        static float s_EnvGround[3];

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

        // Cache de texturas carregadas, indexado por (path, isSRGB) - ver
        // comentario grande em GetOrLoadTexture() acima. Mora aqui (nao
        // dentro de MaterialComponent/Scene) porque duas entidades
        // DIFERENTES (ou dois materiais na mesma cena) apontando para o
        // MESMO arquivo de imagem devem compartilhar UMA unica Texture2D
        // de GPU, nao recarregar/duplicar a imagem por entidade - o
        // dedup e responsabilidade de quem CONSTROI Texture2D (ver
        // comentario em Texture.h), que e exatamente esta funcao.
        // Nunca invalidado/limpo hoje (mesma politica simples de
        // s_ShadowMap/s_Meshes - vive pelo tempo de vida do processo);
        // um projeto trocando de cena repetidamente pode acumular
        // texturas de cenas antigas em memoria - aceitavel para o
        // escopo atual, candidato a um Shutdown()/invalidação futura se
        // isso virar problema real de VRAM.
        static std::unordered_map<std::string, Scope<Texture2D>> s_TextureCache;

        // Cache de malhas de MODELOS IMPORTADOS, indexado por AssetID (ver
        // GetOrLoadModelMesh acima). ModelCacheEntry::Failed (entrada presente) e o marcador de "ja tentei importar este
        // AssetID e FALHOU" - distinto de "AssetID nunca visto" (entrada
        // ausente do map), que dispara uma nova tentativa de
        // ModelLoader::Load. Sem essa distincao, uma entidade apontando
        // para um arquivo quebrado tentaria reimportar (reler o arquivo
        // inteiro do disco) TODO FRAME, ja que DrawScene chama
        // GetOrLoadModelMesh a cada chamada.
        struct ModelCacheEntry {
            bool Failed = false;                    // importacao falhou - nao tentar de novo
            std::filesystem::path AbsolutePath;     // para reler o arquivo ao construir a malha mesclada sob demanda
            std::vector<Scope<Mesh>> Submeshes;     // um por material
            Scope<Mesh> Merged;                     // so para modelos com > 1 submesh, e so depois de pedida
            bool MergedTried = false;
        };
        static std::unordered_map<AssetID, ModelCacheEntry> s_ModelMeshCache;
    };

}
