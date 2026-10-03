#version 450 core

// Shader multi-luz: substitui a antiga luz direcional fake hardcoded
// por um array de ate MAX_LIGHTS luzes de verdade, vindas de
// LightComponent (ver Components.h) atraves de Renderer::CollectGPULights
// + UploadLights. MAX_LIGHTS NAO e definido aqui: o Renderer o injeta
// logo apos o #version a partir de Prism::MAX_LIGHTS (Renderer.h), entao
// C++ e GLSL nunca divergem. Para validar este arquivo fora da engine:
//   glslangValidator -l -DMAX_LIGHTS=16 basic.frag
//
// Cada luz tem um campo inteiro 'Type' que decide qual formula de
// atenuacao/cone usar (ver funcao CalculateLight abaixo) - igual o
// enum Prism::LightType (Point=0, Spot=1, Directional=2, ...).
// Adicionar um novo tipo (ex: Area) significa: adicionar mais um
// "else if (light.Type == N)" aqui, SEM mudar a struct GPULight nem
// o array de uniforms - ver comentario grande sobre extensibilidade
// em Components.h/Renderer.h.
//
// Modelo de iluminacao: PBR metallic/roughness (Cook-Torrance GGX), ver
// CalculateLight/main. Trocar o modelo e uma mudanca isolada neste
// arquivo, que nao afeta a API C++ (GPULight/LightComponent) nem o editor.

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

// Ambiente = gradiente vertical analitico de 3 cores (zenite / horizonte /
// chao), sem textura nem cubemap - ver EnvironmentColor() e
// Renderer::SetEnvironmentColors. u_Ambient e a INTENSIDADE dele
// (padrao 0.10, o mesmo do ambiente cinza uniforme que ele
// substituiu: o brilho MEDIO das cenas existentes nao muda).
uniform float u_Ambient;
uniform vec3 u_EnvZenith;   // sRGB (como o picker) - convertido para linear em main()
uniform vec3 u_EnvHorizon;
uniform vec3 u_EnvGround;
// Fator que faz u_Ambient valer o brilho MEDIO do ambiente com QUALQUER
// paleta: u_Ambient / (luminancia media da irradiancia). Calculado
// na CPU (Renderer::ComputeEnvironmentNormalization) porque depende
// das tres cores - uma constante fixa aqui so estaria certa para uma
// paleta especifica (com o azul padrao, 1.45x errada).
uniform float u_EnvScale;

#ifndef MAX_LIGHTS
#error "MAX_LIGHTS deve ser injetado pelo Renderer (ver Renderer::Init) - nao defina aqui."
#endif
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
// proposito: gizmos de linha (line.frag), o clear color e a
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

// --- Ambiente: gradiente analitico (substituto barato de IBL) -------
//
// O ambiente depende so da altura y = direcao.y (+1 = ceu, 0 =
// horizonte, -1 = chao): uma mistura suave (smoothstep) entre as tres
// cores de u_Env*. Isso permite calcular TUDO de forma fechada, sem
// amostrar textura:
//   - EnvironmentColor(y):     o ambiente visto numa direcao (reflexo
//                              perfeito, para metais lisos)
//   - EnvironmentIrradiance(): a luz difusa que chega numa normal
//   - EnvironmentBRDF():       fracao do reflexo (escala/bias do Fresnel)
// As constantes abaixo foram ajustadas contra integracao numerica; ver
// docs/renderizacao.md (secao "Ambiente") para os erros medidos.
//
// Sem cubemap/HDR: e uma APROXIMACAO. Nao ha reflexo de objetos da
// cena nem de um skybox real - so o gradiente. Quando existir IBL de
// verdade, estas tres funcoes sao o ponto de troca.
// Cores do gradiente em LINEAR, preenchidas no inicio de main() a
// partir de u_Env* (sRGB). Globais (e nao parametros) para as tres
// funcoes abaixo continuarem com a assinatura simples.
vec3 g_EnvZenith;
vec3 g_EnvHorizon;
vec3 g_EnvGround;

vec3 EnvironmentColor(float y) {
    float up = clamp(y, 0.0, 1.0);
    float dn = clamp(-y, 0.0, 1.0);
    float su = up * up * (3.0 - 2.0 * up);   // smoothstep: sem "linha" dura no horizonte
    float sd = dn * dn * (3.0 - 2.0 * dn);
    return (y >= 0.0) ? mix(g_EnvHorizon, g_EnvZenith, su)
                      : mix(g_EnvHorizon, g_EnvGround, sd);
}

// Irradiancia difusa numa normal com componente vertical ny. A
// integral do gradiente e LINEAR nas tres cores, entao ela e
//   zenite*Wz(ny) + horizonte*Wh(ny) + chao*Wg(ny)
// com tres pesos escalares (polinomios de grau 2 em ny, soma sempre
// 1). Por isso funciona com QUALQUER paleta, sem reajustar
// constantes (erro maximo ~0.009, medido inclusive com uma paleta de
// por-do-sol totalmente diferente).
vec3 EnvironmentIrradiance(float ny) {
    float ny2 = ny * ny;
    float wz = 0.202896 + 0.350002 * ny + 0.141758 * ny2;
    float wh = 0.594209                  - 0.283512 * ny2;
    float wg = 0.202896 - 0.350002 * ny + 0.141758 * ny2;
    return g_EnvZenith * wz + g_EnvHorizon * wh + g_EnvGround * wg;
}

