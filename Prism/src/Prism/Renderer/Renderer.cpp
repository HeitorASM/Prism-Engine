#include <glad/gl.h>
#include <GLFW/glfw3.h> 
#include "Renderer.h"
#include "PrimitiveMeshFactory.h"
#include "../Core/Log.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp> 
#include <algorithm> 
#include <string>    
#include <limits>   

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
    float Renderer::s_Exposure = 1.0f;
    float Renderer::s_Ambient = 0.10f;
    std::unordered_map<std::string, Scope<Texture2D>> Renderer::s_TextureCache;

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
    // igual independente do winding. O culling por indice e mais barato
    // para a GPU, mas para o tamanho de cena atual a diferenca e
    // desprezivel. Otimizacao possivel: normalizar o winding de cada
    // PrimitiveMeshFactory::Create* e trocar para glCullFace.
    static const char* s_VertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;
        layout(location = 2) in vec2 a_UV;
        layout(location = 3) in vec3 a_Tangent;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Model;

        out vec3 v_Normal;
        out vec3 v_WorldPos;
        out vec2 v_UV;
        out vec3 v_Tangent;

        void main() {
            v_Normal = mat3(u_Model) * a_Normal;
            v_Tangent = mat3(u_Model) * a_Tangent;
            v_UV = a_UV;
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

    // ========================================================================
    // Shaders de SSAO - ver comentario grande em SSAO.h para o pipeline
    // completo (geometria -> SSAO bruto -> blur -> pass de cor final).
    // ========================================================================

    // Pre-pass de geometria (RenderGeometryPrePass): mesma ideia do
    // shadow-depth acima, mas com a camera PRINCIPAL (nao a da luz) e uma
    // SEGUNDA saida (normal em view-space, alem da profundidade que a GPU
    // ja escreve sozinha no depth buffer).
    static const char* s_GeometryPrePassVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_View;
        uniform mat4 u_Model;

        // Normal transformada para VIEW-SPACE (nao world-space) - ver
        // comentario grande em GeometryBuffer.h sobre o motivo. A matriz
        // normal correta seria transpose(inverse(mat3(u_View * u_Model)))
        // para lidar com escala nao-uniforme sem distorcer a normal -
        // omitido aqui de proposito (mat3(u_View * u_Model) direto) pela
        // mesma razao ja documentada em s_VertexSrc (o shader principal):
        // esta engine ainda nao aplica escala nao-uniforme em nenhum fluxo
        // de edicao hoje, entao a versao mais simples e barata e
        // equivalente na pratica. Revisitar junto se/quando escala
        // nao-uniforme for suportada.
        out vec3 v_ViewNormal;

        void main() {
            v_ViewNormal = normalize(mat3(u_View * u_Model) * a_Normal);
            gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_GeometryPrePassFragmentSrc = R"(
        #version 450 core
        in vec3 v_ViewNormal;
        layout(location = 0) out vec4 o_ViewNormal;

        void main() {
            o_ViewNormal = vec4(normalize(v_ViewNormal), 1.0);
        }
    )";

    // Vertex shader COMPARTILHADO pelos 2 fullscreen-quad passes abaixo
    // (SSAO e blur) - gera um unico triangulo GIGANTE que cobre a tela
    // inteira usando so gl_VertexID (sem VBO/atributos nenhum - ver
    // comentario em Renderer::GetFullscreenQuadVAOForCurrentContext, Renderer.h). Tecnica
    // padrao ("fullscreen triangle trick"): um triangulo com vertices em
    // (-1,-1), (3,-1), (-1,3) cobre totalmente a regiao [-1,1]x[-1,1] (o
    // NDC inteiro), com a parte que sobra do triangulo fora da tela
    // simplesmente descartada pelo clipping do rasterizador - mais barato
    // que desenhar 2 triangulos (4 vertices, 6 indices) formando um quad
    // de verdade, pela mesma razao que menos chamadas/vertices e sempre
    // melhor quando o resultado visual e identico.
    static const char* s_FullscreenQuadVertexSrc = R"(
        #version 450 core
        out vec2 v_TexCoord;

        void main() {
            v_TexCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
            gl_Position = vec4(v_TexCoord * 2.0 - 1.0, 0.0, 1.0);
        }
    )";

    static const char* s_SSAOFragmentSrc = R"(
        #version 450 core
        in vec2 v_TexCoord;
        layout(location = 0) out float o_Occlusion;

        uniform sampler2D u_ViewNormal;  // GeometryBuffer - view-space
        uniform sampler2D u_Depth;       // GeometryBuffer
        uniform sampler2D u_NoiseTexture;

        uniform vec3 u_Samples[16];
        uniform mat4 u_Projection;
        uniform mat4 u_InverseProjection;
        uniform vec2 u_ScreenSize;
        uniform vec2 u_NoiseScale; // ScreenSize / 4.0 (tamanho da textura de ruido) - repete o ruido "ladrilhado" por toda a tela

        const int kKernelSize = 16;
        const float kRadius = 0.5;     // raio da hemisfera de amostragem, em unidades de mundo (metros, assumindo 1 unidade = 1 metro) - ajustar aqui se cenas muito maiores/menores mostrarem AO fraco/exagerado demais
        const float kBias = 0.025;     // evita "acne" de auto-oclusao por precisao limitada de profundidade, mesmo espirito do bias em CalculateShadow (Renderer.cpp)

        // Reconstroi a posicao em VIEW-SPACE de um pixel a partir da sua
        // profundidade (NDC) - "unproject" via a inversa da matriz de
        // projecao, tecnica padrao para SSAO sem precisar guardar a
        // posicao 3D inteira num G-buffer extra (so profundidade, que
        // ja escrevemos de qualquer forma para o teste de profundidade
        // normal do pre-pass).
        vec3 ReconstructViewPos(vec2 texCoord) {
            float depthNDC = texture(u_Depth, texCoord).r * 2.0 - 1.0;
            vec4 clipPos = vec4(texCoord * 2.0 - 1.0, depthNDC, 1.0);
            vec4 viewPos = u_InverseProjection * clipPos;
            return viewPos.xyz / viewPos.w;
        }

        void main() {
            vec3 fragPos = ReconstructViewPos(v_TexCoord);
            vec3 normal = normalize(texture(u_ViewNormal, v_TexCoord).rgb);
            vec3 randomVec = normalize(texture(u_NoiseTexture, v_TexCoord * u_NoiseScale).xyz);

            // Base TBN (tangent/bitangent/normal) para orientar o kernel de
            // amostras (definido em tangent space, ver SSAO::GenerateKernelAndNoise)
            // ao longo da normal REAL de cada pixel - processo de
            // Gram-Schmidt simplificado (a "aleatoriedade" de randomVec e o
            // que produz a rotacao usada para quebrar o banding, ver
            // comentario em SSAO.cpp sobre a textura de ruido).
            vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
            vec3 bitangent = cross(normal, tangent);
            mat3 TBN = mat3(tangent, bitangent, normal);

            float occlusion = 0.0;
            for (int i = 0; i < kKernelSize; i++) {
                vec3 samplePos = fragPos + (TBN * u_Samples[i]) * kRadius;

                vec4 offset = u_Projection * vec4(samplePos, 1.0);
                offset.xyz /= offset.w;
                offset.xyz = offset.xyz * 0.5 + 0.5; // NDC -> [0,1] (coordenada de textura)

                float sampleDepthViewZ = ReconstructViewPos(offset.xy).z;

                // Range check: sem isto, geometria muito distante do ponto
                // amostrado (ex: uma parede longe atras de um objeto
                // pequeno) contaria como "oclusao" so por estar mais perto
                // da camera que o far plane - smoothstep suaviza a
                // transicao em vez de um corte abrupto (que apareceria
                // como uma borda visivel de AO ao redor de cada objeto).
                float rangeCheck = smoothstep(0.0, 1.0, kRadius / max(abs(fragPos.z - sampleDepthViewZ), 0.0001));
                occlusion += (sampleDepthViewZ >= samplePos.z + kBias ? 1.0 : 0.0) * rangeCheck;
            }

            occlusion = occlusion / float(kKernelSize);
            o_Occlusion = 1.0 - occlusion; // convertido para "quanto de luz passa" - facilita o pass de cor final so multiplicar direto (ver u_AOMap em s_FragmentSrc)
        }
    )";

    static const char* s_SSAOBlurFragmentSrc = R"(
        #version 450 core
        in vec2 v_TexCoord;
        layout(location = 0) out float o_Occlusion;

        uniform sampler2D u_SSAOTexture;

        void main() {
            // Box blur 4x4 simples sobre o texel size do proprio SSAO
            // (nao um blur "geometry-aware"/bilateral que preservaria
            // bordas com mais fidelidade) - suficiente para remover o
            // ruido introduzido pela textura de rotacao 4x4 (ver
            // SSAO::GenerateKernelAndNoise) sem borrar visivelmente
            // silhuetas de objetos, dado que o proprio kernel de SSAO ja
            // e localizado (kRadius pequeno). Um blur bilateral (que leva
            // profundidade/normal em conta para nao misturar objetos
            // diferentes) e a evolucao natural se esta versao mostrar halo
            // perceptivel nas bordas dos objetos.
            vec2 texelSize = 1.0 / vec2(textureSize(u_SSAOTexture, 0));
            float result = 0.0;
            for (int x = -2; x < 2; x++) {
                for (int y = -2; y < 2; y++) {
                    vec2 offset = vec2(float(x), float(y)) * texelSize;
                    result += texture(u_SSAOTexture, v_TexCoord + offset).r;
                }
            }
            o_Occlusion = result / 16.0;
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
    // resto do renderer neste estagio. Trocar
    // por um modelo mais realista (Blinn-Phong, PBR/Cook-Torrance) e uma
    // mudanca isolada dentro de CalculateLight/main, que nao afeta a
    // API C++ (GPULight/LightComponent) nem o editor.
    static const char* s_FragmentSrc = R"(
        #version 450 core
        in vec3 v_Normal;
        in vec3 v_WorldPos;
        in vec2 v_UV;
        in vec3 v_Tangent;
        out vec4 o_Color;

        uniform vec3 u_BaseColor;
        uniform vec3 u_CameraWorldPos;

        // --- Material (albedo/normal/roughness-metallic) -----------------
        // u_Has*Map=false (o padrao) significa nenhuma amostragem de
        // textura, so os fatores/tint (ver MaterialComponent,
        // Components.h) - permite desenhar qualquer MeshRendererComponent
        // SEM MaterialComponent.
        uniform sampler2D u_AlbedoMap;
        uniform bool u_HasAlbedoMap;
        uniform sampler2D u_NormalMap;
        uniform bool u_HasNormalMap;
        uniform sampler2D u_RoughnessMetallicMap;
        uniform bool u_HasRoughnessMetallicMap;

        // Fatores PBR - multiplicam o mapa (quando existe) ou valem sozinhos
        // (sem mapa). SEM MaterialComponent, o lado C++ envia roughness=1 e
        // metallic=0 (ver Renderer::DrawMesh): a superficie fica 100% fosca
        // e o especular some, o que reproduz o visual Lambert de antes.
        uniform float u_Roughness;
        uniform float u_Metallic;

        // Nivel do ambiente fixo (sem GI). Era 0.25 hardcoded; agora e
        // uniform para ajuste sem tocar no shader - ver Renderer::SetAmbient.
        uniform float u_Ambient;

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
        // Raio do frustum ortho da luz (ver comentario em
        // texelWorldSize, CalculateShadow abaixo) - so tem valor
        // significativo quando u_HasShadow=true (RenderShadowPass e
        // quem calcula e propaga isto, ver Renderer.cpp).
        uniform float u_ShadowFrustumRadius;
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
            // Normal offset bias: desloca o PONTO DE MUNDO ao longo da
            // normal (nao so a profundidade comparada, como uma versao
            // anterior desta funcao fazia) antes de projetar para o
            // espaco da luz - tecnica mais robusta que um bias de
            // profundidade simples para superficies CURVAS (esferas,
            // capsulas, cilindros): num cubo/plano a normal e constante
            // por face inteira, entao um bias fixo por pixel funciona
            // bem; numa capsula a normal varia suavemente vertice a
            // vertice, e o bias de profundidade sozinho deixava um
            // padrao visivel de faixas/listras na sombra da propria
            // superficie curva (shadow acne mais pronunciado em
            // geometria arredondada). Deslocar a AMOSTRA (nao so o
            // limiar de comparacao) ao longo da normal empurra o ponto
            // testado para fora da superficie de forma proporcional a
            // "largura" de um texel do shadow map em unidades de mundo,
            // o que se adapta melhor a curvatura continua. Ver
            // "Normal Offset Shadows" (tecnica classica, usada por
            // engines como Unity) para a referencia desta abordagem.
            // texelWorldSize: quanto (em unidades de mundo) um texel do
            // shadow map cobre, usado para escalar o normal offset acima
            // de forma proporcional - um shadow map de resolucao fixa
            // (ver ShadowMap::ShadowMap) cobrindo um frustum GRANDE
            // (cena com objetos espalhados) tem texels fisicamente
            // maiores que o mesmo shadow map cobrindo uma cena pequena;
            // sem escalar por u_ShadowFrustumRadius, um offset fixo
            // ficaria exagerado (sombra "descolada" do objeto, peter-
            // panning) em cenas pequenas ou insuficiente (acne de volta)
            // em cenas grandes. u_ShadowFrustumRadius e o mesmo
            // 'sceneRadius' que RenderShadowPass calculou para
            // dimensionar o frustum ortho da luz (ver Renderer.cpp).
            float texelWorldSize = (2.0 * u_ShadowFrustumRadius) / float(textureSize(u_ShadowMap, 0).x);
            vec3 offsetWorldPos = worldPos + normal * texelWorldSize * 1.5;

            vec4 fragPosLightSpace = u_LightSpaceMatrix * vec4(offsetWorldPos, 1.0);

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

            // O normal offset acima ja faz a maior parte do trabalho de
            // evitar acne; este bias residual, pequeno, cobre so o erro de
            // precisao de ponto flutuante remanescente.
            float bias = max(0.0006 * (1.0 - dot(normal, lightDir)), 0.00015);

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
        // --- BRDF Cook-Torrance (PBR metallic/roughness) ------------------
        // Especular = D * G * F / (4 * N.L * N.V), com:
        //   D: distribuicao de normais GGX/Trowbridge-Reitz (forma do brilho)
        //   G: geometria Smith com Schlick-GGX (auto-sombreamento de micro-facetas)
        //   F: Fresnel-Schlick (mais reflexo em angulos rasantes)
        // Tudo em espaco LINEAR (ver pipeline de cor acima).
        const float PI = 3.14159265359;

        float DistributionGGX(float NdotH, float roughness) {
            float a  = roughness * roughness;   // remapeamento "Disney": alpha = roughness^2
            float a2 = a * a;
            float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
            return a2 / (PI * d * d);
        }

        float GeometrySchlickGGX(float NdotX, float roughness) {
            // k para luz DIRETA (analitica): (r+1)^2 / 8 (Epic/Unreal).
            float r = roughness + 1.0;
            float k = (r * r) / 8.0;
            return NdotX / (NdotX * (1.0 - k) + k);
        }

        float GeometrySmith(float NdotV, float NdotL, float roughness) {
            return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
        }

        vec3 FresnelSchlick(float cosTheta, vec3 F0) {
            return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
        }

        // 'albedo', 'roughness' e 'metallic' sao por-fragmento (ja resolvidos
        // em main() a partir de textura/fatores); 'viewDir' aponta do
        // fragmento para a camera.
        vec3 CalculateLight(GPULight light, vec3 normal, vec3 viewDir, vec3 albedo, float roughness, float metallic, vec3 worldPos, bool applyShadow) {
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

            float NdotL = max(dot(normal, lightDir), 0.0);

            float shadow = 0.0;
            if (applyShadow && u_HasShadow)
                shadow = CalculateShadow(worldPos, normal, lightDir);

            // Sem contribuicao (de costas para a luz): evita divisao/calculo inutil.
            if (NdotL <= 0.0)
                return vec3(0.0);

            vec3 halfVec = normalize(viewDir + lightDir);
            float NdotV  = max(dot(normal, viewDir), 0.0001); // nunca 0: aparece no denominador
            float NdotH  = max(dot(normal, halfVec), 0.0);
            float VdotH  = max(dot(viewDir, halfVec), 0.0);

            // Piso de rugosidade: com roughness ~0 o GGX vira um ponto
            // infinitamente fino (e uma luz direcional/pontual nao tem area),
            // sumindo ou explodindo em fireflies. 0.04 e o piso usual.
            float r = clamp(roughness, 0.04, 1.0);

            // F0 = refletancia a 0 grau: dieletricos ~4% (cinza), metais usam
            // a propria cor do albedo (metais tingem o reflexo).
            vec3 F0 = mix(vec3(0.04), albedo, metallic);

            float D = DistributionGGX(NdotH, r);
            float G = GeometrySmith(NdotV, NdotL, r);
            vec3  F = FresnelSchlick(VdotH, F0);

            vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);

            // Conservacao de energia: o que e refletido (F) nao pode tambem
            // ser difuso; metais nao tem difuso (kD -> 0 com metallic = 1).
            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
            vec3 diffuse = kD * albedo / PI;

            // radiance * BRDF * N.L. O 'PI' do difuso e cancelado pela
            // convencao "luz de intensidade 1 = superficie branca em 1.0":
            // multiplicamos por PI abaixo para que Intensity=1 continue
            // significando o mesmo brilho de antes (Lambert sem /PI). Sem
            // isso, todas as cenas existentes ficariam ~3x mais escuras.
            vec3 radiance = light.Color * light.Intensity * attenuation * (1.0 - shadow);
            return (diffuse * PI + specular * PI) * radiance * NdotL;
        }

        // --- SSAO (Screen-Space Ambient Occlusion) -----------------------
        // u_HasAO=false (o padrao, ver Renderer::DrawMesh) e o
        // comportamento antigo exato: ambiente fixo em 0.25 sem nenhuma
        // amostragem extra. Ver comentario grande em SSAO.h para o
        // pipeline completo que produz u_AOMap ANTES deste shader rodar.
        uniform sampler2D u_AOMap;
        uniform bool u_HasAO;
        uniform vec2 u_ScreenSize;

        // --- Pipeline de cor: linear -> tela ------------------------------
        // Toda a matematica de iluminacao neste shader acontece em espaco
        // LINEAR (e o unico em que somar/multiplicar luz faz sentido
        // fisico). Mas duas pontas do pipeline NAO estao em linear:
        //
        //  ENTRADA: cores escolhidas pelo usuario no color picker (u_BaseColor:
        //    MeshRendererComponent::Color / MaterialComponent::AlbedoTint)
        //    estao em sRGB - o que o usuario ve no seletor. Texturas de albedo
        //    ja sao convertidas pela GPU (GL_SRGB8_ALPHA8, ver Texture.cpp),
        //    entao SO a cor solida precisa de conversao manual (SrgbToLinear).
        //
        //  SAIDA: o framebuffer e GL_RGBA8 comum (nao GL_SRGB8_ALPHA8) e o
        //    ImGui/monitor exibem o valor cru como se fosse sRGB. Sem
        //    converter de volta (LinearToSrgb), a imagem sai mais escura e
        //    com contraste errado - e qualquer luz com Intensity > 1 estoura
        //    para branco chapado, sem gradacao.
        //
        // Isto e feito AQUI (e nao num passe de pos-processamento) de
        // proposito: gizmos de linha (s_LineFragmentSrc), o clear color e a
        // UI sao desenhados no mesmo framebuffer DEPOIS de DrawScene, com
        // cores ja escolhidas em espaco de tela - um passe final sobre o
        // framebuffer inteiro os corromperia.
        //
        // u_Exposure: multiplicador de brilho antes do tone mapping (1.0 =
        // neutro). Ainda nao exposto no editor - ver Renderer::SetExposure.
        uniform float u_Exposure;

        vec3 SrgbToLinear(vec3 c) {
            // Curva sRGB exata (nao o atalho pow(c, 2.2)): trecho linear
            // perto do preto + trecho de potencia 2.4 no resto.
            bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
            vec3 low  = c / 12.92;
            vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
            return mix(high, low, vec3(cutoff));
        }

        vec3 LinearToSrgb(vec3 c) {
            c = clamp(c, 0.0, 1.0);
            bvec3 cutoff = lessThanEqual(c, vec3(0.0031308));
            vec3 low  = c * 12.92;
            vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
            return mix(high, low, vec3(cutoff));
        }

        // Tone mapping ACES (aproximacao de Krzysztof Narkowicz): comprime
        // valores HDR (>1.0) numa curva em "S" filmica em vez de cortar
        // bruscamente em 1.0 - luzes fortes ganham gradacao de brilho e
        // as sombras ganham contraste. Entrada e saida em espaco LINEAR.
        vec3 ToneMapACES(vec3 x) {
            const float a = 2.51;
            const float b = 0.03;
            const float c = 2.43;
            const float d = 0.59;
            const float e = 0.14;
            return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
        }

        void main() {
            vec3 normal = normalize(v_Normal);
            vec3 viewDir = normalize(u_CameraWorldPos - v_WorldPos);

            // Face voltada para "dentro" (de costas para a camera) - ver
            // comentario grande acima de s_VertexSrc sobre o motivo de
            // usar discard em vez de glCullFace aqui.
            if (dot(normal, viewDir) < 0.0)
                discard;

            // Normal mapping (TBN): so troca 'normal' se houver mapa -
            // sem u_HasNormalMap, 'normal' continua sendo so a
            // interpolada do vertex shader (comportamento antigo). O
            // mapa vem em tangent-space (RGB 0..1 -> XYZ -1..1, Z =
            // "para fora" da superficie lisa) - Bitangent calculada via
            // cross (nao armazenada, ver comentario em MeshVertex).
            if (u_HasNormalMap) {
                vec3 tangent = normalize(v_Tangent - normal * dot(v_Tangent, normal)); // Gram-Schmidt: reortogonaliza contra a normal interpolada
                vec3 bitangent = cross(normal, tangent);
                mat3 TBN = mat3(tangent, bitangent, normal);

                vec3 tangentNormal = texture(u_NormalMap, v_UV).rgb * 2.0 - 1.0;
                normal = normalize(TBN * tangentNormal);
            }

            // Ambiente fixo e pequeno - evita faces totalmente pretas em
            // areas sem nenhuma luz alcancando (nao ha GI/luz indireta).
            //
            // SSAO e aplicado SO no termo ambiente, nunca na contribuicao
            // direta de cada luz (CalculateLight) - fisicamente, ambient
            // occlusion aproxima o bloqueio de luz AMBIENTE/INDIRETA que
            // vem de todas as direcoes (por isso escurece cantos/frestas),
            // nao luz DIRETA vinda de uma direcao especifica - aplicar em
            // cima da luz direta tambem escureceria incorretamente
            // superficies bem iluminadas de frente so por estarem perto de
            // outra geometria.
            float ao = 1.0;
            if (u_HasAO)
                ao = texture(u_AOMap, gl_FragCoord.xy / u_ScreenSize).r;

            // u_BaseColor vem do color picker (sRGB) - converte para linear
            // ANTES de multiplicar por luz/textura. A textura de albedo
            // NAO passa por SrgbToLinear: ja chega linear da GPU
            // (GL_SRGB8_ALPHA8), converter de novo seria conversao dupla.
            vec3 albedo = SrgbToLinear(u_BaseColor);
            if (u_HasAlbedoMap)
                albedo *= texture(u_AlbedoMap, v_UV).rgb;

            // Roughness/metallic: convencao glTF (G = roughness, B = metallic),
            // texturas LINEARES. Os fatores multiplicam o mapa; sem mapa,
            // valem sozinhos.
            float roughness = u_Roughness;
            float metallic  = u_Metallic;
            if (u_HasRoughnessMetallicMap) {
                vec3 rm = texture(u_RoughnessMetallicMap, v_UV).rgb;
                roughness *= rm.g;
                metallic  *= rm.b;
            }

            // Ambiente: albedo * u_Ambient * AO. Multiplicar pelo albedo (e
            // nao somar cinza puro) e o que faz a sombra manter a COR do
            // material em vez de lavar para cinza. Metais quase nao tem
            // difuso, entao o ambiente tambem escurece com metallic (sem
            // reflexao de ambiente/IBL, seria um metal "preto" - ver TODO).
            vec3 lightAccum = vec3(0.0);
            vec3 ambient = albedo * u_Ambient * ao * (1.0 - metallic);

            for (int i = 0; i < u_LightCount; i++) {
                bool applyShadow = (i == u_ShadowCasterLightIndex);
                lightAccum += CalculateLight(u_Lights[i], normal, viewDir, albedo, roughness, metallic, v_WorldPos, applyShadow);
            }

            // u_BaseColor ja chega pronto do lado C++ como AlbedoTint
            // (MeshRendererComponent::Color OU MaterialComponent::AlbedoTint
            // - ver Renderer::DrawMesh/DrawScene) - amostrar o mapa aqui e
            // so MULTIPLICAR por cima, exatamente como a doc de
            // MaterialComponent::AlbedoTint descreve (tint sozinho = cor
            // solida; com textura, module o resultado da amostragem).
            // Cor final em linear -> exposicao -> tone mapping -> sRGB.
            // 'lightAccum' ja e radiancia refletida (BRDF aplicado por luz);
            // so o ambiente (ja multiplicado por albedo acima) soma por fora.
            vec3 color = (ambient + lightAccum) * u_Exposure;
            color = ToneMapACES(color);
            color = LinearToSrgb(color);
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

        s_GeometryPrePassShader = Shader::Create("GeometryPrePass", s_GeometryPrePassVertexSrc, s_GeometryPrePassFragmentSrc);
        s_SSAOShader = Shader::Create("SSAO", s_FullscreenQuadVertexSrc, s_SSAOFragmentSrc);
        s_SSAOBlurShader = Shader::Create("SSAOBlur", s_FullscreenQuadVertexSrc, s_SSAOBlurFragmentSrc);
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

    void Renderer::DrawMesh(PrimitiveMesh meshType, const float* viewProjection, const float* model, const float* color, const std::vector<GPULight>* lights, const ShadowMap* shadowMap, int shadowCasterLightIndex, const SSAO* ssao, const MaterialComponent* material) {
        if (!s_BasicShader) return;

        Mesh* mesh = s_Meshes[MeshIndex(meshType)].get();
        if (!mesh) return; // nao deveria acontecer apos Init(), mas evita um crash silencioso se algo pedir para desenhar antes da engine estar pronta

        s_BasicShader->Bind();
        s_BasicShader->SetMat4("u_ViewProjection", viewProjection);
        s_BasicShader->SetMat4("u_Model", model);
        s_BasicShader->SetFloat3("u_CameraWorldPos", s_CameraWorldPos[0], s_CameraWorldPos[1], s_CameraWorldPos[2]);
        // Sem este envio, u_Exposure valeria 0.0 (padrao de uniform nao
        // setado em GLSL) e a cena inteira sairia preta - ver s_FragmentSrc.
        s_BasicShader->SetFloat("u_Exposure", s_Exposure);
        s_BasicShader->SetFloat("u_Ambient", s_Ambient);

        // Com 'material' fornecido, AlbedoTint manda (ver comentario em
        // Renderer.h) - 'color' (MeshRendererComponent::Color) e ignorado
        // nesse caso, exatamente como um Material substitui a cor solida
        // antiga no editor (ver EditorLayer::RenderPropertiesPanel).
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
            // CalculateLight (Cook-Torrance) - ver s_FragmentSrc acima.
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
            // (ver CalculateLight em s_FragmentSrc), entao cenas existentes
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
        if (exposure > 0.0f)
            s_Exposure = exposure;
    }

    float Renderer::GetExposure() {
        return s_Exposure;
    }

    void Renderer::SetAmbient(float ambient) {
        // Diferente de SetExposure, 0 e valido (cena so com luzes). So
        // negativo e rejeitado: ambiente negativo subtrairia luz.
        if (ambient >= 0.0f)
            s_Ambient = ambient;
    }

    float Renderer::GetAmbient() {
        return s_Ambient;
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

            Mesh* mesh = s_Meshes[MeshIndex(meshRenderer.Mesh)].get();
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
        glDrawArrays(GL_TRIANGLES, 0, 3); // "big triangle" - ver comentario em s_FullscreenQuadVertexSrc
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

            DrawMesh(meshRenderer.Mesh, glm::value_ptr(viewProjection), glm::value_ptr(model), &meshRenderer.Color.x, &lights, shadowMap, shadowCasterLightIndex, ssao, material);
        }
    }

}
