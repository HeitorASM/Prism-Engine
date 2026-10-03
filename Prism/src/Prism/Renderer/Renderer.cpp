#include <glad/gl.h>
#include <GLFW/glfw3.h> 
#include "Renderer.h"
#include "PrimitiveMeshFactory.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Assets/ModelLoader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp> 
#include <algorithm> 
#include <cmath>
#include <string>    
#include <limits>   
#include <filesystem>
#include <vector>

namespace Prism {

    Ref<Shader> Renderer::s_BasicShader = nullptr;
    Ref<Shader> Renderer::s_LineShader = nullptr;
    Ref<Shader> Renderer::s_ShadowDepthShader = nullptr;
    Scope<ShadowMap> Renderer::s_ShadowMap = nullptr;
    uint32_t Renderer::s_ShadowCasterEntityId = 0;
    bool Renderer::s_HasShadowCasterEntity = false;
    Ref<Shader> Renderer::s_GeometryPrePassShader = nullptr;
    Ref<Shader> Renderer::s_SSAOShader = nullptr;
    Ref<Shader> Renderer::s_SSAOBlurShader = nullptr;
    Scope<GeometryBuffer> Renderer::s_GeometryBuffer = nullptr;
    Scope<SSAO> Renderer::s_SSAO = nullptr;
    std::vector<std::pair<::GLFWwindow*, uint32_t>> Renderer::s_FullscreenQuadVAOsByContext;
    Scope<Mesh> Renderer::s_Meshes[5] = {};
    uint32_t Renderer::s_LineVAO = 0;
    uint32_t Renderer::s_LineVBO = 0;
    float Renderer::s_CameraWorldPos[3] = { 0.0f, 0.0f, 0.0f };
    // Valores padrao: vem de RenderSettings (Project.h), a fonte UNICA da
    // verdade - o "Restaurar padrao" do editor e este arranque usam os mesmos
    // numeros, entao nunca divergem. As cores sao sRGB (como o color picker).
    float Renderer::s_Exposure = RenderSettings{}.Exposure;
    float Renderer::s_Ambient = RenderSettings{}.Ambient;
    float Renderer::s_EnvZenith[3]  = { RenderSettings{}.EnvZenith[0],  RenderSettings{}.EnvZenith[1],  RenderSettings{}.EnvZenith[2] };
    float Renderer::s_EnvHorizon[3] = { RenderSettings{}.EnvHorizon[0], RenderSettings{}.EnvHorizon[1], RenderSettings{}.EnvHorizon[2] };
    float Renderer::s_EnvGround[3]  = { RenderSettings{}.EnvGround[0],  RenderSettings{}.EnvGround[1],  RenderSettings{}.EnvGround[2] };
    std::unordered_map<std::string, Scope<Texture2D>> Renderer::s_TextureCache;
    std::unordered_map<AssetID, Scope<Mesh>> Renderer::s_ModelMeshCache;

    // ========================================================================
    // Shaders: ficam em arquivos (Prism/shaders/*.vert|*.frag), nao mais em
    // strings neste arquivo. Ver Prism/shaders/README.md (mapa de arquivos,
    // '#include', hot reload) e Shader.h/ShaderSource.h.
    // ========================================================================

