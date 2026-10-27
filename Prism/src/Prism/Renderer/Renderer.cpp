#include <glad/gl.h>
#include <GLFW/glfw3.h> 
#include "Renderer.h"
#include "PrimitiveMeshFactory.h"
#include "../Core/Log.h"
#include "../Core/Profiler.h"
#include "GpuTimer.h"
#include "DrawBatching.h"
#include "../Core/Paths.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Particles/ParticleSystem.h"
#include "../Assets/ModelLoader.h"
#include "../Animation/AnimationSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp> 
#include <algorithm> 
#include <cstring>
#include <map>
#include <tuple>
#include <cmath>
#include <string>    
#include <limits>   
#include <filesystem>
#include <vector>

namespace Prism {

    Ref<Shader> Renderer::s_BasicShader = nullptr;
    Ref<Shader> Renderer::s_LineShader = nullptr;
    Ref<Shader> Renderer::s_ParticleShader = nullptr;
    Ref<Shader> Renderer::s_SkyShader = nullptr;
    SkyMode Renderer::s_SkyMode = EnvironmentComponent{}.Sky;
    float Renderer::s_SkyColor[3] = { EnvironmentComponent{}.SkyColor.x, EnvironmentComponent{}.SkyColor.y, EnvironmentComponent{}.SkyColor.z };
    std::string Renderer::s_SkyTexturePath;
    float Renderer::s_SkyRotation = EnvironmentComponent{}.SkyRotation;
    float Renderer::s_SkyIntensity = EnvironmentComponent{}.SkyIntensity;
    Scope<SkyTexture> Renderer::s_SkyTexture = nullptr;
    std::string Renderer::s_SkyLoadedPath;
    bool Renderer::s_IBLActive = false;
    uint32_t Renderer::s_ParticleSSBO = 0;
    Ref<Shader> Renderer::s_UIShader = nullptr;
    uint32_t Renderer::s_UISSBO = 0;
    Ref<Shader> Renderer::s_GeometryPrePassShader = nullptr;
    Ref<Shader> Renderer::s_SSAOShader = nullptr;
    Ref<Shader> Renderer::s_SSAOBlurShader = nullptr;
    Scope<GeometryBuffer> Renderer::s_GeometryBuffer = nullptr;
    Scope<SSAO> Renderer::s_SSAO = nullptr;
    std::vector<std::pair<::GLFWwindow*, uint32_t>> Renderer::s_FullscreenQuadVAOsByContext;
    std::vector<std::pair<::GLFWwindow*, uint32_t>> Renderer::s_InstanceBuffersByContext;
    Scope<Mesh> Renderer::s_Meshes[5] = {};
    uint32_t Renderer::s_LineVAO = 0;
    uint32_t Renderer::s_LineVBO = 0;
    float Renderer::s_CameraWorldPos[3] = { 0.0f, 0.0f, 0.0f };
    // Estado por quadro (reescrito em DrawScene/ApplyFrameState). Os valores
    // iniciais sao os padroes do EnvironmentComponent/CameraComponent, para
    // um DrawMesh avulso antes do primeiro DrawScene ja sair coerente.
    float Renderer::s_Exposure = CameraComponent{}.Exposure;
    bool Renderer::s_OutputHDR = false;
    bool Renderer::s_SkyNeedsToneMap = false;
    float Renderer::s_Ambient = EnvironmentComponent{}.Ambient;
    float Renderer::s_EnvZenith[3]  = { EnvironmentComponent{}.Zenith.x,  EnvironmentComponent{}.Zenith.y,  EnvironmentComponent{}.Zenith.z };
    float Renderer::s_EnvHorizon[3] = { EnvironmentComponent{}.Horizon.x, EnvironmentComponent{}.Horizon.y, EnvironmentComponent{}.Horizon.z };
    float Renderer::s_EnvGround[3]  = { EnvironmentComponent{}.Ground.x,  EnvironmentComponent{}.Ground.y,  EnvironmentComponent{}.Ground.z };
    std::unordered_map<std::string, Scope<Texture2D>> Renderer::s_TextureCache;
    std::unordered_map<AssetID, Renderer::ModelCacheEntry> Renderer::s_ModelMeshCache;
    std::unordered_map<AssetID, Renderer::RigCacheEntry> Renderer::s_RigCache;

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
        s_BasicShader = loadShader("BasicLit", "basic.vert", "basic.frag", {
            { "MAX_LIGHTS", std::to_string(MAX_LIGHTS) },
            { "CASCADE_COUNT", std::to_string(kShadowCascades) },
            { "MAX_SPOT_SHADOWS", std::to_string(kMaxSpotShadows) },
            { "MAX_POINT_SHADOWS", std::to_string(kMaxPointShadows) } });

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
        s_ParticleShader = loadShader("Particle", "particle.vert", "particle.frag");
        s_SkyShader = loadShader("Sky", "sky.vert", "sky.frag");
        glCreateBuffers(1, &s_ParticleSSBO);
        s_UIShader = loadShader("UI", "ui.vert", "ui.frag");
        glCreateBuffers(1, &s_UISSBO);
        if (!ShadowSystem::Init(shaderDir))
            PRISM_CORE_ERROR("Renderer: shaders de sombra invalidos - a cena sera desenhada sem sombras (corrija shadow_depth/shadow_point; o hot reload os recarrega).");

        if (!EnvironmentIBL::Init(shaderDir))
            PRISM_CORE_ERROR("Renderer: shaders de IBL invalidos - o ambiente usara o gradiente analitico (corrija ibl*.frag; o hot reload os recarrega).");