// Termo BRDF do especular de ambiente (o que uma LUT faria): devolve
// (A, B) tal que reflexo = F0 * A + B. Base: aproximacao de Karis
// (Unreal 4, SIGGRAPH 2014) + correcao polinomial propria, porque o
// polinomio original de Karis erra ate 25 niveis de 255 em
// rugosidade alta olhando de frente (medido). A correcao foi ajustada
// para 0.2 <= roughness <= 1; abaixo disso ela extrapola, por isso o
// resultado e RESTRITO fisicamente (A em [0,1], A + B <= 1) - sem
// essa restricao, um espelho olhado de frente refletiria ate 23% mais
// luz do que recebe.
vec2 EnvironmentBRDF(float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;

    float omv = 1.0 - NdotV;
    float r2 = roughness * roughness;
    ab.x += -0.261181 + 0.417586 * NdotV + 0.108976 * NdotV * NdotV
            + 0.673558 * roughness - 0.782264 * roughness * NdotV - 0.307961 * r2;
    ab.y +=  0.028508 - 0.197511 * omv + 0.294312 * omv * omv - 0.284201 * omv * omv * omv
            - 0.044720 * roughness + 0.201709 * roughness * omv;

    float A = clamp(ab.x, 0.0, 1.0);
    float B = clamp(ab.y, 0.0, 1.0 - A);
    return vec2(A, B);
}

// Especular do ambiente: mistura entre o reflexo nitido (rugosidade
// 0: EnvironmentColor na direcao refletida) e a irradiancia
// (rugosidade 1: o ambiente inteiro borrado), com peso roughness^1.25
// (melhor expoente medido: erro medio ~1.5 niveis de 255 contra a
// integral exata do lobo GGX).
vec3 EnvironmentSpecular(vec3 R, float roughness) {
    float w = pow(roughness, 1.25);
    return mix(EnvironmentColor(R.y), EnvironmentIrradiance(R.y), w);
}

void main() {
    vec3 normal = normalize(v_Normal);
    vec3 viewDir = normalize(u_CameraWorldPos - v_WorldPos);

    // Face voltada para "dentro" (de costas para a camera) - ver
    // comentario grande no topo de basic.vert sobre o motivo de
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

    // Ambiente = difuso + especular do gradiente (ver
    // EnvironmentColor e cia, acima).
    //
    // DIFUSO: albedo * irradiancia, so para a parte nao metalica.
    //   u_EnvScale normaliza a irradiancia (calculada na CPU a partir
    //   das cores atuais), e u_Ambient (0.10) fica sendo o brilho
    //   MEDIO, exatamente como era com o cinza uniforme. O
    //   albedo multiplica (em vez de somar cinza) para a sombra manter
    //   a COR do material. Ganha a variacao topo/fundo (~1.6x).
    //   O kD tira do difuso a parte que virou reflexo (Fresnel),
    //   como em CalculateLight.
    //
    // ESPECULAR: F * EnvironmentSpecular(R). E ISTO que impede o
    //   metal preto: sem luz direta, um metal reflete o ambiente. F0
    //   como em CalculateLight; (A, B) e o termo BRDF de Karis
    //   corrigido, que depende de roughness e do angulo de visao.
    //
    // O SSAO (ao) multiplica os dois: oclusao bloqueia luz ambiente e
    // reflexo de ambiente, nunca a luz direta (ver comentario acima).
    vec3 lightAccum = vec3(0.0);

    // As cores do gradiente vem do color picker (sRGB) - mesma regra
    // do u_BaseColor: converte para linear ANTES de qualquer conta.
    g_EnvZenith  = SrgbToLinear(u_EnvZenith);
    g_EnvHorizon = SrgbToLinear(u_EnvHorizon);
    g_EnvGround  = SrgbToLinear(u_EnvGround);

    float NdotVenv = max(dot(normal, viewDir), 0.0001);
    vec3 F0env = mix(vec3(0.04), albedo, metallic);
    vec2 envAB = EnvironmentBRDF(clamp(roughness, 0.04, 1.0), NdotVenv);
    vec3 Fenv = F0env * envAB.x + vec3(envAB.y);

    vec3 kDenv = (vec3(1.0) - Fenv) * (1.0 - metallic);
    vec3 ambientDiffuse = kDenv * albedo * EnvironmentIrradiance(normal.y) * u_EnvScale * ao;

    vec3 R = reflect(-viewDir, normal);
    vec3 ambientSpecular = Fenv * EnvironmentSpecular(R, clamp(roughness, 0.04, 1.0)) * u_EnvScale * ao;

    vec3 ambient = ambientDiffuse + ambientSpecular;

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
