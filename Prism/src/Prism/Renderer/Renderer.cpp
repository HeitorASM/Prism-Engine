#include <glad/gl.h>
#include "Renderer.h"
#include "PrimitiveMeshFactory.h"
#include "../Core/Log.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp> // glm::lookAt, glm::ortho (RenderShadowPass)
#include <algorithm> // std::min (UploadLights)
#include <string>    // std::to_string (UploadLights - nomes de uniform por indice)
#include <limits>    // std::numeric_limits (RenderShadowPass - bounding box da cena)

namespace Prism {

    Ref<Shader> Renderer::s_BasicShader = nullptr;
    Ref<Shader> Renderer::s_LineShader = nullptr;
    Ref<Shader> Renderer::s_ShadowDepthShader = nullptr;
    Scope<ShadowMap> Renderer::s_ShadowMap = nullptr;
    uint32_t Renderer::s_ShadowCasterEntityId = 0;
    bool Renderer::s_HasShadowCasterEntity = false;
    Scope<Mesh> Renderer::s_Meshes[5] = {};
    uint32_t Renderer::s_LineVAO = 0;
    uint32_t Renderer::s_LineVBO = 0;
    float Renderer::s_CameraWorldPos[3] = { 0.0f, 0.0f, 0.0f };

    // Shader minimo: posicao + normal, iluminacao direcional simples "fake"
    // (um unico dot product) so para as primitivas nao parecerem uma
    // silhueta plana sem nenhuma pista de profundidade/forma.
    //
    // Tambem descarta (discard) o fragmento quando a normal do triangulo
    // esta de costas para a camera (dot(normal, viewDir) < 0) - isto e, a
    // FACE INTERNA de qualquer mesh fechado (Cube/Sphere/Capsule/Cylinder)
    // fica transparente. Isso resolve o problema de uma camera (ou
    // qualquer outra coisa) posicionada DENTRO de um mesh fechado (ex: uma
    // CameraComponent dentro da capsula de colisao de um Character): em
    // vez de enxergar a face interna solida do mesh (o que parece um erro
    // visual, ja que a maioria das engines faz backface culling e nunca
    // desenha essa face), a face de dentro simplesmente nao e desenhada e
    // a camera ve direto para o resto da cena.
    //
    // Deliberadamente feito no FRAGMENT SHADER via dot product (em vez de
    // glCullFace(GL_BACK) no lado C++) porque as primitivas hoje NAO tem
    // winding order (CW/CCW) consistente entre si - Cube e CCW visto de
    // fora, mas Sphere/Capsule/Cylinder (gerados por UV-mapping
    // lat/lon, ver PrimitiveMeshFactory) saem com o winding oposto.
    // Ligar culling por winding no pipeline faria essas primitivas
    // sumirem inteiras (ou mostrarem so a face errada) em vez de so
    // esconder a face interna. Testar dot(normal, viewDir) funciona
    // igual independente do winding - e mais barato para a GPU calcular
    // culling por indice, mas para o tamanho de cena que este prototipo
    // desenha hoje a diferenca de custo e desprezivel. Uma correcao futura
    // (normalizar o winding de cada PrimitiveMeshFactory::Create* e trocar
    // para glCullFace) fica registrada no README como proximo passo
    // opcional de performance, nao como bug.
    static const char* s_VertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Model;

        out vec3 v_Normal;
        out vec3 v_WorldPos;

