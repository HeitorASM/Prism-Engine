#include <glad/gl.h>
#include "Renderer.h"
#include "PrimitiveMeshFactory.h"
#include "../Core/Log.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm> // std::min (UploadLights)
#include <string>    // std::to_string (UploadLights - nomes de uniform por indice)

namespace Prism {

    Ref<Shader> Renderer::s_BasicShader = nullptr;
    Ref<Shader> Renderer::s_LineShader = nullptr;
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
        vec3 CalculateLight(GPULight light, vec3 normal, vec3 worldPos) {
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
            return light.Color * light.Intensity * diffuse * attenuation;
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
                lightAccum += CalculateLight(u_Lights[i], normal, v_WorldPos);
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

    void Renderer::DrawMesh(PrimitiveMesh meshType, const float* viewProjection, const float* model, const float* color, const std::vector<GPULight>* lights) {
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
            UploadLights(*lights);
        else
            s_BasicShader->SetInt("u_LightCount", 0);

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
        }
    }

    void Renderer::DrawScene(Scene& scene, const float* viewProjection, const float* cameraWorldPos) {
        SetCameraPosition(cameraWorldPos);

        // Coleta todas as luzes da cena UMA VEZ por frame (nao por
        // entidade desenhada) - reusada em todo DrawMesh abaixo, ja que a
        // lista de luzes ativas nao muda entre um mesh e outro do mesmo
        // frame. Ver CollectGPULights para a logica de traducao
        // LightComponent -> GPULight.
        std::vector<GPULight> lights = CollectGPULights(scene);

        // Mesmo loop que EditorLayer::RenderSceneEntities fazia antes desta
        // funcao existir (ver comentario em Renderer.h) - toda entidade com
        // TransformComponent + MeshRendererComponent, usando a transform de
        // MUNDO (Scene::GetWorldTransform, ancestrais/parenting inclusos).
        auto view = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();
        for (auto entityHandle : view) {
            auto& meshRenderer = view.get<MeshRendererComponent>(entityHandle);
            glm::mat4 model = scene.GetWorldTransform(Entity(entityHandle, &scene));
            DrawMesh(meshRenderer.Mesh, viewProjection, glm::value_ptr(model), &meshRenderer.Color.x, &lights);
        }
    }

}