    // Procura a pasta de shaders, em ordem:
    //   1. PRISM_SHADER_SOURCE_DIR - pasta do CODIGO-FONTE (definida pelo
    //      CMake): em desenvolvimento o hot reload observa os arquivos que
    //      voce realmente edita, nao uma copia.
    //   2. <pasta do executavel>/shaders - copiada no build; vale num jogo
    //      distribuido (a pasta do codigo-fonte nao existe la).
    //   3. <diretorio de trabalho>/shaders - ultimo recurso.
    static std::filesystem::path FindShaderDirectory() {
        std::vector<std::filesystem::path> candidates;
#ifdef PRISM_SHADER_SOURCE_DIR
        candidates.emplace_back(PRISM_SHADER_SOURCE_DIR);
#endif
        std::filesystem::path exeDir = GetExecutableDirectory();
        if (!exeDir.empty())
            candidates.push_back(exeDir / "shaders");
        std::error_code cwdError;
        std::filesystem::path cwd = std::filesystem::current_path(cwdError);
        if (!cwdError)
            candidates.push_back(cwd / "shaders");

        for (const auto& dir : candidates) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(dir / "basic.vert", ec))
                return dir;
        }

        std::string tried;
        for (const auto& dir : candidates)
            tried += "\n  " + dir.generic_string();
        PRISM_CORE_ERROR("Pasta de shaders nao encontrada (procurado:", tried, "\n). A cena nao sera desenhada - confira se a pasta 'shaders' foi copiada junto do executavel.");
        return {};
    }

    // Hot reload: ligado por padrao (~1 consulta de data de arquivo por
    // shader a cada kShaderPollInterval segundos). Ver ReloadChangedShaders.
    static bool s_ShaderHotReload = true;
    static constexpr double kShaderPollInterval = 0.5;
    static double s_LastShaderPoll = 0.0;

    // Indice dentro de s_Meshes - deve bater com a ordem numerica do enum
    // PrimitiveMesh em Components.h (Cube=0, Sphere=1, Capsule=2,
    // Cylinder=3, Plane=4).
    static uint32_t MeshIndex(PrimitiveMesh mesh) {
        return (uint32_t)mesh;
    }

    void Renderer::Init() {
        const std::filesystem::path shaderDir = FindShaderDirectory();
        auto loadShader = [&](const char* name, const char* vertexFile, const char* fragmentFile, const ShaderDefines& defines = {}) {
            return Shader::CreateFromFiles(name, shaderDir / vertexFile, shaderDir / fragmentFile, defines);
        };

        // MAX_LIGHTS e injetado no GLSL a partir da constante C++: os dois
        // lados nunca divergem (basic.frag nao compila sem ele).
        s_BasicShader = loadShader("BasicLit", "basic.vert", "basic.frag", { { "MAX_LIGHTS", std::to_string(MAX_LIGHTS) } });

        // Gera a geometria de cada primitiva uma unica vez (CPU, ver
        // PrimitiveMeshFactory) e sobe para a GPU como um Mesh - reusado
        // para toda entidade que usar aquela primitiva, sem duplicar
        // buffers por entidade.
        auto upload = [](PrimitiveMesh type, GeneratedMesh generated) {
            s_Meshes[MeshIndex(type)] = Mesh::Create(generated.Vertices, generated.Indices);
        };

        upload(PrimitiveMesh::Cube, PrimitiveMeshFactory::CreateCube());
        upload(PrimitiveMesh::Sphere, PrimitiveMeshFactory::CreateSphere());
        upload(PrimitiveMesh::Capsule, PrimitiveMeshFactory::CreateCapsule());
        upload(PrimitiveMesh::Cylinder, PrimitiveMeshFactory::CreateCylinder());
        upload(PrimitiveMesh::Plane, PrimitiveMeshFactory::CreatePlane());

        s_LineShader = loadShader("Line", "line.vert", "line.frag");
        s_ShadowDepthShader = loadShader("ShadowDepth", "shadow_depth.vert", "shadow_depth.frag");
        // s_ShadowMap NAO e criado aqui de proposito - ver comentario em
        // Renderer.h (s_ShadowMap) sobre alocacao sob demanda.

        s_GeometryPrePassShader = loadShader("GeometryPrePass", "geometry_prepass.vert", "geometry_prepass.frag");
        s_SSAOShader = loadShader("SSAO", "fullscreen_quad.vert", "ssao.frag");
        s_SSAOBlurShader = loadShader("SSAOBlur", "fullscreen_quad.vert", "ssao_blur.frag");
        // s_GeometryBuffer/s_SSAO NAO sao criados aqui de proposito - mesma
        // alocacao sob demanda de s_ShadowMap (ver comentario em Renderer.h).
        //
        // Nenhum VAO do fullscreen quad e criado aqui - GetFullscreenQuadVAOForCurrentContext()
        // cria sob demanda, por contexto GLFW (ver comentario grande em
        // Renderer.h sobre por que isto e necessario).

        // VAO/VBO de linhas: so posicao (3 floats), sem EBO - DrawLines()
        // sempre desenha via GL_LINES direto do VBO, sem indexacao. O VBO
        // comeca vazio (GL_DYNAMIC_DRAW, sem dados ainda) - o conteudo real
        // e enviado a cada chamada de DrawLines() via glBufferData, ja que
        // gizmos mudam de forma frame a frame.
        glCreateVertexArrays(1, &s_LineVAO);
        glCreateBuffers(1, &s_LineVBO);
        glVertexArrayVertexBuffer(s_LineVAO, 0, s_LineVBO, 0, 3 * sizeof(float));
        glEnableVertexArrayAttrib(s_LineVAO, 0);
        glVertexArrayAttribFormat(s_LineVAO, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(s_LineVAO, 0, 0);

        PRISM_CORE_INFO("Renderer inicializado (shader basico + 5 primitivas: Cube, Sphere, Capsule, Cylinder, Plane; shader de linhas para gizmos).");
    }

    void Renderer::SetShaderHotReload(bool enabled) {
        s_ShaderHotReload = enabled;
    }

    void Renderer::ReloadChangedShaders() {
        if (!s_ShaderHotReload)
            return;

        const double now = glfwGetTime();
        if (now - s_LastShaderPoll < kShaderPollInterval)
            return;
        s_LastShaderPoll = now;

        // Cada Shader decide se seus arquivos mudaram; recarga que falha
        // mantem o programa antigo (ver Shader::ReloadIfChanged).
        for (Ref<Shader>* shader : { &s_BasicShader, &s_LineShader, &s_ShadowDepthShader,
                                     &s_GeometryPrePassShader, &s_SSAOShader, &s_SSAOBlurShader }) {
            if (*shader)
                (*shader)->ReloadIfChanged();
        }
    }

    void Renderer::Shutdown() {
        for (auto& mesh : s_Meshes)
            mesh.reset();
        s_BasicShader.reset();
        s_LineShader.reset();
        s_ShadowDepthShader.reset();
        s_ShadowMap.reset();
        s_GeometryPrePassShader.reset();
        s_SSAOShader.reset();
        s_SSAOBlurShader.reset();
        s_GeometryBuffer.reset();
        s_SSAO.reset();

        if (s_LineVBO) { glDeleteBuffers(1, &s_LineVBO); s_LineVBO = 0; }
        if (s_LineVAO) { glDeleteVertexArrays(1, &s_LineVAO); s_LineVAO = 0; }

        // Deleta todos os VAOs do fullscreen quad, um por contexto GLFW
        // (ver GetFullscreenQuadVAOForCurrentContext) - cada glDeleteVertexArrays
        // so tem efeito real se chamado com o contexto correspondente
        // ainda ativo/valido, mas e seguro chamar mesmo apos o contexto
        // ja ter sido destruido (o driver ignora silenciosamente).
        for (auto& [window, vao] : s_FullscreenQuadVAOsByContext)
            glDeleteVertexArrays(1, &vao);
        s_FullscreenQuadVAOsByContext.clear();
    }

    // Ver comentario grande em Renderer.h (s_FullscreenQuadVAOsByContext)
    // sobre por que isto e necessario (VAOs nao sao compartilhados entre
    // contextos GLFW, mesmo com share list) - mesma tecnica de
    // Mesh::BindForCurrentContext (Mesh.cpp), simplificada aqui porque
    // este VAO nao tem VBO/atributos para reconfigurar, so precisa
    // existir.
    uint32_t Renderer::GetFullscreenQuadVAOForCurrentContext() {
        GLFWwindow* current = glfwGetCurrentContext();

        for (auto& [window, vao] : s_FullscreenQuadVAOsByContext) {
            if (window == current)
                return vao;
        }

        uint32_t vao;
        glCreateVertexArrays(1, &vao);
        s_FullscreenQuadVAOsByContext.emplace_back(current, vao);
        return vao;
    }

    void Renderer::Clear(float r, float g, float b, float a) {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void Renderer::SetViewport(uint32_t width, uint32_t height) {
        glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    }

    // Chave do cache combina path + isSRGB (ver comentario grande em
    // GetOrLoadTexture, Renderer.h) - "\x01" e um separador que nunca
    // aparece de verdade num path de arquivo, evitando colisao tipo
    // ("a/b", true) vs ("a/btrue"... impossivel, mas mantido explicito
    // por clareza) sem precisar de um struct/pair como chave de hash.
    static std::string TextureCacheKey(const std::string& path, bool isSRGB) {
        return path + '\x01' + (isSRGB ? '1' : '0');
    }

    Texture2D* Renderer::GetOrLoadTexture(const std::string& path, bool isSRGB) {
        if (path.empty())
            return nullptr; // "sem textura configurada" - ver comentario em Renderer.h, sem logar erro

        // MaterialComponent::AlbedoPath/NormalPath/RoughnessMetallicPath
        // sao salvos RELATIVOS a pasta do projeto (mesma convencao do
        // resto da engine - ver ContentBrowserPanel/SceneSerializer), mas
        // Texture2D/stb_image abrem o arquivo relativo ao diretorio de
        // trabalho do PROCESSO - por isso resolvemos para um path
        // absoluto aqui, no UNICO lugar que carrega texturas de fato
        // (tanto o editor quanto uma futura PlayWindow/build standalone
        // passam por aqui, entao os dois resolvem igual, sem duplicar
        // essa logica). Sem projeto ativo (nao deveria acontecer na
        // pratica - nenhuma Scene existe sem Project - mas por seguranca
        // contra chamadores incomuns/testes), usa o path como veio.
        std::string resolvedPath = path;
        if (auto project = Project::GetActive())
            resolvedPath = (project->GetProjectDirectory() / path).string();

        std::string key = TextureCacheKey(resolvedPath, isSRGB);
        auto it = s_TextureCache.find(key);
        if (it != s_TextureCache.end())
            return it->second.get();

        // Texture2D::Texture2D ja loga PRISM_CORE_ERROR e marca
        // IsValid()==false internamente se o arquivo nao existir/nao
        // puder ser decodificado (ver Texture.h) - ainda guardamos o
        // ponteiro no cache mesmo invalido, para nao tentar reler do
        // disco a CADA frame enquanto o path continuar quebrado (ex:
        // usuario digitou um path errado e ainda nao corrigiu).
        auto texture = CreateScope<Texture2D>(resolvedPath, isSRGB);
        Texture2D* result = texture.get();
        s_TextureCache[key] = std::move(texture);
        return result;
    }

    Mesh* Renderer::GetOrLoadModelMesh(AssetID modelAsset) {
        if (!modelAsset.IsValid())
            return nullptr; // "sem modelo importado" - ver comentario em Renderer.h, sem logar erro

        // find() (nao operator[]) para distinguir "entrada ausente" (nunca
        // tentamos) de "entrada presente com Scope nulo" (ja tentamos e
        // FALHOU - ver comentario grande em s_ModelMeshCache, Renderer.h)
        // sem criar uma entrada nova so de consultar.
        auto it = s_ModelMeshCache.find(modelAsset);
        if (it != s_ModelMeshCache.end())
            return it->second.get(); // pode ser nullptr de proposito (falha em cache)

        auto project = Project::GetActive();
        if (!project) {
            // Nao deveria acontecer na pratica (nenhuma Scene existe sem
            // Project - mesma guarda defensiva de GetOrLoadTexture), mas
            // sem projeto ativo nao ha AssetRegistry para resolver o ID.
            // NAO cacheia como falha permanente: um projeto pode ficar
            // ativo logo em seguida (ex: durante a inicializacao), e essa
            // falha e uma condicao transitoria, diferente de um arquivo
            // de fato corrompido/ausente.
            return nullptr;
        }

        std::filesystem::path absolutePath = project->GetAssetRegistry().AbsolutePath(modelAsset);
        if (absolutePath.empty()) {
            // AssetID nao resolve para nenhum arquivo (asset apagado ou
            // .meta perdido - ver comentario grande em AssetRegistry.h).
            // ESTE caso E cacheado como falha: o ID continua invalido ate
            // o usuario trocar o vinculo (ou o asset reaparecer e um
            // Refresh() rodar) - reter 'null' evita reconsultar o
            // AssetRegistry a cada frame por uma referencia que sabemos
            // estar quebrada agora.
            PRISM_CORE_ERROR("Renderer: MeshRendererComponent::ModelAsset (", modelAsset.ToString(), ") nao resolve para nenhum arquivo - asset apagado ou .meta ausente?");
            s_ModelMeshCache[modelAsset] = nullptr;
            return nullptr;
        }

        ModelImportResult imported = ModelLoader::Load(absolutePath);
        if (!imported.Success) {
            // ModelLoader::Load ja loga o erro detalhado (PRISM_CORE_ERROR,
            // ver ModelLoader.cpp) - aqui so cacheia a falha, mesma logica
            // do bloco acima.
            s_ModelMeshCache[modelAsset] = nullptr;
            return nullptr;
        }

        auto mesh = Mesh::Create(imported.Mesh.Vertices, imported.Mesh.Indices);
        Mesh* result = mesh.get();
        s_ModelMeshCache[modelAsset] = std::move(mesh);
        return result;
    }

    void Renderer::InvalidateModelCache(AssetID modelAsset) {
        s_ModelMeshCache.erase(modelAsset);
    }

    Mesh* Renderer::ResolveMesh(const MeshRendererComponent& meshRenderer) {
        if (meshRenderer.ModelAsset.IsValid()) {
            Mesh* modelMesh = GetOrLoadModelMesh(meshRenderer.ModelAsset);
            if (modelMesh)
                return modelMesh;
            // Vinculo quebrado ou importacao falhou (ver comentario
            // grande em GetOrLoadModelMesh) - cai para a primitiva
            // 'Mesh' em vez de desenhar/testar picking contra nada, para
            // a entidade continuar visivel/clicavel (com a forma
            // "errada", mas presente) em vez de desaparecer
            // silenciosamente da cena.
        }
        return s_Meshes[MeshIndex(meshRenderer.Mesh)].get();
    }

    // Matriz normal (transpose(inverse(mat3))) de 'model'. Ver o comentario
    // de u_NormalMatrix em shaders/basic.vert para o PORQUE.
    //
    // Casos que NAO podem virar NaN: um eixo de escala exatamente 0 (o
    // gizmo de escalar e um .prismmap editado a mao chegam nisso, mesmo o
    // campo "Escala" limitando em 0.01) torna a matriz singular, e a
    // inversa dela vira inf/NaN - a normal com NaN deixa o objeto preto ou
    // piscando. Nesses casos devolve mat3(model) (o comportamento antigo):
    // o objeto colapsou num plano/linha/ponto, entao nao ha "normal certa"
    // a preservar. Escala NEGATIVA (espelho) funciona normalmente: o
    // determinante negativo nao e singular, e a inversa-transposta espelha
    // a normal junto com a geometria.
    static glm::mat3 ComputeNormalMatrix(const glm::mat4& model) {
        glm::mat3 m3(model);
        float det = glm::determinant(m3);
        if (!std::isfinite(det) || std::abs(det) < 1e-12f)
            return m3;

        glm::mat3 n = glm::transpose(glm::inverse(m3));
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++)
                if (!std::isfinite(n[c][r]))
                    return m3;
        return n;
    }

    static float SrgbChannelToLinear(float c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    // Fator u_EnvScale: u_Ambient dividido pela luminancia MEDIA da
    // irradiancia do gradiente (media sobre a esfera de normais). Como a
    // irradiancia e linear nas tres cores, a media tambem: as medias dos
    // pesos do ceu / horizonte / chao sao 0.250148 / 0.499705 / 0.250148
    // (medias exatas dos polinomios de EnvironmentIrradiance, com E[ny]=0 e
    // E[ny^2]=1/3; conferido contra integracao numerica em 5 paletas, erro
    // maximo 5e-6). Luminancia Rec.709 sobre as cores em LINEAR.
    //
    // Paleta totalmente preta => luminancia media 0 => dividir daria infinito.
    // Nesse caso o ambiente e preto de qualquer jeito (todas as cores sao 0),
    // entao devolve 0 e nada e somado.
    static float ComputeEnvironmentNormalization(float ambient, const float* zenithSrgb, const float* horizonSrgb, const float* groundSrgb) {
        auto lum = [](const float* srgb) {
            float r = SrgbChannelToLinear(srgb[0]);
            float g = SrgbChannelToLinear(srgb[1]);
            float b = SrgbChannelToLinear(srgb[2]);
            return 0.2126f * r + 0.7152f * g + 0.0722f * b;
        };
        float meanLum = lum(zenithSrgb) * 0.250148f + lum(horizonSrgb) * 0.499705f + lum(groundSrgb) * 0.250148f;
        if (!(meanLum > 1e-6f))
            return 0.0f;
        return ambient / meanLum;
    }

    void Renderer::DrawMesh(PrimitiveMesh meshType, const float* viewProjection, const float* model, const float* color, const std::vector<GPULight>* lights, const ShadowMap* shadowMap, int shadowCasterLightIndex, const SSAO* ssao, const MaterialComponent* material) {
        Mesh* mesh = s_Meshes[MeshIndex(meshType)].get();
        DrawMesh(mesh, viewProjection, model, color, lights, shadowMap, shadowCasterLightIndex, ssao, material);
    }

    void Renderer::DrawMesh(Mesh* mesh, const float* viewProjection, const float* model, const float* color, const std::vector<GPULight>* lights, const ShadowMap* shadowMap, int shadowCasterLightIndex, const SSAO* ssao, const MaterialComponent* material) {
        if (!s_BasicShader) return;
        if (!mesh) return; // primitiva ainda nao carregada (nao deveria acontecer apos Init()) OU modelo importado que falhou a carregar (ver GetOrLoadModelMesh) - evita um crash silencioso nos dois casos

        s_BasicShader->Bind();
        s_BasicShader->SetMat4("u_ViewProjection", viewProjection);
        s_BasicShader->SetMat4("u_Model", model);
        {
            // Sem este envio, u_NormalMatrix valeria zero (padrao de uniform
            // nao setado): normal (0,0,0) => normalize() vira NaN no fragment
            // shader e o objeto sai preto. Ver shaders/basic.vert.
            glm::mat3 normalMatrix = ComputeNormalMatrix(glm::make_mat4(model));
            s_BasicShader->SetMat3("u_NormalMatrix", glm::value_ptr(normalMatrix));
        }
        s_BasicShader->SetFloat3("u_CameraWorldPos", s_CameraWorldPos[0], s_CameraWorldPos[1], s_CameraWorldPos[2]);
        // Sem este envio, u_Exposure valeria 0.0 (padrao de uniform nao
        // setado em GLSL) e a cena inteira sairia preta - ver shaders/basic.frag.
        s_BasicShader->SetFloat("u_Exposure", s_Exposure);
        s_BasicShader->SetFloat("u_Ambient", s_Ambient);
        // As tres cores do gradiente de ambiente. Sem estes envios elas
        // valeriam (0,0,0) e o ambiente inteiro (difuso e reflexo dos
        // metais) sairia preto. Ver EnvironmentColor em shaders/basic.frag.
        s_BasicShader->SetFloat3("u_EnvZenith",  s_EnvZenith[0],  s_EnvZenith[1],  s_EnvZenith[2]);
        s_BasicShader->SetFloat3("u_EnvHorizon", s_EnvHorizon[0], s_EnvHorizon[1], s_EnvHorizon[2]);
        s_BasicShader->SetFloat3("u_EnvGround",  s_EnvGround[0],  s_EnvGround[1],  s_EnvGround[2]);
        s_BasicShader->SetFloat("u_EnvScale", ComputeEnvironmentNormalization(s_Ambient, s_EnvZenith, s_EnvHorizon, s_EnvGround));

        // Com 'material' fornecido, AlbedoTint manda (ver comentario em
        // Renderer.h) - 'color' (MeshRendererComponent::Color) e ignorado
        // nesse caso, exatamente como um Material substitui a cor solida
        // antiga no editor (ver PropertiesPanel::OnImGuiRender).
        if (material)
            s_BasicShader->SetFloat3("u_BaseColor", material->AlbedoTint.x, material->AlbedoTint.y, material->AlbedoTint.z);
        else if (color)
            s_BasicShader->SetFloat3("u_BaseColor", color[0], color[1], color[2]);
        else
            s_BasicShader->SetFloat3("u_BaseColor", 0.85f, 0.55f, 0.2f);

        // --- Texturas do Material (slots 2/3/4 - 0/1 reservados para
        // shadow map / AO map, ver comentarios abaixo) --------------------
        // Sem 'material' (nullptr), os tres u_Has*Map ficam false e o
        // fragment shader nunca amostra sampler nenhum - identico ao
        // comportamento anterior a esta feature existir.
        if (material) {
            Texture2D* albedo = GetOrLoadTexture(material->AlbedoPath, /*isSRGB*/ true);
            if (albedo && albedo->IsValid()) {
                albedo->Bind(2);
                s_BasicShader->SetInt("u_AlbedoMap", 2);
                s_BasicShader->SetInt("u_HasAlbedoMap", 1);
            } else {
                s_BasicShader->SetInt("u_HasAlbedoMap", 0);
            }

            Texture2D* normalMap = GetOrLoadTexture(material->NormalPath, /*isSRGB*/ false);
            if (normalMap && normalMap->IsValid()) {
                normalMap->Bind(3);
                s_BasicShader->SetInt("u_NormalMap", 3);
                s_BasicShader->SetInt("u_HasNormalMap", 1);
            } else {
                s_BasicShader->SetInt("u_HasNormalMap", 0);
            }

            // RoughnessMetallicMap (slot 4): canal G = roughness, canal B =
            // metallic (convencao glTF), lido em main() e repassado a
            // CalculateLight (Cook-Torrance) - ver shaders/basic.frag acima.
            Texture2D* roughnessMetallic = GetOrLoadTexture(material->RoughnessMetallicPath, /*isSRGB*/ false);
            if (roughnessMetallic && roughnessMetallic->IsValid()) {
                roughnessMetallic->Bind(4);
                s_BasicShader->SetInt("u_RoughnessMetallicMap", 4);
                s_BasicShader->SetInt("u_HasRoughnessMetallicMap", 1);
            } else {
                s_BasicShader->SetInt("u_HasRoughnessMetallicMap", 0);
            }

            // Fatores PBR: multiplicam o mapa acima quando existe, ou valem
            // sozinhos (sem mapa). Clamp defensivo: valores fora de [0,1]
            // (ex: editados a mao num .prismmat) nao devem quebrar o BRDF.
            s_BasicShader->SetFloat("u_Roughness", glm::clamp(material->RoughnessFactor, 0.0f, 1.0f));
            s_BasicShader->SetFloat("u_Metallic", glm::clamp(material->MetallicFactor, 0.0f, 1.0f));
        } else {
            s_BasicShader->SetInt("u_HasAlbedoMap", 0);
            s_BasicShader->SetInt("u_HasNormalMap", 0);
            s_BasicShader->SetInt("u_HasRoughnessMetallicMap", 0);

            // Entidade SEM MaterialComponent: totalmente fosca e nao
            // metalica (roughness=1, metallic=0). Nao enviar nada deixaria
            // os dois em 0.0 (padrao de uniform GLSL) = ESPELHO METALICO
            // preto. Com 1/0 o resultado fica ~igual ao Lambert antigo
            // (ver CalculateLight em shaders/basic.frag), entao cenas existentes
            // nao mudam de aparencia.
            s_BasicShader->SetFloat("u_Roughness", 1.0f);
            s_BasicShader->SetFloat("u_Metallic", 0.0f);
        }

        // 'lights' e opcional (ver comentario em Renderer.h) - sem lista,
        // desenha so com o ambiente fixo do shader (u_LightCount = 0).
        if (lights)
            UploadLights(*lights, shadowMap ? shadowCasterLightIndex : -1);
        else
            s_BasicShader->SetInt("u_LightCount", 0);

        // 'shadowMap' e opcional (ver comentario em Renderer.h) - sem ele,
        // u_HasShadow fica false e CalculateShadow nunca e chamada no
        // shader.
        // GL_TEXTURE0 e reservado para o shadow map, GL_TEXTURE1 para o
        // AO map (ver abaixo); as texturas do Material usam os slots 2-4.
        if (shadowMap) {
            shadowMap->BindForReading(0);
            s_BasicShader->SetInt("u_ShadowMap", 0);
            s_BasicShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(shadowMap->GetLightSpaceMatrix()));
            s_BasicShader->SetFloat("u_ShadowFrustumRadius", shadowMap->GetFrustumRadius());
            s_BasicShader->SetInt("u_HasShadow", 1);
        } else {
            s_BasicShader->SetInt("u_HasShadow", 0);
        }

        // 'ssao' e opcional (ver comentario em Renderer.h) - sem ele,
        // u_HasAO fica false e o termo ambiente usa 1.0 (sem oclusao).
        if (ssao) {
            ssao->BindBlurredForReading(1);
            s_BasicShader->SetInt("u_AOMap", 1);
            s_BasicShader->SetInt("u_HasAO", 1);
            s_BasicShader->SetFloat2("u_ScreenSize", (float)ssao->GetWidth(), (float)ssao->GetHeight());
        } else {
            s_BasicShader->SetInt("u_HasAO", 0);
        }

        mesh->BindForCurrentContext();
        glDrawElements(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        s_BasicShader->Unbind();
    }

    void Renderer::SetCameraPosition(const float* worldPos) {
        s_CameraWorldPos[0] = worldPos[0];
        s_CameraWorldPos[1] = worldPos[1];
        s_CameraWorldPos[2] = worldPos[2];
    }

    void Renderer::SetExposure(float exposure) {
        // Ignora 0/negativo em vez de aceitar: exposicao <= 0 zera (ou
        // inverte) toda a cor antes do tone mapping - cena preta sem
        // nenhuma pista de por que. Manter o valor anterior e mais seguro.
        // isfinite: +inf passaria em 'exposure > 0' e faria o tone mapping
        // devolver NaN (tela toda preta ou piscando).
        if (std::isfinite(exposure) && exposure > 0.0f)
            s_Exposure = exposure;
    }

    float Renderer::GetExposure() {
        return s_Exposure;
    }

    void Renderer::SetAmbient(float ambient) {
        // Diferente de SetExposure, 0 e valido (cena so com luzes). So
        // negativo e rejeitado: ambiente negativo subtrairia luz.
        // isfinite pelo mesmo motivo de SetExposure (+inf passaria em >= 0).
        if (std::isfinite(ambient) && ambient >= 0.0f)
            s_Ambient = ambient;
    }

    float Renderer::GetAmbient() {
        return s_Ambient;
    }

    void Renderer::SetEnvironmentColors(const float* zenith, const float* horizon, const float* ground) {
        // nullptr = mantem a cor atual (permite trocar so o ceu, por exemplo).
        // Limita a [0,1]: o picker so produz esse intervalo, e um valor
        // fora dele (NaN incluido) viraria luz negativa/infinita no shader.
        auto assign = [](float* dst, const float* src) {
            if (!src) return;
            for (int i = 0; i < 3; i++) {
                float v = src[i];
                dst[i] = std::isfinite(v) ? std::min(std::max(v, 0.0f), 1.0f) : dst[i];
            }
        };
        assign(s_EnvZenith, zenith);
        assign(s_EnvHorizon, horizon);
        assign(s_EnvGround, ground);
    }

    void Renderer::GetEnvironmentColors(float* outZenith, float* outHorizon, float* outGround) {
        if (outZenith)  for (int i = 0; i < 3; i++) outZenith[i]  = s_EnvZenith[i];
        if (outHorizon) for (int i = 0; i < 3; i++) outHorizon[i] = s_EnvHorizon[i];
        if (outGround)  for (int i = 0; i < 3; i++) outGround[i]  = s_EnvGround[i];
    }

    void Renderer::ApplyRenderSettings(const RenderSettings& settings) {
        SetExposure(settings.Exposure);
        SetAmbient(settings.Ambient);
        SetEnvironmentColors(settings.EnvZenith, settings.EnvHorizon, settings.EnvGround);
    }

    void Renderer::DrawLines(const float* points, uint32_t pointCount, const float* viewProjection, const float* color) {
        if (!s_LineShader || pointCount == 0) return;

        // ATENCAO: s_LineVAO e criado uma unica vez em Init() (contexto do
        // editor) e, como qualquer VAO, NAO e valido em outro contexto
        // OpenGL mesmo com share list (ver Mesh.h/
        // Mesh::BindForCurrentContext). DrawLines() so e chamado pelo
        // EditorLayer (gizmos), nunca pela PlayWindow - se gizmos de debug
        // forem desenhados tambem na PlayWindow, esta funcao vai precisar
        // do mesmo tratamento de "VAO por contexto" que Mesh:: ja tem.
        glBindVertexArray(s_LineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, s_LineVBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pointCount * 3 * sizeof(float)), points, GL_DYNAMIC_DRAW);

        s_LineShader->Bind();
        s_LineShader->SetMat4("u_ViewProjection", viewProjection);
        s_LineShader->SetFloat3("u_BaseColor", color[0], color[1], color[2]);

        glDrawArrays(GL_LINES, 0, (GLsizei)pointCount);

        glBindVertexArray(0);
        s_LineShader->Unbind();
    }

    std::vector<GPULight> Renderer::CollectGPULights(Scene& scene) {
        std::vector<GPULight> result;
        result.reserve(MAX_LIGHTS);

        // Toda entidade com TransformComponent + LightComponent conta como
        // fonte de luz. Usa a transform de MUNDO (mesma logica que
        // DrawScene ja usa para meshes) - uma luz filha de um objeto pai
        // (ex: uma lanterna presa na mao de um personagem) acompanha a
        // posicao/rotacao do pai corretamente.
        auto view = scene.GetRegistry().view<TransformComponent, LightComponent>();
        for (auto entityHandle : view) {
            if (result.size() >= MAX_LIGHTS) {
                // Limite atingido - ver comentario em CollectGPULights
                // (Renderer.h) sobre por que isso e silencioso por
                // enquanto (sem priorizacao por distancia/importancia).
                break;
            }

            const auto& light = view.get<LightComponent>(entityHandle);
            glm::mat4 worldTransform = scene.GetWorldTransform(Entity(entityHandle, &scene));

            GPULight gpuLight;
            gpuLight.SourceEntityId = (uint32_t)entityHandle;
            gpuLight.Color = light.Color;
            gpuLight.Intensity = light.Intensity;
            gpuLight.Range = light.Range;

            // Posicao de mundo = coluna de translacao da matriz de
            // transform combinada (ultima coluna, xyz).
            gpuLight.Position = glm::vec3(worldTransform[3]);

            // Direcao "para frente" da entidade em espaco de mundo:
            // rotaciona o eixo -Z local (convencao da engine para "frente"
            // de uma entidade, mesma usada pela CameraComponent) pela
            // parte de rotacao/escala da matriz de mundo. Relevante para
            // Spot e Directional; Point ignora esse campo no shader.
            glm::vec3 forwardLocal = { 0.0f, 0.0f, -1.0f };
            gpuLight.Direction = glm::normalize(glm::mat3(worldTransform) * forwardLocal);

            // Traducao especifica por tipo - este e o UNICO lugar que
            // precisa de um novo "case" ao adicionar um LightType novo
            // (ex: Area) - ver comentario grande em Components.h sobre
            // extensibilidade. Point e o "default" (gpuLight.Type = 0)
            // ja fica correto sem entrar em nenhum branch abaixo.
            switch (light.Type) {
                case LightType::Point:
                    gpuLight.Type = 0; // LIGHT_TYPE_POINT no shader
                    break;
                case LightType::Spot:
                    gpuLight.Type = 1; // LIGHT_TYPE_SPOT no shader
                    // Pre-calcula os cossenos aqui (CPU, uma vez por
                    // frame por luz) em vez de no shader (por-fragmento,
                    // milhares de vezes por frame) - cos() e caro para
                    // repetir por pixel quando o angulo so muda quando o
                    // usuario edita a luz.
                    gpuLight.CosOuterAngle = glm::cos(glm::radians(light.SpotAngle));
                    gpuLight.CosInnerAngle = glm::cos(glm::radians(glm::min(light.InnerSpotAngle, light.SpotAngle)));
                    break;
                case LightType::Directional:
                    gpuLight.Type = 2; // LIGHT_TYPE_DIRECTIONAL no shader
                    break;
                // TODO(Area/IES): quando LightType ganhar esses valores
                // (ver Components.h), adicionar os cases aqui.
            }

            result.push_back(gpuLight);
        }

        return result;
    }

    void Renderer::UploadLights(const std::vector<GPULight>& lights, int shadowCasterLightIndex) {
        if (!s_BasicShader) return;

        int count = (int)std::min<size_t>(lights.size(), MAX_LIGHTS);
        s_BasicShader->SetInt("u_LightCount", count);
        s_BasicShader->SetInt("u_ShadowCasterLightIndex", shadowCasterLightIndex);

        for (int i = 0; i < count; i++) {
            const GPULight& light = lights[i];
            std::string prefix = "u_Lights[" + std::to_string(i) + "].";

            s_BasicShader->SetInt(prefix + "Type", light.Type);
            s_BasicShader->SetFloat3(prefix + "Position", light.Position.x, light.Position.y, light.Position.z);
            s_BasicShader->SetFloat3(prefix + "Direction", light.Direction.x, light.Direction.y, light.Direction.z);
            s_BasicShader->SetFloat3(prefix + "Color", light.Color.x, light.Color.y, light.Color.z);
            s_BasicShader->SetFloat(prefix + "Intensity", light.Intensity);
            s_BasicShader->SetFloat(prefix + "Range", light.Range);
            s_BasicShader->SetFloat(prefix + "CosOuterAngle", light.CosOuterAngle);
            s_BasicShader->SetFloat(prefix + "CosInnerAngle", light.CosInnerAngle);
        }
    }

    // ========================================================================
    // Shadow mapping - RenderShadowPass
    //
    // PIPELINE (2 passes, ambos escondidos dentro de DrawScene - nenhum
    // chamador de fora precisa saber que isto acontece):
    //   1) RenderShadowPass (esta funcao): acha a primeira luz Directional
    //      com CastShadows=true, calcula um frustum ORTHO cobrindo a cena
    //      inteira a partir do PONTO DE VISTA DELA, e desenha toda
    //      entidade com mesh (mesmo loop de DrawScene, so que com
    //      s_ShadowDepthShader em vez de s_BasicShader) dentro do
    //      ShadowMap.
    //   2) De volta em DrawScene: pass de cor normal, de novo por toda
    //      entidade, agora passando o ShadowMap resultante para DrawMesh
    //      amostrar (ver CalculateShadow no shader).
    //
    // ESCOPO DELIBERADAMENTE LIMITADO desta primeira implementacao (ver
    // discussao de arquitetura que precedeu este codigo):
    //   - So UMA luz projeta sombra por frame, e so se for Directional -
    //     Point/Spot exigiriam projecao perspective (ou 6 faces de
    //     cubemap, no caso de Point) em vez do ortho simples aqui; adiado
    //     de proposito para manter esta etapa pequena e testavel. Uma
    //     segunda LightComponent com CastShadows=true simplesmente e
    //     ignorada, sem erro nem aviso (um PRISM_CORE_WARN uma vez por
    //     sessao ajudaria).
    //   - SEM Cascaded Shadow Maps (CSM): um unico frustum ortho cobre a
    //     cena inteira (ver bounding box abaixo) em vez de varios
    //     frustums encadeados por distancia da camera. Para as cenas de
    //     portfolio/prototipo que esta engine desenha hoje a resolucao
    //     fixa (ShadowMap::m_Resolution) e suficiente; CSM e a evolucao
    //     natural se cenas maiores comecarem a mostrar sombra
    //     visivelmente pixelizada.
    //   - Bounding box da cena recalculado A CADA FRAME (loop simples
    //     sobre TransformComponent, ver abaixo) - barato (nao envolve
    //     geometria, so a posicao de cada entidade) mas significa que o
    //     frustum da luz "respira" (muda de tamanho) conforme objetos se
    //     movem/aparecem/somem. Aceitavel para o nivel de fidelidade
    //     atual da engine; uma versao futura poderia fixar o frustum
    //     manualmente (ex: um campo no LightComponent) se o "respirar"
    //     incomodar visualmente em alguma cena especifica.
    ShadowMap* Renderer::RenderShadowPass(Scene& scene) {
        // Acha a primeira luz Directional com CastShadows=true - "primeira"
        // na ordem de iteracao do registry EnTT (nao ha prioridade
        // explicita ainda, mesma limitacao ja documentada para
        // CollectGPULights/MAX_LIGHTS).
        entt::entity shadowCasterHandle = entt::null;
        glm::vec3 lightDirection{ 0.0f, -1.0f, 0.0f };

        auto lightView = scene.GetRegistry().view<TransformComponent, LightComponent>();
        for (auto entityHandle : lightView) {
            const auto& light = lightView.get<LightComponent>(entityHandle);
            if (light.Type == LightType::Directional && light.CastShadows) {
                shadowCasterHandle = entityHandle;
                glm::mat4 worldTransform = scene.GetWorldTransform(Entity(entityHandle, &scene));
                lightDirection = glm::normalize(glm::mat3(worldTransform) * glm::vec3(0.0f, 0.0f, -1.0f));
                break;
            }
        }

        if (shadowCasterHandle == entt::null) {
            // Nenhuma luz projeta sombra - DrawScene desenha sem shadow
            // map. Limpa o estado
            // "shadow caster" do frame anterior (ver comentario em
            // s_HasShadowCasterEntity, Renderer.h) para DrawScene nao
            // reusar por engano o indice de um frame onde a luz shadow
            // caster foi removida/desativada entre um frame e outro.
            s_HasShadowCasterEntity = false;
            return nullptr;
        }

        // Publica a identidade da entidade shadow caster para DrawScene
        // (ver comentario em s_ShadowCasterEntityId, Renderer.h) - usa a
        // IDENTIDADE da entidade, nao o Type, para o caso de duas luzes
        // Directional na cena onde so uma tem CastShadows=true (ver
        // comentario grande em GPULight::SourceEntityId sobre o bug que
        // isto evita).
        s_ShadowCasterEntityId = (uint32_t)shadowCasterHandle;
        s_HasShadowCasterEntity = true;

        // Aloca o ShadowMap sob demanda (ver comentario em Renderer.h) -
        // so na primeira vez que uma cena realmente usa CastShadows.
        if (!s_ShadowMap)
            s_ShadowMap = CreateScope<ShadowMap>();

        // --- Bounding box (AABB) da cena, so pelas posicoes de mundo das
        // entidades com mesh - usado para dimensionar o frustum ortho da
        // luz (ver comentario grande acima da funcao sobre as
        // simplificacoes desta etapa). Nao usa os vertices de cada mesh
        // (so o "centro" de cada entidade, via GetWorldTransform) para
        // manter isto barato - uma margem fixa (ver 'padding' abaixo)
        // compensa o mesh de cada entidade se estender alem do seu
        // centro.
        glm::vec3 sceneMin(std::numeric_limits<float>::max());
        glm::vec3 sceneMax(std::numeric_limits<float>::lowest());
        bool anyMesh = false;

        auto meshView = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();
        for (auto entityHandle : meshView) {
            glm::vec3 pos = glm::vec3(scene.GetWorldTransform(Entity(entityHandle, &scene))[3]);
            sceneMin = glm::min(sceneMin, pos);
            sceneMax = glm::max(sceneMax, pos);
            anyMesh = true;
        }

        if (!anyMesh) {
            // Cena sem nenhum mesh (so a luz) - nao ha nada para
            // sombrear; usa uma caixa pequena arbitraria em vez de deixar
            // sceneMin/sceneMax com os valores extremos de inicializacao
            // (o que produziria um frustum absurdamente grande).
            sceneMin = glm::vec3(-1.0f);
            sceneMax = glm::vec3(1.0f);
        }

        glm::vec3 sceneCenter = (sceneMin + sceneMax) * 0.5f;
        float sceneRadius = glm::length(sceneMax - sceneMin) * 0.5f;
        const float padding = 2.0f; // margem para o mesh de cada entidade (nao so seu centro) caber dentro do frustum
        sceneRadius = glm::max(sceneRadius, 1.0f) + padding;

        // Posiciona a "camera" da luz longe o suficiente do centro da
        // cena (na direcao OPOSTA a lightDirection, ja que a luz "viaja"
        // na direcao lightDirection) para o near plane (proximo bloco)
        // nao cortar nada.
        glm::vec3 lightPos = sceneCenter - lightDirection * sceneRadius * 2.0f;
        glm::mat4 lightView_ = glm::lookAt(lightPos, sceneCenter, glm::abs(lightDirection.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProjection = glm::ortho(-sceneRadius, sceneRadius, -sceneRadius, sceneRadius, 0.1f, sceneRadius * 4.0f);
        glm::mat4 lightSpaceMatrix = lightProjection * lightView_;

        s_ShadowMap->SetLightSpaceMatrix(lightSpaceMatrix);
        s_ShadowMap->SetFrustumRadius(sceneRadius);

        // --- Pass de profundidade: mesmo loop de entidades de DrawScene,
        // shader/framebuffer diferentes.
        s_ShadowMap->BindForWriting();

        s_ShadowDepthShader->Bind();
        s_ShadowDepthShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(lightSpaceMatrix));

        for (auto entityHandle : meshView) {
            auto& meshRenderer = meshView.get<MeshRendererComponent>(entityHandle);
            glm::mat4 model = scene.GetWorldTransform(Entity(entityHandle, &scene));
            s_ShadowDepthShader->SetMat4("u_Model", glm::value_ptr(model));

            Mesh* mesh = ResolveMesh(meshRenderer); // primitiva OU modelo importado, ver comentario em ResolveMesh
            if (!mesh) continue;
            mesh->BindForCurrentContext();
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        }

        glBindVertexArray(0);
        s_ShadowDepthShader->Unbind();
        s_ShadowMap->Unbind();

        return s_ShadowMap.get();
    }

    // ========================================================================
    // RenderGeometryPrePass / RenderSSAOPass / RenderSSAOBlurPass
    // Ver comentario grande em SSAO.h para o pipeline completo. Ordem de
    // chamada fixa (imposta por DrawScene, nao pelos proprios metodos):
    // geometria -> SSAO bruto -> blur.
    // ========================================================================

    GeometryBuffer* Renderer::RenderGeometryPrePass(Scene& scene, const float* view, const float* projection, uint32_t width, uint32_t height) {
        // Aloca sob demanda (mesma logica de s_ShadowMap, ver comentario
        // em Renderer.h) - so na primeira vez que DrawScene decide usar
        // SSAO (ver flag em DrawScene). Redimensiona se a resolucao da
        // viewport mudou desde o ultimo frame (GeometryBuffer::Resize e
        // no-op se o tamanho for igual).
        if (!s_GeometryBuffer)
            s_GeometryBuffer = CreateScope<GeometryBuffer>(width, height);
        else
            s_GeometryBuffer->Resize(width, height);

        s_GeometryBuffer->BindForWriting();

        s_GeometryPrePassShader->Bind();
        glm::mat4 viewProjection = glm::make_mat4(projection) * glm::make_mat4(view);
        s_GeometryPrePassShader->SetMat4("u_ViewProjection", glm::value_ptr(viewProjection));
        s_GeometryPrePassShader->SetMat4("u_View", view);

        auto meshView = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();
        for (auto entityHandle : meshView) {
            auto& meshRenderer = meshView.get<MeshRendererComponent>(entityHandle);
            glm::mat4 model = scene.GetWorldTransform(Entity(entityHandle, &scene));
            s_GeometryPrePassShader->SetMat4("u_Model", glm::value_ptr(model));
            {
                // View-space: a matriz normal e a de (view * model), nao so
                // a do model - a normal chega ao SSAO ja em view-space.
                glm::mat3 viewNormalMatrix = ComputeNormalMatrix(glm::make_mat4(view) * model);
                s_GeometryPrePassShader->SetMat3("u_ViewNormalMatrix", glm::value_ptr(viewNormalMatrix));
            }

            Mesh* mesh = ResolveMesh(meshRenderer); // primitiva OU modelo importado, ver comentario em ResolveMesh
            if (!mesh) continue;
            mesh->BindForCurrentContext();
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        }

        glBindVertexArray(0);
        s_GeometryPrePassShader->Unbind();
        s_GeometryBuffer->Unbind();

        return s_GeometryBuffer.get();
    }

    SSAO* Renderer::RenderSSAOPass(GeometryBuffer& gBuffer, const float* projection, uint32_t width, uint32_t height) {
        if (!s_SSAO)
            s_SSAO = CreateScope<SSAO>(width, height);
        else
            s_SSAO->Resize(width, height);

        glm::mat4 projectionMatrix = glm::make_mat4(projection);
        glm::mat4 inverseProjection = glm::inverse(projectionMatrix);

        s_SSAO->BindRawForWriting();

        // Fullscreen quad roda SEM depth test (nao escreve/le profundidade
        // nenhuma - e so um pass de calculo por pixel de tela inteira) e
        // SEM blend (queremos SUBSTITUIR o texel, nao misturar com o que
        // ja estava la, que e lixo de memoria/frame anterior de qualquer
        // forma) - ambos habilitados globalmente em
        // OpenGLContext::Init(), entao precisam ser desligados aqui e
        // religados no fim (ver bloco simetrico no final desta funcao e
        // em RenderSSAOBlurPass).
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        s_SSAOShader->Bind();
        s_SSAOShader->SetMat4("u_Projection", projection);
        s_SSAOShader->SetMat4("u_InverseProjection", glm::value_ptr(inverseProjection));
        s_SSAOShader->SetFloat2("u_ScreenSize", (float)width, (float)height);
        s_SSAOShader->SetFloat2("u_NoiseScale", (float)width / 4.0f, (float)height / 4.0f);
        s_SSAOShader->SetFloat3Array("u_Samples", glm::value_ptr(s_SSAO->GetKernel()[0]), SSAO::KernelSize);

        gBuffer.BindNormalForReading(0);
        s_SSAOShader->SetInt("u_ViewNormal", 0);
        gBuffer.BindDepthForReading(1);
        s_SSAOShader->SetInt("u_Depth", 1);
        s_SSAO->BindNoiseForReading(2);
        s_SSAOShader->SetInt("u_NoiseTexture", 2);

        glBindVertexArray(GetFullscreenQuadVAOForCurrentContext());
        glDrawArrays(GL_TRIANGLES, 0, 3); // "big triangle" - ver comentario em shaders/fullscreen_quad.vert
        glBindVertexArray(0);

        s_SSAOShader->Unbind();
        s_SSAO->Unbind();

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);

        return s_SSAO.get();
    }

    void Renderer::RenderSSAOBlurPass(SSAO& ssao) {
        ssao.BindBlurredForWriting();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        s_SSAOBlurShader->Bind();
        ssao.BindRawForReading(0);
        s_SSAOBlurShader->SetInt("u_SSAOTexture", 0);

        glBindVertexArray(GetFullscreenQuadVAOForCurrentContext());
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        s_SSAOBlurShader->Unbind();
        ssao.Unbind();

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
    }

    void Renderer::DrawScene(Scene& scene, const float* view, const float* projection, const float* cameraWorldPos) {
        SetCameraPosition(cameraWorldPos);

        glm::mat4 viewProjection = glm::make_mat4(projection) * glm::make_mat4(view);

        // Coleta todas as luzes da cena UMA VEZ por frame (nao por
        // entidade desenhada) - reusada em todo DrawMesh abaixo, ja que a
        // lista de luzes ativas nao muda entre um mesh e outro do mesmo
        // frame. Ver CollectGPULights para a logica de traducao
        // LightComponent -> GPULight.
        std::vector<GPULight> lights = CollectGPULights(scene);

        // Salva framebuffer + viewport ATUAIS antes de qualquer sub-pass
        // que desenhe em outro lugar (shadow map, G-buffer, SSAO) - todos
        // eles mudam framebuffer/viewport temporariamente e sao
        // restaurados ao final de cada um deles aqui, de forma explicita e
        // centralizada, em vez de espalhado em cada sub-pass.
        GLint previousFramebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        GLint previousViewport[4];
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        uint32_t viewportWidth = (uint32_t)previousViewport[2];
        uint32_t viewportHeight = (uint32_t)previousViewport[3];

        ShadowMap* shadowMap = RenderShadowPass(scene);

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

        // Descobre o indice, dentro de 'lights' (JA COLETADA acima), da
        // luz shadow caster que RenderShadowPass acabou de desenhar -
        // casa por IDENTIDADE DE ENTIDADE (GPULight::SourceEntityId),
        // NAO por Type: comparar so por LIGHT_TYPE_DIRECTIONAL falharia
        // silenciosamente numa cena com duas luzes Directional onde so
        // uma tem CastShadows=true (ver comentario grande em
        // GPULight::SourceEntityId sobre esse bug especifico).
        int shadowCasterLightIndex = -1;
        if (shadowMap && s_HasShadowCasterEntity) {
            for (size_t i = 0; i < lights.size(); i++) {
                if (lights[i].SourceEntityId == s_ShadowCasterEntityId) {
                    shadowCasterLightIndex = (int)i;
                    break;
                }
            }
        }

        // --- SSAO: pre-pass de geometria (camera principal) + calculo +
        // blur - ver comentario grande em SSAO.h. So roda quando a
        // viewport tem tamanho valido (largura/altura > 0) - pode ser 0
        // por um frame quando o painel esta sendo redimensionado/oculto.
        //
        // TODO: SSAO esta SEMPRE ativo (sem opcao de liga/desliga no
        // editor, ao contrario de CastShadows por luz); poderia virar uma
        // configuracao de qualidade grafica (ex: "Render Settings" por
        // cena/projeto).
        SSAO* ssao = nullptr;
        if (viewportWidth > 0 && viewportHeight > 0) {
            GeometryBuffer* gBuffer = RenderGeometryPrePass(scene, view, projection, viewportWidth, viewportHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

            ssao = RenderSSAOPass(*gBuffer, projection, viewportWidth, viewportHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

            RenderSSAOBlurPass(*ssao);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        }

        // Toda entidade com TransformComponent + MeshRendererComponent,
        // usando a transform de MUNDO (Scene::GetWorldTransform,
        // ancestrais/parenting inclusos).
        //
        // Nome 'meshRendererView' (nao so 'view') para nao colidir com o
        // parametro 'view' (matriz de camera) desta funcao.
        auto meshRendererView = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();
        for (auto entityHandle : meshRendererView) {
            auto& meshRenderer = meshRendererView.get<MeshRendererComponent>(entityHandle);
            glm::mat4 model = scene.GetWorldTransform(Entity(entityHandle, &scene));

            // MaterialComponent e OPCIONAL (uma entidade pode ter so
            // MeshRendererComponent, sem Material nenhum - ver comentario
            // grande em Renderer::DrawMesh/Renderer.h) - quando presente,
            // ganha prioridade sobre MeshRendererComponent::Color (a cor
            // solida "legada" de antes do sistema de Material existir).
            Entity entity(entityHandle, &scene);
            const MaterialComponent* material = entity.HasComponent<MaterialComponent>() ? &entity.GetComponent<MaterialComponent>() : nullptr;

            Mesh* mesh = ResolveMesh(meshRenderer); // primitiva OU modelo importado, ver comentario em ResolveMesh
            DrawMesh(mesh, glm::value_ptr(viewProjection), glm::value_ptr(model), &meshRenderer.Color.x, &lights, shadowMap, shadowCasterLightIndex, ssao, material);
        }
    }

}