        void main() {
            v_Normal = mat3(u_Model) * a_Normal;
            vec4 worldPos = u_Model * vec4(a_Position, 1.0);
            v_WorldPos = worldPos.xyz;
            gl_Position = u_ViewProjection * worldPos;
        }
    )";

    // Shader "depth-only" para o PRIMEIRO pass do shadow mapping (ver
    // RenderShadowPass abaixo): desenha a cena inteira do ponto de vista
    // da luz, sem cor nenhuma - so preenche o depth buffer do ShadowMap
    // (ver ShadowMap.h/.cpp). O vertex shader e a unica parte que
    // realmente importa (transforma a posicao pela matriz da LUZ, nao da
    // camera); o fragment shader existe vazio so porque OpenGL exige um
    // fragment stage linkado no programa - a GPU escreve gl_FragDepth
    // (profundidade do triangulo rasterizado) automaticamente mesmo sem
    // nenhuma instrucao explicita nele.
    static const char* s_ShadowDepthVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        // location = 1 (a_Normal) existe no Mesh (ver Mesh.h) mas nao e
        // lido aqui - o layout do vertex buffer e o mesmo de s_VertexSrc
        // (Mesh e compartilhado entre os dois shaders), so os atributos
        // usados diferem.

        uniform mat4 u_LightSpaceMatrix; // Projection * View DA LUZ, nao da camera
        uniform mat4 u_Model;

        void main() {
            gl_Position = u_LightSpaceMatrix * u_Model * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_ShadowDepthFragmentSrc = R"(
        #version 450 core
        void main() {
            // Vazio de proposito - ver comentario acima de
            // s_ShadowDepthVertexSrc.
        }
    )";

    // Shader multi-luz: substitui a antiga luz direcional fake hardcoded
    // por um array de ate MAX_LIGHTS luzes de verdade, vindas de
    // LightComponent (ver Components.h) atraves de Renderer::CollectGPULights
    // + UploadLights. MAX_LIGHTS aqui e uma constante GLSL espelhando
    // Prism::MAX_LIGHTS (Renderer.h) - os dois DEVEM bater.
    //
    // Cada luz tem um campo inteiro 'Type' que decide qual formula de
    // atenuacao/cone usar (ver funcao CalculateLight abaixo) - igual o
    // enum Prism::LightType (Point=0, Spot=1, Directional=2, ...).
    // Adicionar um novo tipo (ex: Area) significa: adicionar mais um
    // "else if (light.Type == N)" aqui, SEM mudar a struct GPULight nem
    // o array de uniforms - ver comentario grande sobre extensibilidade
    // em Components.h/Renderer.h.
    //
    // Modelo de iluminacao continua Lambert simples (sem especular, sem
    // PBR) - propositalmente, para bater com o nivel de fidelidade do
    // resto do renderer neste estagio do prototipo (ver README). Trocar
    // por um modelo mais realista (Blinn-Phong, PBR/Cook-Torrance) e uma
    // mudanca isolada dentro de CalculateLight/main, que nao afeta a
    // API C++ (GPULight/LightComponent) nem o editor.
    static const char* s_FragmentSrc = R"(
        #version 450 core
        in vec3 v_Normal;
        in vec3 v_WorldPos;
        out vec4 o_Color;

        uniform vec3 u_BaseColor;
        uniform vec3 u_CameraWorldPos;

        #define MAX_LIGHTS 16
        #define LIGHT_TYPE_POINT       0
        #define LIGHT_TYPE_SPOT        1
        #define LIGHT_TYPE_DIRECTIONAL 2

        struct GPULight {
            int   Type;
            vec3  Position;
            vec3  Direction;
            vec3  Color;
            float Intensity;
            float Range;
            float CosOuterAngle;
            float CosInnerAngle;
        };

        uniform int u_LightCount;
        uniform GPULight u_Lights[MAX_LIGHTS];

        // --- Shadow mapping (directional, uma luz por vez) --------------
        // u_HasShadow=false (o padrao, ver Renderer::DrawMesh) e o
        // comportamento antigo exato: nenhuma amostragem de textura
        // acontece, sem custo extra. Ver comentario grande em
        // Renderer::RenderShadowPass (Renderer.cpp) para o pipeline
        // completo dos 2 passes.
        uniform sampler2D u_ShadowMap;
        uniform mat4 u_LightSpaceMatrix;
        uniform bool u_HasShadow;
        // Indice dentro de u_Lights[] da UNICA luz que gerou u_ShadowMap
        // neste frame (-1 = nenhuma, embora u_HasShadow=false ja cubra
        // esse caso) - ver Renderer::RenderShadowPass/UploadLights, que
        // preenchem isso a partir da mesma busca "primeira luz Directional
        // com CastShadows=true" usada para desenhar o shadow map em si,
        // garantindo que os dois nunca discordem sobre qual luz e essa.
        uniform int u_ShadowCasterLightIndex;

        // Retorna um fator de sombra: 0.0 = totalmente iluminado, 1.0 =
        // totalmente na sombra. 'worldPos' e 'normal' sao do fragmento
        // ATUAL (espaco de mundo); a funcao mesma faz a projecao para o
        // espaco da luz via u_LightSpaceMatrix.
        float CalculateShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
            vec4 fragPosLightSpace = u_LightSpaceMatrix * vec4(worldPos, 1.0);

            // Perspective divide (sem efeito real aqui ja que a projecao
            // da luz e ORTHO - ver RenderShadowPass - mas mantido pelo
            // padrao de qualquer implementacao de shadow mapping, caso a
            // engine ganhe luzes Spot/Point com sombra via projecao
            // perspective no futuro) + remapeia de [-1,1] (NDC) para
            // [0,1] (coordenadas de textura/profundidade).
            vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
            projCoords = projCoords * 0.5 + 0.5;

            // Fora do frustum ortho da luz (ver GL_CLAMP_TO_BORDER +
            // borda branca em ShadowMap.cpp) - nao deveria acontecer na
            // pratica (RenderShadowPass calcula o frustum para cobrir a
            // cena inteira), mas serve de seguranca contra artefatos caso
            // uma entidade esteja bem fora do bounding box calculado.
            if (projCoords.z > 1.0)
                return 0.0;

            // Bias inclinado pela normal (maior quase-paralelo a luz,
            // menor de frente) - sem isso, a comparacao de profundidade
            // sofre de "shadow acne" (listras/moire na propria superficie
            // iluminada, causadas por precisao limitada do depth buffer).
            // Constantes escolhidas empiricamente (padrao comum em
            // implementacoes de shadow mapping, ex: LearnOpenGL) - ajustar
            // aqui se alguma cena especifica mostrar peter-panning (sombra
            // "descolada" do objeto, bias grande demais) ou acne (bias
            // pequeno demais).
            float bias = max(0.0025 * (1.0 - dot(normal, lightDir)), 0.0006);

            // PCF (Percentage-Closer Filtering) 3x3: amostra a vizinhanca
            // do texel em vez de um unico texel, e faz a MEDIA do
            // resultado - suaviza a borda serrilhada que uma unica
            // comparacao de profundidade produziria (sombra com "escada"
            // visivel pixel a pixel).
            float shadow = 0.0;
            vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
            for (int x = -1; x <= 1; x++) {
                for (int y = -1; y <= 1; y++) {
                    float closestDepth = texture(u_ShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
                    shadow += (projCoords.z - bias) > closestDepth ? 1.0 : 0.0;
                }
            }
            return shadow / 9.0;
        }

        // Atenuacao por distancia estilo "smooth falloff" (usada por
        // engines como Unity/Unreal em vez do inverse-square puro, que
        // tende a infinito perto da fonte e nunca chega literalmente a
        // zero) - cai suavemente de 1.0 (na fonte) a 0.0 (em Range),
        // clampada para nunca ficar negativa.
        float AttenuateByDistance(float distance, float range) {
            if (range <= 0.0) return 1.0;
            float ratio = clamp(distance / range, 0.0, 1.0);
            float falloff = 1.0 - ratio * ratio;
            return falloff * falloff;
        }

        // Retorna a contribuicao de UMA luz (ja multiplicada por cor,
        // intensidade, atenuacao de distancia/cone e o termo difuso de
        // Lambert) para o fragmento atual. 'normal' e 'viewWorldPos' sao
        // por-fragmento; 'light' e um elemento do array de uniforms.
        // 'applyShadow' e true apenas para a UNICA luz (Directional) que
        // gerou u_ShadowMap neste frame (ver main() - o chamador decide
        // isso comparando o indice do loop com o indice retornado por
        // Renderer::RenderShadowPass/DrawScene). Point/Spot e outras
        // luzes Directional sem CastShadows nunca chamam CalculateShadow.
        vec3 CalculateLight(GPULight light, vec3 normal, vec3 worldPos, bool applyShadow) {
            vec3 lightDir;
            float attenuation = 1.0;

            if (light.Type == LIGHT_TYPE_DIRECTIONAL) {
                // Posicao da entidade e ignorada (ver LightComponent,
                // Components.h) - so a direcao importa, luz "infinita".
                lightDir = normalize(-light.Direction);
            } else {
                // Point e Spot: luz vem de um ponto no espaco.
                vec3 toLight = light.Position - worldPos;
                float distance = length(toLight);
                lightDir = distance > 0.0001 ? (toLight / distance) : vec3(0.0, 1.0, 0.0);
                attenuation = AttenuateByDistance(distance, light.Range);

                if (light.Type == LIGHT_TYPE_SPOT) {
                    // Cone: quanto o fragmento esta alinhado com a
                    // direcao do spot (cos do angulo entre eles) versus
                    // os cossenos pre-calculados do angulo interno/externo
                    // (ver LightComponent::SpotAngle/InnerSpotAngle,
                    // Components.h). smoothstep da a borda suave entre
                    // CosOuterAngle (0% de luz) e CosInnerAngle (100%).
                    float cosAngleToFragment = dot(normalize(-light.Direction), lightDir);
                    float spotFactor = smoothstep(light.CosOuterAngle, light.CosInnerAngle, cosAngleToFragment);
                    attenuation *= spotFactor;
                }
            }

            float diffuse = max(dot(normal, lightDir), 0.0);

            float shadow = 0.0;
            if (applyShadow && u_HasShadow)
                shadow = CalculateShadow(worldPos, normal, lightDir);

            return light.Color * light.Intensity * diffuse * attenuation * (1.0 - shadow);
        }

        void main() {
            vec3 normal = normalize(v_Normal);
            vec3 viewDir = normalize(u_CameraWorldPos - v_WorldPos);

            // Face voltada para "dentro" (de costas para a camera) - ver
            // comentario grande acima de s_VertexSrc sobre o motivo de
            // usar discard em vez de glCullFace aqui.
            if (dot(normal, viewDir) < 0.0)
                discard;

            // Ambiente fixo e pequeno - evita faces totalmente pretas em
            // areas sem nenhuma luz alcancando (nao ha GI/luz indireta
            // ainda). Mesmo valor (0.25) que o shader antigo usava, para
            // cenas sem luzes configuradas nao ficarem mais escuras do
            // que estavam antes desta mudanca.
            vec3 lightAccum = vec3(0.25);

            for (int i = 0; i < u_LightCount; i++) {
                bool applyShadow = (i == u_ShadowCasterLightIndex);
                lightAccum += CalculateLight(u_Lights[i], normal, v_WorldPos, applyShadow);
            }

            vec3 color = u_BaseColor * lightAccum;
            o_Color = vec4(color, 1.0);
        }
    )";

    // Shader de linha: sem normal, sem luz - so posicao transformada e uma
    // cor solida via uniform. Deliberadamente separado do s_BasicShader
    // (que exige atributo de normal no layout location 1) para o VAO de
    // linhas poder ter um layout de vertice mais simples (so vec3
    // posicao) sem enviar normais fake para a GPU.
    static const char* s_LineVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;

        uniform mat4 u_ViewProjection;

        void main() {
            gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_LineFragmentSrc = R"(
        #version 450 core
        out vec4 o_Color;

        uniform vec3 u_BaseColor;

        void main() {
            o_Color = vec4(u_BaseColor, 1.0);
        }
    )";

    // Indice dentro de s_Meshes - deve bater com a ordem numerica do enum
    // PrimitiveMesh em Components.h (Cube=0, Sphere=1, Capsule=2,
    // Cylinder=3, Plane=4).
    static uint32_t MeshIndex(PrimitiveMesh mesh) {
        return (uint32_t)mesh;
    }

    void Renderer::Init() {
        s_BasicShader = Shader::Create("BasicLit", s_VertexSrc, s_FragmentSrc);

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

        s_LineShader = Shader::Create("Line", s_LineVertexSrc, s_LineFragmentSrc);
        s_ShadowDepthShader = Shader::Create("ShadowDepth", s_ShadowDepthVertexSrc, s_ShadowDepthFragmentSrc);
        // s_ShadowMap NAO e criado aqui de proposito - ver comentario em
        // Renderer.h (s_ShadowMap) sobre alocacao sob demanda.

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

    void Renderer::Shutdown() {
        for (auto& mesh : s_Meshes)
            mesh.reset();
        s_BasicShader.reset();
        s_LineShader.reset();
        s_ShadowDepthShader.reset();
        s_ShadowMap.reset();

        if (s_LineVBO) { glDeleteBuffers(1, &s_LineVBO); s_LineVBO = 0; }
        if (s_LineVAO) { glDeleteVertexArrays(1, &s_LineVAO); s_LineVAO = 0; }
    }

    void Renderer::Clear(float r, float g, float b, float a) {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void Renderer::SetViewport(uint32_t width, uint32_t height) {
        glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    }

    void Renderer::DrawMesh(PrimitiveMesh meshType, const float* viewProjection, const float* model, const float* color, const std::vector<GPULight>* lights, const ShadowMap* shadowMap, int shadowCasterLightIndex) {
        if (!s_BasicShader) return;

        Mesh* mesh = s_Meshes[MeshIndex(meshType)].get();
        if (!mesh) return; // nao deveria acontecer apos Init(), mas evita um crash silencioso se algo pedir para desenhar antes da engine estar pronta

        s_BasicShader->Bind();
        s_BasicShader->SetMat4("u_ViewProjection", viewProjection);
        s_BasicShader->SetMat4("u_Model", model);
        s_BasicShader->SetFloat3("u_CameraWorldPos", s_CameraWorldPos[0], s_CameraWorldPos[1], s_CameraWorldPos[2]);
        if (color)
            s_BasicShader->SetFloat3("u_BaseColor", color[0], color[1], color[2]);
        else
            s_BasicShader->SetFloat3("u_BaseColor", 0.85f, 0.55f, 0.2f);

        // 'lights' e opcional (ver comentario em Renderer.h) - sem lista,
        // desenha so com o ambiente fixo do shader (u_LightCount = 0).
        if (lights)
            UploadLights(*lights, shadowMap ? shadowCasterLightIndex : -1);
        else
            s_BasicShader->SetInt("u_LightCount", 0);

        // 'shadowMap' e opcional (ver comentario em Renderer.h) - sem ele,
        // u_HasShadow fica false e CalculateShadow nunca e chamada no
        // shader (comportamento identico a antes desta feature existir).
        // GL_TEXTURE0 e reservado para o shadow map: DrawMesh nao usa
        // nenhuma outra textura hoje (sem materiais/texturas ainda - ver
        // README), entao nao ha conflito de slot.
        if (shadowMap) {
            shadowMap->BindForReading(0);
            s_BasicShader->SetInt("u_ShadowMap", 0);
            s_BasicShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(shadowMap->GetLightSpaceMatrix()));
            s_BasicShader->SetInt("u_HasShadow", 1);
        } else {
            s_BasicShader->SetInt("u_HasShadow", 0);
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

    void Renderer::DrawLines(const float* points, uint32_t pointCount, const float* viewProjection, const float* color) {
        if (!s_LineShader || pointCount == 0) return;

        // ATENCAO: s_LineVAO e criado uma unica vez em Init() (contexto do
        // editor) e, como qualquer VAO, NAO e valido em outro contexto
        // OpenGL mesmo com share list (ver comentario grande em Mesh.h/
        // Mesh::BindForCurrentContext - mesmo problema que DrawMesh() tinha
        // e foi corrigido para meshes de entidade). DrawLines() so e
        // chamado hoje pelo EditorLayer (gizmos de selecao/camera preview),
        // nunca pela PlayWindow - se um dia gizmos de debug forem
        // desenhados tambem dentro da PlayWindow, esta funcao vai precisar
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
    //     ignorada (nenhum erro, nenhum aviso ainda - TODO se isso incomodar
    //     na pratica: um PRISM_CORE_WARN uma vez por sessao ajudaria).
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
            // map, igual antes desta feature existir. Limpa o estado
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

        // --- Pass de profundidade: mesmo loop de entidades de DrawScene,
        // shader/framebuffer diferentes.
        s_ShadowMap->BindForWriting();

        s_ShadowDepthShader->Bind();
        s_ShadowDepthShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(lightSpaceMatrix));

        for (auto entityHandle : meshView) {
            auto& meshRenderer = meshView.get<MeshRendererComponent>(entityHandle);
            glm::mat4 model = scene.GetWorldTransform(Entity(entityHandle, &scene));
            s_ShadowDepthShader->SetMat4("u_Model", glm::value_ptr(model));

            Mesh* mesh = s_Meshes[MeshIndex(meshRenderer.Mesh)].get();
            if (!mesh) continue;
            mesh->BindForCurrentContext();
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        }

        glBindVertexArray(0);
        s_ShadowDepthShader->Unbind();
        s_ShadowMap->Unbind();

        return s_ShadowMap.get();
    }

    void Renderer::DrawScene(Scene& scene, const float* viewProjection, const float* cameraWorldPos) {
        SetCameraPosition(cameraWorldPos);

        // Coleta todas as luzes da cena UMA VEZ por frame (nao por
        // entidade desenhada) - reusada em todo DrawMesh abaixo, ja que a
        // lista de luzes ativas nao muda entre um mesh e outro do mesmo
        // frame. Ver CollectGPULights para a logica de traducao
        // LightComponent -> GPULight.
        std::vector<GPULight> lights = CollectGPULights(scene);

        // Shadow pass ANTES do pass de cor - ver comentario grande acima
        // de RenderShadowPass para o pipeline completo. Note que isto MUDA
        // tanto o FRAMEBUFFER quanto o VIEWPORT do OpenGL temporariamente
        // (para dentro do ShadowMap, na resolucao dele, tipicamente
        // diferente do framebuffer de cor que esta chamada de DrawScene
        // esta desenhando) - por isso salvamos os dois ANTES de chamar
        // RenderShadowPass e restauramos os dois explicitamente depois.
        //
        // BUG HISTORICO consertado aqui: uma versao anterior desta funcao
        // so restaurava o viewport (glViewport), nao o framebuffer
        // (glBindFramebuffer) - ShadowMap::Unbind() (chamado no fim de
        // RenderShadowPass) faz glBindFramebuffer(GL_FRAMEBUFFER, 0), ou
        // seja, volta para o framebuffer PADRAO DO SISTEMA (a janela do
        // SO), NAO para o Framebuffer offscreen da viewport
        // (m_ViewportFramebuffer, ja bindado pelo chamador antes de
        // chamar DrawScene - ver EditorLayer::RenderScene). Sem restaurar
        // o bind certo aqui, todo o pass de cor abaixo desenhava no
        // framebuffer errado - o painel Viewport (que so le a textura de
        // m_ViewportFramebuffer) ficava preto, MESMO SEM NENHUMA luz
        // projetando sombra ficar visivel, porque a cena inteira parava
        // de ser desenhada no lugar certo assim que qualquer luz da cena
        // tivesse CastShadows=true (o que faz RenderShadowPass entrar
        // neste caminho pela primeira vez).
        GLint previousFramebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        GLint previousViewport[4];
        glGetIntegerv(GL_VIEWPORT, previousViewport);

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

        // Mesmo loop que EditorLayer::RenderSceneEntities fazia antes desta
        // funcao existir (ver comentario em Renderer.h) - toda entidade com
        // TransformComponent + MeshRendererComponent, usando a transform de
        // MUNDO (Scene::GetWorldTransform, ancestrais/parenting inclusos).
        auto view = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();
        for (auto entityHandle : view) {
            auto& meshRenderer = view.get<MeshRendererComponent>(entityHandle);
            glm::mat4 model = scene.GetWorldTransform(Entity(entityHandle, &scene));
            DrawMesh(meshRenderer.Mesh, viewProjection, glm::value_ptr(model), &meshRenderer.Color.x, &lights, shadowMap, shadowCasterLightIndex);
        }
    }

}