        s_GeometryPrePassShader = loadShader("GeometryPrePass", "geometry_prepass.vert", "geometry_prepass.frag");
        s_SSAOShader = loadShader("SSAO", "fullscreen_quad.vert", "ssao.frag");
        s_SSAOBlurShader = loadShader("SSAOBlur", "fullscreen_quad.vert", "ssao_blur.frag");
        if (!PostProcess::Init(shaderDir))
            PRISM_CORE_ERROR("Renderer: shaders de pos-processamento invalidos - a cena sera desenhada sem bloom/FXAA/ajustes (corrija os shaders post_*.frag; o hot reload os recarrega).");
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
        for (Ref<Shader>* shader : { &s_BasicShader, &s_LineShader, &s_ParticleShader, &s_UIShader, &s_SkyShader,
                                     &s_GeometryPrePassShader, &s_SSAOShader, &s_SSAOBlurShader }) {
            if (*shader)
                (*shader)->ReloadIfChanged();
        }
        PostProcess::ReloadChangedShaders();
        ShadowSystem::ReloadChangedShaders();
        EnvironmentIBL::ReloadChangedShaders();
    }

    void Renderer::Shutdown() {
        for (auto& mesh : s_Meshes)
            mesh.reset();
        s_BasicShader.reset();
        s_LineShader.reset();
        s_ParticleShader.reset();
        s_UIShader.reset();
        if (s_UISSBO) { glDeleteBuffers(1, &s_UISSBO); s_UISSBO = 0; }
        s_SkyShader.reset();
        s_SkyTexture.reset();
        s_SkyLoadedPath.clear();
        if (s_ParticleSSBO) { glDeleteBuffers(1, &s_ParticleSSBO); s_ParticleSSBO = 0; }
        ShadowSystem::Shutdown();
        EnvironmentIBL::Shutdown();
        s_GeometryPrePassShader.reset();
        s_SSAOShader.reset();
        s_SSAOBlurShader.reset();
        s_GeometryBuffer.reset();
        s_SSAO.reset();
        PostProcess::Shutdown();

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
        for (auto& [window, buffer] : s_InstanceBuffersByContext)
            glDeleteBuffers(1, &buffer);
        s_InstanceBuffersByContext.clear();
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

    Mesh* Renderer::GetOrLoadModelMesh(AssetID modelAsset, int submeshIndex) {
        if (!modelAsset.IsValid())
            return nullptr; // "sem modelo importado" - ver comentario em Renderer.h, sem logar erro

        // Escolhe a malha dentro de uma entrada ja carregada.
        auto pick = [submeshIndex](ModelCacheEntry& entry) -> Mesh* {
            if (entry.Failed || entry.Submeshes.empty())
                return nullptr;
            if (submeshIndex >= 0 && submeshIndex < (int)entry.Submeshes.size())
                return entry.Submeshes[submeshIndex].get();
            if (entry.Submeshes.size() == 1)
                return entry.Submeshes[0].get(); // um unico material: a malha mesclada E o submesh 0

            // Malha mesclada de varios submeshes: construida sob demanda.
            if (!entry.Merged && !entry.MergedTried) {
                entry.MergedTried = true;
                ModelImportResult again = ModelLoader::Load(entry.AbsolutePath);
                if (again.Success)
                    entry.Merged = Mesh::Create(again.Mesh.Vertices, again.Mesh.Indices);
            }
            return entry.Merged.get();
        };

        // find() (nao operator[]) para distinguir "entrada ausente" (nunca
        // tentamos) de "entrada com Failed" (ja tentamos e FALHOU - ver
        // comentario grande em s_ModelMeshCache, Renderer.h) sem criar uma
        // entrada nova so de consultar.
        auto it = s_ModelMeshCache.find(modelAsset);
        if (it != s_ModelMeshCache.end())
            return pick(it->second);

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
            // Refresh() rodar) - reter a falha evita reconsultar o
            // AssetRegistry a cada frame por uma referencia que sabemos
            // estar quebrada agora.
            PRISM_CORE_ERROR("Renderer: MeshRendererComponent::ModelAsset (", modelAsset.ToString(), ") nao resolve para nenhum arquivo - asset apagado ou .meta ausente?");
            s_ModelMeshCache[modelAsset].Failed = true;
            return nullptr;
        }

        ModelImportResult imported = ModelLoader::Load(absolutePath);
        ModelCacheEntry& entry = s_ModelMeshCache[modelAsset];
        entry.AbsolutePath = absolutePath;
        if (!imported.Success) {
            // ModelLoader::Load ja loga o erro detalhado (PRISM_CORE_ERROR,
            // ver ModelLoader.cpp) - aqui so cacheia a falha.
            entry.Failed = true;
            return nullptr;
        }

        entry.Submeshes.reserve(imported.Submeshes.size());
        for (const ImportedSubmesh& submesh : imported.Submeshes)
            entry.Submeshes.push_back(Mesh::Create(submesh.Mesh.Vertices, submesh.Mesh.Indices));

        // O rig do MESMO import (ids de osso dos vertices casam com ele). Se a
        // animacao ja carregou o rig antes, mantem aquele ponteiro (identidade
        // estavel - os Animators comparam por ponteiro).
        RigCacheEntry& rigEntry = s_RigCache[modelAsset];
        if (!rigEntry.Tried) {
            rigEntry.Tried = true;
            rigEntry.Rig = imported.Rig;
        }
        return pick(entry);
    }

    std::shared_ptr<const ModelRig> Renderer::GetOrLoadModelRig(AssetID modelAsset) {
        if (!modelAsset.IsValid())
            return nullptr;
        auto it = s_RigCache.find(modelAsset);
        if (it != s_RigCache.end() && it->second.Tried)
            return it->second.Rig;

        auto project = Project::GetActive();
        if (!project)
            return nullptr; // transitorio: tenta de novo depois (nao cacheia)
        std::filesystem::path absolutePath = project->GetAssetRegistry().AbsolutePath(modelAsset);

        RigCacheEntry& entry = s_RigCache[modelAsset];
        entry.Tried = true;
        if (absolutePath.empty())
            return nullptr; // vinculo quebrado (ja logado por GetOrLoadModelMesh quando desenha)
        ModelImportResult imported = ModelLoader::Load(absolutePath);
        if (imported.Success)
            entry.Rig = imported.Rig;
        return entry.Rig;
    }

    Renderer::BonePalette Renderer::ResolveBonePalette(Scene& scene, entt::entity entity, const MeshRendererComponent& meshRenderer, const Mesh* mesh) {
        if (!mesh || !mesh->IsSkinned() || !meshRenderer.ModelAsset.IsValid())
            return {};

        // Animator proprio ou de um ancestral (do mesmo modelo).
        if (const std::vector<glm::mat4>* palette = AnimationSystem::FindPalette(scene, entity, meshRenderer.ModelAsset))
            if (!palette->empty())
                return { palette->data(), (uint32_t)palette->size() };

        // Sem Animator: pose de bind.
        auto it = s_RigCache.find(meshRenderer.ModelAsset);
        if (it != s_RigCache.end() && it->second.Rig && !it->second.Rig->BindPalette.empty())
            return { it->second.Rig->BindPalette.data(), (uint32_t)it->second.Rig->BindPalette.size() };
        return {};
    }

    void Renderer::InvalidateModelCache(AssetID modelAsset) {
        s_ModelMeshCache.erase(modelAsset);
        s_RigCache.erase(modelAsset);
    }

    Mesh* Renderer::ResolveMesh(const MeshRendererComponent& meshRenderer) {
        if (meshRenderer.ModelAsset.IsValid()) {
            Mesh* modelMesh = GetOrLoadModelMesh(meshRenderer.ModelAsset, meshRenderer.SubmeshIndex);
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

    // Envia u_Skinned/u_Bones (shaders/skinning.glsl). Sem paleta: u_Skinned = 0.
    static void UploadBones(const Shader& shader, const glm::mat4* bones, uint32_t boneCount) {
        const bool skinned = bones && boneCount > 0;
        shader.SetInt("u_Skinned", skinned ? 1 : 0);
        if (skinned)
            shader.SetMat4Array("u_Bones", glm::value_ptr(bones[0]), std::min(boneCount, (uint32_t)kMaxBones));
    }

    void Renderer::DrawMesh(PrimitiveMesh meshType, const float* viewProjection, const float* model, const float* color, const std::vector<GPULight>* lights, const ShadowFrame* shadows, const SSAO* ssao, const MaterialComponent* material) {
        Mesh* mesh = s_Meshes[MeshIndex(meshType)].get();
        DrawMesh(mesh, viewProjection, model, color, lights, shadows, ssao, material);
    }

    // --- Estado do shader principal, em tres camadas -------------------------
    // Antes, DrawMesh reenviava TUDO (ambiente, luzes, sombras, SSAO, texturas) a cada objeto.
    // Os uniforms e as texturas ficam no programa/unidades entre desenhos, entao DrawScene envia
    // o estado do quadro 1x, o do material so quando muda e o do objeto a cada um.

    void Renderer::BindFrameState(const float* viewProjection, const std::vector<GPULight>* lights, const ShadowFrame* shadows, const SSAO* ssao) {
        s_BasicShader->SetMat4("u_ViewProjection", viewProjection);
        s_BasicShader->SetFloat3("u_CameraWorldPos", s_CameraWorldPos[0], s_CameraWorldPos[1], s_CameraWorldPos[2]);
        // Sem este envio, u_Exposure valeria 0.0 (padrao de uniform nao
        // setado em GLSL) e a cena inteira sairia preta - ver shaders/basic.frag.
        s_BasicShader->SetFloat("u_Exposure", s_Exposure);
        s_BasicShader->SetInt("u_OutputMode", s_OutputHDR ? 1 : 0);
        s_BasicShader->SetFloat("u_Ambient", s_Ambient);
        // As tres cores do gradiente de ambiente. Sem estes envios elas
        // valeriam (0,0,0) e o ambiente inteiro (difuso e reflexo dos
        // metais) sairia preto. Ver EnvironmentColor em shaders/basic.frag.
        s_BasicShader->SetFloat3("u_EnvZenith",  s_EnvZenith[0],  s_EnvZenith[1],  s_EnvZenith[2]);
        s_BasicShader->SetFloat3("u_EnvHorizon", s_EnvHorizon[0], s_EnvHorizon[1], s_EnvHorizon[2]);
        s_BasicShader->SetFloat3("u_EnvGround",  s_EnvGround[0],  s_EnvGround[1],  s_EnvGround[2]);
        s_BasicShader->SetFloat("u_EnvScale", ComputeEnvironmentNormalization(s_Ambient, s_EnvZenith, s_EnvHorizon, s_EnvGround));
        // IBL (panorama do ambiente): Ambient/0.10 mantem o slider de Ambient util
        // (0.10 = o brilho natural do panorama), SkyIntensity escala o panorama.
        EnvironmentIBL::Upload(*s_BasicShader, s_IBLActive, s_SkyIntensity * (s_Ambient / 0.10f), glm::radians(s_SkyRotation));

        // 'lights' e opcional (ver comentario em Renderer.h) - sem lista,
        // desenha so com o ambiente fixo do shader (u_LightCount = 0).
        if (lights)
            UploadLights(*lights);
        else
            s_BasicShader->SetInt("u_LightCount", 0);

        // 'shadows' e opcional (nullptr = sem sombra). Texture units 5-7 (ver
        // ShadowSystem.h); 0-1 sao AO e 2-4 o Material.
        if (shadows && shadows->Any())
            ShadowSystem::Upload(*shadows, *s_BasicShader);
        else
            ShadowSystem::UploadNone(*s_BasicShader);

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
    }

    // Cor base: com 'material' fornecido, AlbedoTint manda (ver comentario em Renderer.h) -
    // 'color' (MeshRendererComponent::Color) e ignorado nesse caso.
    void Renderer::SetBaseColor(const MaterialComponent* material, const float* color) {
        if (material)
            s_BasicShader->SetFloat3("u_BaseColor", material->AlbedoTint.x, material->AlbedoTint.y, material->AlbedoTint.z);
        else if (color)
            s_BasicShader->SetFloat3("u_BaseColor", color[0], color[1], color[2]);
        else
            s_BasicShader->SetFloat3("u_BaseColor", 0.85f, 0.55f, 0.2f);
    }

    // Texturas e fatores do Material (slots 2/3/4 - 0/1 sao do AO, 5-7 das sombras).
    // Sem 'material' (nullptr), os tres u_Has*Map ficam false e o fragment shader
    // nunca amostra sampler nenhum.
    void Renderer::BindMaterialState(const MaterialComponent* material) {
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
    }

    void Renderer::BindObjectState(const float* model, const glm::mat4* bones, uint32_t boneCount) {
        s_BasicShader->SetInt("u_Instanced", 0);
        s_BasicShader->SetMat4("u_Model", model);
        // Skinning (ver shaders/skinning.glsl): sem paleta, u_Skinned = 0.
        UploadBones(*s_BasicShader, bones, boneCount);
        // Sem este envio, u_NormalMatrix valeria zero (padrao de uniform
        // nao setado): normal (0,0,0) => normalize() vira NaN no fragment
        // shader e o objeto sai preto. Ver shaders/basic.vert.
        glm::mat3 normalMatrix = ComputeNormalMatrix(glm::make_mat4(model));
        s_BasicShader->SetMat3("u_NormalMatrix", glm::value_ptr(normalMatrix));
    }

    void Renderer::DrawMesh(Mesh* mesh, const float* viewProjection, const float* model, const float* color, const std::vector<GPULight>* lights, const ShadowFrame* shadows, const SSAO* ssao, const MaterialComponent* material, const glm::mat4* bones, uint32_t boneCount) {
        if (!s_BasicShader) return;
        if (!mesh) return; // primitiva ainda nao carregada (nao deveria acontecer apos Init()) OU modelo importado que falhou a carregar (ver GetOrLoadModelMesh) - evita um crash silencioso nos dois casos

        s_BasicShader->Bind();
        BindFrameState(viewProjection, lights, shadows, ssao);
        BindMaterialState(material);
        SetBaseColor(material, color);
        BindObjectState(model, bones, boneCount);

        mesh->BindForCurrentContext();
        glDrawElements(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        Profiler::CountDraw(mesh->GetIndexCount());
        glBindVertexArray(0);

        s_BasicShader->Unbind();
    }

    // --- Instancing -----------------------------------------------------------
    namespace {
        bool s_InstancingEnabled = true;
        constexpr uint32_t kMaxInstancesPerDraw = 1024;
        // Por instancia: mat4 modelo (16) + mat3 normal (9) + cor base (3) = 28 floats.
        // Locais de atributo 8-15, ver shaders/basic.vert.
        constexpr uint32_t kInstanceFloats = 28;
        constexpr GLsizei kInstanceStride = (GLsizei)(kInstanceFloats * sizeof(float));
    }

    void Renderer::SetInstancingEnabled(bool enabled) { s_InstancingEnabled = enabled; }
    bool Renderer::IsInstancingEnabled() { return s_InstancingEnabled; }

    // Um buffer de instancias por contexto GL (mesma tecnica dos VAOs; buffers sao compartilhados
    // na share list, mas assim nao dependemos disso).
    uint32_t Renderer::GetInstanceBufferForCurrentContext() {
        GLFWwindow* current = glfwGetCurrentContext();
        for (auto& [window, buffer] : s_InstanceBuffersByContext)
            if (window == current)
                return buffer;
        uint32_t buffer = 0;
        glCreateBuffers(1, &buffer);
        s_InstanceBuffersByContext.emplace_back(current, buffer);
        return buffer;
    }

    void Renderer::SetCameraPosition(const float* worldPos) {
        s_CameraWorldPos[0] = worldPos[0];
        s_CameraWorldPos[1] = worldPos[1];
        s_CameraWorldPos[2] = worldPos[2];
    }

    // Reescreve o estado por quadro (exposicao, ambiente, fundo) a partir da
    // cena e da camera. Tudo e SANEADO aqui: o dado pode vir de um arquivo
    // editado a mao ou de um script, e NaN/inf/0 deixariam a cena preta ou
    // piscando sem nenhuma pista do motivo.
    void Renderer::ApplyFrameState(Scene& scene, const CameraRenderSettings& camera) {
        auto finiteOr = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto clamp01 = [&](float v, float fallback) { return std::min(std::max(finiteOr(v, fallback), 0.0f), 1.0f); };

        // Exposicao <= 0 zeraria a imagem inteira: cai para o neutro.
        float exposure = finiteOr(camera.Exposure, 1.0f);
        s_Exposure = std::min(std::max(exposure, 0.05f), 16.0f);

        // Primeira entidade com EnvironmentComponent; sem nenhuma, padroes.
        EnvironmentComponent env;
        {
            auto envView = scene.GetRegistry().view<EnvironmentComponent>();
            auto it = envView.begin();
            if (it != envView.end())
                env = envView.get<EnvironmentComponent>(*it);
        }

        s_Ambient = std::min(std::max(finiteOr(env.Ambient, 0.10f), 0.0f), 2.0f);
        const glm::vec3 defaults[3] = { EnvironmentComponent{}.Zenith, EnvironmentComponent{}.Horizon, EnvironmentComponent{}.Ground };
        const glm::vec3* sources[3] = { &env.Zenith, &env.Horizon, &env.Ground };
        float* targets[3] = { s_EnvZenith, s_EnvHorizon, s_EnvGround };
        for (int c = 0; c < 3; c++)
            for (int i = 0; i < 3; i++)
                targets[c][i] = clamp01((*sources[c])[i], defaults[c][i]);

        // Fundo. Camera com cor chapada ignora o skybox da cena (mas o panorama
        // continua iluminando a cena via IBL).
        if (camera.ClearMode == CameraClearMode::SolidColor) {
            s_SkyMode = SkyMode::Solid;
            for (int i = 0; i < 3; i++)
                s_SkyColor[i] = clamp01(camera.ClearColor[i], 0.0f);
        } else {
            int mode = (int)env.Sky;
            s_SkyMode = (mode >= 0 && mode <= (int)SkyMode::Panorama) ? env.Sky : SkyMode::Solid;
            for (int i = 0; i < 3; i++)
                s_SkyColor[i] = clamp01(env.SkyColor[i], 0.0f);
        }
        s_SkyTexturePath.clear();
        if (env.Sky == SkyMode::Panorama && env.SkyTexture.IsValid()) {
            if (auto project = Project::GetActive())
                s_SkyTexturePath = project->GetAssetRegistry().AbsolutePath(env.SkyTexture).string();
        }
        s_SkyRotation = std::min(std::max(finiteOr(env.SkyRotation, 0.0f), -180.0f), 180.0f);
        s_SkyIntensity = std::min(std::max(finiteOr(env.SkyIntensity, 1.0f), 0.0f), 16.0f);
    }

    // Fundo da cena: um triangulo de tela cheia no plano distante, desenhado ANTES
    // da geometria (sem teste nem escrita de profundidade) - ver sky.vert/sky.frag.
    SkyTexture* Renderer::ResolvePanorama() {
        if (s_SkyLoadedPath != s_SkyTexturePath) {
            s_SkyLoadedPath = s_SkyTexturePath; // tenta UMA vez por caminho (falha nao repete o erro todo frame)
            s_SkyTexture.reset();
            if (!s_SkyTexturePath.empty())
                s_SkyTexture = CreateScope<SkyTexture>(s_SkyTexturePath);
        }
        return (s_SkyTexture && s_SkyTexture->IsValid()) ? s_SkyTexture.get() : nullptr;
    }

    void Renderer::DrawSky(const float* view, const float* projection, const float* cameraWorldPos) {
        if (!s_SkyShader)
            return;

        // Modo efetivo: Panorama sem textura valida cai para cor chapada.
        int mode = (int)s_SkyMode;
        SkyTexture* panorama = nullptr;
        if (s_SkyMode == SkyMode::Panorama) {
            panorama = ResolvePanorama();
            if (!panorama)
                mode = (int)SkyMode::Solid;
        }

        glm::mat4 viewMatrix = glm::make_mat4(view);
        glm::mat4 projectionMatrix = glm::make_mat4(projection);
        glm::mat4 inverseViewProjection = glm::inverse(projectionMatrix * viewMatrix);

        bool depthWasEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        bool cullWasEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        bool blendWasEnabled = glIsEnabled(GL_BLEND) == GL_TRUE;
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        s_SkyShader->Bind();
        s_SkyShader->SetMat4("u_InverseViewProjection", glm::value_ptr(inverseViewProjection));
        s_SkyShader->SetFloat3("u_CameraPosition", cameraWorldPos[0], cameraWorldPos[1], cameraWorldPos[2]);
        s_SkyShader->SetInt("u_Mode", mode);
        s_SkyShader->SetFloat3("u_SolidColor", s_SkyColor[0], s_SkyColor[1], s_SkyColor[2]);
        s_SkyShader->SetFloat3("u_EnvZenith", s_EnvZenith[0], s_EnvZenith[1], s_EnvZenith[2]);
        s_SkyShader->SetFloat3("u_EnvHorizon", s_EnvHorizon[0], s_EnvHorizon[1], s_EnvHorizon[2]);
        s_SkyShader->SetFloat3("u_EnvGround", s_EnvGround[0], s_EnvGround[1], s_EnvGround[2]);
        s_SkyShader->SetFloat("u_Rotation", glm::radians(s_SkyRotation));
        s_SkyShader->SetFloat("u_Intensity", s_SkyIntensity);
        s_SkyShader->SetFloat("u_Exposure", s_Exposure);
        s_SkyShader->SetInt("u_ToneMap", (panorama && panorama->IsHDR()) ? 1 : 0);
        s_SkyShader->SetInt("u_OutputMode", s_OutputHDR ? 1 : 0);
        s_SkyNeedsToneMap = panorama && panorama->IsHDR();
        s_SkyShader->SetInt("u_Panorama", 0);
        if (panorama)
            panorama->Bind(0);

        glBindVertexArray(GetFullscreenQuadVAOForCurrentContext()); // VAO vazio: o triangulo nasce no shader
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        s_SkyShader->Unbind();

        glDepthMask(GL_TRUE);
        if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
        if (cullWasEnabled) glEnable(GL_CULL_FACE);
        if (blendWasEnabled) glEnable(GL_BLEND);
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
            gpuLight.CastShadows = light.CastShadows;

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

    void Renderer::UploadLights(const std::vector<GPULight>& lights) {
        if (!s_BasicShader) return;

        int count = (int)std::min<size_t>(lights.size(), MAX_LIGHTS);
        s_BasicShader->SetInt("u_LightCount", count);

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
            s_BasicShader->SetInt(prefix + "ShadowSlot", light.CastShadows ? light.ShadowSlot : -1);
        }
    }


    // ========================================================================
    // RenderGeometryPrePass / RenderSSAOPass / RenderSSAOBlurPass
    // Ver comentario grande em SSAO.h para o pipeline completo. Ordem de
    // chamada fixa (imposta por DrawScene, nao pelos proprios metodos):
    // geometria -> SSAO bruto -> blur.
    // ========================================================================

    namespace {
        Renderer::CullStats s_CullStats;

        // Esta malha entra na imagem desta camera? Camadas primeiro, depois frustum.
        // Malha com esqueleto (a pose pode sair da caixa local) nunca e descartada pelo frustum.
        enum class Visibility { Visible, LayerHidden, OutsideFrustum };
        Visibility TestVisibility(const MeshRendererComponent& renderer, const Mesh* mesh, const glm::mat4& model,
                                  const Culling::Frustum& frustum, uint32_t cullMask) {
            if (!Culling::LayerVisible(renderer.Layers, cullMask))
                return Visibility::LayerHidden;
            if (!mesh || mesh->IsSkinned())
                return Visibility::Visible;
            const glm::vec3 lo = mesh->GetLocalBoundsMin(), hi = mesh->GetLocalBoundsMax();
            const float localMin[3] = { lo.x, lo.y, lo.z }, localMax[3] = { hi.x, hi.y, hi.z };
            return frustum.IntersectsLocalBox(localMin, localMax, glm::value_ptr(model)) ? Visibility::Visible : Visibility::OutsideFrustum;
        }
    }

    const Renderer::CullStats& Renderer::GetCullStats() { return s_CullStats; }

    GeometryBuffer* Renderer::RenderGeometryPrePass(Scene& scene, const float* view, const float* projection, uint32_t width, uint32_t height, uint32_t cullMask) {
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

        const Culling::Frustum frustum = Culling::Frustum::FromViewProjection(glm::value_ptr(viewProjection));
        auto meshView = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();
        for (auto entityHandle : meshView) {
            auto& meshRenderer = meshView.get<MeshRendererComponent>(entityHandle);
            glm::mat4 model = scene.GetWorldTransform(Entity(entityHandle, &scene));
            Mesh* prePassMesh = ResolveMesh(meshRenderer);
            if (!prePassMesh || TestVisibility(meshRenderer, prePassMesh, model, frustum, cullMask) != Visibility::Visible)
                continue; // fora da imagem (camada ou frustum): nao ocupa o SSAO
            s_GeometryPrePassShader->SetMat4("u_Model", glm::value_ptr(model));
            {
                // View-space: a matriz normal e a de (view * model), nao so
                // a do model - a normal chega ao SSAO ja em view-space.
                glm::mat3 viewNormalMatrix = ComputeNormalMatrix(glm::make_mat4(view) * model);
                s_GeometryPrePassShader->SetMat3("u_ViewNormalMatrix", glm::value_ptr(viewNormalMatrix));
            }

            Mesh* mesh = prePassMesh;
            BonePalette palette = ResolveBonePalette(scene, entityHandle, meshRenderer, mesh);
            UploadBones(*s_GeometryPrePassShader, palette.Data, palette.Count);
            mesh->BindForCurrentContext();
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
            Profiler::CountDraw(mesh->GetIndexCount());
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

    void Renderer::DrawScene(Scene& scene, const float* view, const float* projection, const float* cameraWorldPos, const CameraRenderSettings& camera) {
        PRISM_PROFILE_SCOPE("Renderer.DrawScene");
        GpuTimer::Scope gpuScene("GPU.DrawScene");
        SetCameraPosition(cameraWorldPos);
        ApplyFrameState(scene, camera);

        // IBL: com panorama valido no ambiente, gera (uma vez por panorama) os mapas de
        // irradiancia/especular; sem ele, o gradiente analitico continua valendo.
        s_IBLActive = false;
        if (!s_SkyTexturePath.empty()) {
            if (SkyTexture* panorama = ResolvePanorama())
                s_IBLActive = EnvironmentIBL::Update(*panorama, s_SkyTexturePath);
        } else {
            EnvironmentIBL::Invalidate();
        }

        glm::mat4 viewProjection = glm::make_mat4(projection) * glm::make_mat4(view);

        // Camadas e frustum DESTA camera. Com layout de CameraRenderSettings antigo
        // (build desatualizada, ver LayoutMagic) a mascara e lixo: enxerga tudo.
        const uint32_t cullMask = (camera.LayoutMagic == CameraRenderSettings::kLayoutMagic) ? camera.CullMask : Culling::kAllLayers;
        const Culling::Frustum cameraFrustum = Culling::Frustum::FromViewProjection(glm::value_ptr(viewProjection));
        s_CullStats = CullStats{};

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

        // Sombras: casters = toda entidade com mesh; ShadowSystem desenha os mapas
        // (cascatas / spot / cubo) e preenche lights[i].ShadowSlot.
        std::vector<ShadowCaster> casters;
        {
            bool anyShadowLight = false;
            for (const GPULight& l : lights)
                anyShadowLight = anyShadowLight || l.CastShadows;
            if (anyShadowLight) {
                auto casterView = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();
                for (auto handle : casterView) {
                    const MeshRendererComponent& casterRenderer = casterView.get<MeshRendererComponent>(handle);
                    Mesh* geometry = ResolveMesh(casterRenderer);
                    if (!geometry) continue;
                    ShadowCaster caster;
                    caster.Geometry = geometry;
                    caster.Model = scene.GetWorldTransform(Entity(handle, &scene));
                    BonePalette palette = ResolveBonePalette(scene, handle, casterRenderer, geometry);
                    caster.Bones = palette.Data;
                    caster.BoneCount = palette.Count;
                    casters.push_back(caster);
                }
            }
        }
        Profiler::Scope zShadows("Renderer.Shadows");
        GpuTimer::Scope gShadows("GPU.Shadows");
        const ShadowFrame& shadowFrame = ShadowSystem::Render(casters, lights, view, projection);
        gShadows.Stop();
        zShadows.Stop();
        const ShadowFrame* shadows = shadowFrame.Any() ? &shadowFrame : nullptr;

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

        // --- SSAO: pre-pass de geometria (camera principal) + calculo +
        // blur - ver comentario grande em SSAO.h. So roda quando a
        // viewport tem tamanho valido (largura/altura > 0) - pode ser 0
        // por um frame quando o painel esta sendo redimensionado/oculto.
        // Desligavel por camera (CameraComponent::SSAO): sem AO, ssao fica
        // nullptr e o DrawMesh usa oclusao neutra (ver u_HasSSAO).
        Profiler::Scope zSSAO("Renderer.SSAO");
        GpuTimer::Scope gSSAO("GPU.SSAO");
        SSAO* ssao = nullptr;
        if (camera.SSAO && viewportWidth > 0 && viewportHeight > 0) {
            GeometryBuffer* gBuffer = RenderGeometryPrePass(scene, view, projection, viewportWidth, viewportHeight, cullMask);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

            ssao = RenderSSAOPass(*gBuffer, projection, viewportWidth, viewportHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

            RenderSSAOBlurPass(*ssao);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        }

        gSSAO.Stop();
        zSSAO.Stop();

        // Limpa cor E PROFUNDIDADE do destino antes de desenhar. O fundo (sky)
        // cobre a cor, mas NAO escreve profundidade: sem limpar a profundidade,
        // o valor do quadro anterior fica no framebuffer e a geometria falha no
        // teste de profundidade (cena "invisivel" com so o fundo aparecendo).
        // Chamadores nao precisam mais de Renderer::Clear antes. A mascara de
        // profundidade precisa estar ligada, senao glClear nao limpa a profundidade.
        //
        // POS-PROCESSAMENTO: com viewport valida a cena vai para o buffer HDR
        // (linear) e PostProcess::End a converte para o destino original no fim
        // (bloom, exposicao + ACES, ajustes, FXAA). Se nao der (tamanho
        // invalido, shaders quebrados) desenha direto, como antes.
        PostProcess* postProcess = nullptr;
        if (viewportWidth > 0 && viewportHeight > 0) {
            PostProcess& candidate = PostProcess::ForCurrentContext();
            if (candidate.Begin(viewportWidth, viewportHeight))
                postProcess = &candidate;
        }
        s_OutputHDR = (postProcess != nullptr);
        s_SkyNeedsToneMap = false;

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Fundo antes da geometria (preenche a tela inteira).
        DrawSky(view, projection, cameraWorldPos);

        // Toda entidade com TransformComponent + MeshRendererComponent,
        // usando a transform de MUNDO (Scene::GetWorldTransform,
        // ancestrais/parenting inclusos).
        //
        // Nome 'meshRendererView' (nao so 'view') para nao colidir com o
        // parametro 'view' (matriz de camera) desta funcao.
        auto meshRendererView = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();

        // 1) Coleta os objetos visiveis. Os desenhos sao feitos DEPOIS, agrupados por material e
        //    malha (DrawBatching.h) para enviar o estado do material so quando muda e para
        //    desenhar copias iguais numa chamada instanciada so.
        struct PendingDraw {
            Mesh* MeshPtr = nullptr;
            const MaterialComponent* Material = nullptr;
            glm::mat4 Model{ 1.0f };
            glm::vec3 Color{ 0.85f, 0.55f, 0.2f };
            BonePalette Palette;
        };
        // Chave do ESTADO de material (cor base nao entra: e por objeto/instancia).
        struct MaterialKey {
            bool Has = false;
            std::string Albedo, Normal, RoughnessMetallic;
            float Roughness = 1.0f, Metallic = 0.0f;
            bool operator<(const MaterialKey& o) const {
                return std::tie(Has, Albedo, Normal, RoughnessMetallic, Roughness, Metallic)
                     < std::tie(o.Has, o.Albedo, o.Normal, o.RoughnessMetallic, o.Roughness, o.Metallic);
            }
        };
        std::vector<PendingDraw> pending;
        std::vector<Batching::Item> batchItems;
        std::map<MaterialKey, uint32_t> materialIds;

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
            switch (TestVisibility(meshRenderer, mesh, model, cameraFrustum, cullMask)) {
                case Visibility::LayerHidden:     s_CullStats.LayerHidden++;     continue;
                case Visibility::OutsideFrustum:  s_CullStats.FrustumCulled++;   continue;
                case Visibility::Visible:         break;
            }
            s_CullStats.Drawn++;
            if (!mesh)
                continue; // modelo que falhou ao carregar: nada a desenhar (DrawMesh tambem ignorava)

            PendingDraw draw;
            draw.MeshPtr = mesh;
            draw.Material = material;
            draw.Model = model;
            draw.Color = glm::vec3(meshRenderer.Color.x, meshRenderer.Color.y, meshRenderer.Color.z);
            if (material)
                draw.Color = glm::vec3(material->AlbedoTint.x, material->AlbedoTint.y, material->AlbedoTint.z);
            draw.Palette = ResolveBonePalette(scene, entityHandle, meshRenderer, mesh);

            MaterialKey key;
            if (material) {
                key.Has = true;
                key.Albedo = material->AlbedoPath;
                key.Normal = material->NormalPath;
                key.RoughnessMetallic = material->RoughnessMetallicPath;
                key.Roughness = glm::clamp(material->RoughnessFactor, 0.0f, 1.0f);
                key.Metallic = glm::clamp(material->MetallicFactor, 0.0f, 1.0f);
            }
            auto inserted = materialIds.emplace(std::move(key), (uint32_t)materialIds.size());

            Batching::Item item;
            item.Mesh = mesh;
            item.MaterialId = inserted.first->second;
            item.Skinned = draw.Palette.Data && draw.Palette.Count > 0;
            item.Index = (uint32_t)pending.size();
            batchItems.push_back(item);
            pending.push_back(draw);
        }

        // 2) Desenha por lotes.
        if (s_BasicShader && !pending.empty()) {
            const std::vector<Batching::Batch> batches = Batching::Build(batchItems, s_InstancingEnabled, kMaxInstancesPerDraw);

            s_BasicShader->Bind();
            BindFrameState(glm::value_ptr(viewProjection), &lights, shadows, ssao);

            uint32_t currentMaterialId = UINT32_MAX;
            std::vector<float> instanceData;
            for (const Batching::Batch& batch : batches) {
                const Batching::Item& first = batchItems[batch.First];
                const PendingDraw& firstDraw = pending[first.Index];
                if (first.MaterialId != currentMaterialId) {
                    BindMaterialState(firstDraw.Material);
                    currentMaterialId = first.MaterialId;
                }

                if (!batch.Instanced) {
                    const PendingDraw& draw = firstDraw;
                    SetBaseColor(nullptr, &draw.Color.x); // a cor (tint do material ou cor do renderer) ja foi resolvida na coleta
                    BindObjectState(glm::value_ptr(draw.Model), draw.Palette.Data, draw.Palette.Count);
                    draw.MeshPtr->BindForCurrentContext();
                    glDrawElements(GL_TRIANGLES, (GLsizei)draw.MeshPtr->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
                    Profiler::CountDraw(draw.MeshPtr->GetIndexCount());
                    glBindVertexArray(0);
                    continue;
                }

                // Lote instanciado: modelo + matriz normal + cor de cada instancia num buffer.
                instanceData.resize(batch.Count * kInstanceFloats);
                for (size_t k = 0; k < batch.Count; k++) {
                    const PendingDraw& draw = pending[batchItems[batch.First + k].Index];
                    const glm::mat3 normalMatrix = ComputeNormalMatrix(draw.Model);
                    float* out = &instanceData[k * kInstanceFloats];
                    std::memcpy(out, glm::value_ptr(draw.Model), 16 * sizeof(float));
                    std::memcpy(out + 16, glm::value_ptr(normalMatrix), 9 * sizeof(float));
                    out[25] = draw.Color.x; out[26] = draw.Color.y; out[27] = draw.Color.z;
                }
                const uint32_t instanceBuffer = GetInstanceBufferForCurrentContext();
                glNamedBufferData(instanceBuffer, (GLsizeiptr)(instanceData.size() * sizeof(float)), instanceData.data(), GL_STREAM_DRAW);

                s_BasicShader->SetInt("u_Instanced", 1);
                s_BasicShader->SetInt("u_Skinned", 0);
                firstDraw.MeshPtr->BindForCurrentContext();
                glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
                for (int column = 0; column < 4; column++) { // mat4 = 4 atributos vec4
                    glEnableVertexAttribArray(8 + column);
                    glVertexAttribPointer(8 + column, 4, GL_FLOAT, GL_FALSE, kInstanceStride, (const void*)(uintptr_t)(column * 4 * sizeof(float)));
                    glVertexAttribDivisor(8 + column, 1);
                }
                for (int column = 0; column < 3; column++) { // mat3 = 3 atributos vec3
                    glEnableVertexAttribArray(12 + column);
                    glVertexAttribPointer(12 + column, 3, GL_FLOAT, GL_FALSE, kInstanceStride, (const void*)(uintptr_t)((16 + column * 3) * sizeof(float)));
                    glVertexAttribDivisor(12 + column, 1);
                }
                glEnableVertexAttribArray(15);
                glVertexAttribPointer(15, 3, GL_FLOAT, GL_FALSE, kInstanceStride, (const void*)(uintptr_t)(25 * sizeof(float)));
                glVertexAttribDivisor(15, 1);

                glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)firstDraw.MeshPtr->GetIndexCount(), GL_UNSIGNED_INT, nullptr, (GLsizei)batch.Count);
                Profiler::CountDraw(firstDraw.MeshPtr->GetIndexCount(), (uint32_t)batch.Count);

                // O VAO e da malha (compartilhado com os desenhos normais): devolve ao estado original.
                for (int location = 8; location <= 15; location++) {
                    glVertexAttribDivisor(location, 0);
                    glDisableVertexAttribArray(location);
                }
                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                s_BasicShader->SetInt("u_Instanced", 0);
            }
            s_BasicShader->Unbind();
        }

        // Particulas por ultimo: blend + sem escrita de profundidade, testando
        // contra a profundidade da geometria opaca ja desenhada.
        {
            PRISM_PROFILE_SCOPE("Renderer.Particles");
            GpuTimer::Scope gParticles("GPU.Particles");
            DrawParticles(scene, view, projection);
        }

        if (postProcess) {
            s_OutputHDR = false;

            auto finiteOr = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
            // Guarda de layout (ver CameraRenderSettings::LayoutMagic): objeto
            // vindo de um .cpp compilado com o header antigo -> pos-processamento neutro.
            const bool layoutOk = (camera.LayoutMagic == CameraRenderSettings::kLayoutMagic);
            static bool s_WarnedLayout = false;
            if (!layoutOk && !s_WarnedLayout) {
                s_WarnedLayout = true;
                PRISM_CORE_ERROR("Renderer: CameraRenderSettings com layout antigo - a build esta DESATUALIZADA (algum .cpp nao foi recompilado depois de mudar Renderer.h/Components.h). Faca 'Rebuild All' / apague a pasta de build. Usando pos-processamento neutro.");
            }
            const CameraRenderSettings neutral;
            const CameraRenderSettings& source = layoutOk ? camera : neutral;

            PostProcessSettings post;
            post.Bloom = source.Bloom;
            post.BloomThreshold = std::min(std::max(finiteOr(source.BloomThreshold, 1.0f), 0.0f), 32.0f);
            post.BloomIntensity = std::min(std::max(finiteOr(source.BloomIntensity, 0.15f), 0.0f), 4.0f);
            post.FXAA = source.FXAA;
            post.Exposure = s_Exposure; // ja limitada em ApplyFrameState
            post.Saturation = std::min(std::max(finiteOr(source.Saturation, 1.0f), 0.0f), 3.0f);
            post.Contrast = std::min(std::max(finiteOr(source.Contrast, 1.0f), 0.1f), 3.0f);
            post.SkyPassThrough = !s_SkyNeedsToneMap;
            postProcess->End(post, (uint32_t)previousFramebuffer, previousViewport);
        }
        s_OutputHDR = false;
    }

    void Renderer::DrawUIQuads(const UIQuad* quads, uint32_t count, float screenWidth, float screenHeight,
                               const Texture2D* texture, uint32_t fontTexture) {
        if (!s_UIShader || s_UISSBO == 0 || count == 0 || screenWidth <= 0.0f || screenHeight <= 0.0f)
            return;

        bool cullWasEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        bool depthWasEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(GetFullscreenQuadVAOForCurrentContext()); // VAO vazio: o quad nasce no shader
        s_UIShader->Bind();
        s_UIShader->SetFloat2("u_ScreenSize", screenWidth, screenHeight);
        if (texture && texture->IsValid())
            texture->Bind(0);
        if (fontTexture != 0)
            glBindTextureUnit(1, fontTexture);

        glNamedBufferData(s_UISSBO, (GLsizeiptr)(count * sizeof(UIQuad)), quads, GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_UISSBO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);

        glBindVertexArray(0);
        s_UIShader->Unbind();
        glDisable(GL_BLEND);
        if (depthWasEnabled)
            glEnable(GL_DEPTH_TEST);
        if (cullWasEnabled)
            glEnable(GL_CULL_FACE);
    }

    void Renderer::DrawParticles(Scene& scene, const float* view, const float* projection) {
        if (!s_ParticleShader || s_ParticleSSBO == 0)
            return;

        auto emitters = scene.GetRegistry().view<ParticleEmitterComponent>();
        if (emitters.begin() == emitters.end())
            return;

        glm::mat4 viewMatrix = glm::make_mat4(view);
        glm::mat4 viewProjection = glm::make_mat4(projection) * viewMatrix;
        // Eixos da camera no mundo = linhas da matriz de view (rotacao ortonormal).
        glm::vec3 cameraRight(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
        glm::vec3 cameraUp(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
        glm::vec3 cameraPosition(s_CameraWorldPos[0], s_CameraWorldPos[1], s_CameraWorldPos[2]);

        struct GPUParticle {
            glm::vec4 PositionSize;
            glm::vec4 Color;
        };
        std::vector<GPUParticle> gpuParticles;
        std::vector<std::pair<float, uint32_t>> order; // (distancia^2, indice) para ordenar o blend normal

        bool cullWasEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        bool shaderBound = false;

        for (auto handle : emitters) {
            const std::vector<Particle>* particles = ParticleSystem::GetParticles(scene, handle);
            if (!particles || particles->empty())
                continue;
            const auto& emitter = emitters.get<ParticleEmitterComponent>(handle);

            if (!shaderBound) {
                glEnable(GL_BLEND);
                glDepthMask(GL_FALSE);
                glDisable(GL_CULL_FACE);
                glBindVertexArray(GetFullscreenQuadVAOForCurrentContext()); // VAO vazio: o quad nasce no shader
                s_ParticleShader->Bind();
                s_ParticleShader->SetInt("u_OutputMode", s_OutputHDR ? 1 : 0);
                s_ParticleShader->SetMat4("u_ViewProjection", glm::value_ptr(viewProjection));
                s_ParticleShader->SetFloat3("u_CameraRight", cameraRight.x, cameraRight.y, cameraRight.z);
                s_ParticleShader->SetFloat3("u_CameraUp", cameraUp.x, cameraUp.y, cameraUp.z);
                s_ParticleShader->SetInt("u_Texture", 0);
                shaderBound = true;
            }

            // Ordem de desenho: aditivo nao depende dela; alpha normal vai de tras para frente.
            order.clear();
            order.reserve(particles->size());
            for (uint32_t i = 0; i < (uint32_t)particles->size(); i++) {
                glm::vec3 delta = (*particles)[i].Position - cameraPosition;
                order.emplace_back(glm::dot(delta, delta), i);
            }
            if (!emitter.Additive)
                std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

            gpuParticles.clear();
            gpuParticles.reserve(order.size());
            for (const auto& entry : order) {
                const Particle& particle = (*particles)[entry.second];
                float t = std::min(std::max(particle.Age / particle.Lifetime, 0.0f), 1.0f);
                float size = emitter.SizeStart + (emitter.SizeEnd - emitter.SizeStart) * t;
                glm::vec4 color = glm::mix(emitter.ColorStart, emitter.ColorEnd, t);
                gpuParticles.push_back({ glm::vec4(particle.Position, size), color });
            }

            glNamedBufferData(s_ParticleSSBO, (GLsizeiptr)(gpuParticles.size() * sizeof(GPUParticle)), gpuParticles.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_ParticleSSBO);

            if (emitter.Additive)
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            else
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            Texture2D* texture = GetOrLoadTexture(emitter.TexturePath, false);
            bool useTexture = texture && texture->IsValid();
            if (useTexture)
                texture->Bind(0);
            s_ParticleShader->SetInt("u_UseTexture", useTexture ? 1 : 0);

            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)gpuParticles.size());
        }

        if (shaderBound) {
            glBindVertexArray(0);
            s_ParticleShader->Unbind();
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (cullWasEnabled)
                glEnable(GL_CULL_FACE);
        }
    }

}
