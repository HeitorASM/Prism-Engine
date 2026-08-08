#include <glad/gl.h>
#include "Renderer.h"
#include "PrimitiveMeshFactory.h"
#include "../Core/Log.h"

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

    static const char* s_FragmentSrc = R"(
        #version 450 core
        in vec3 v_Normal;
        in vec3 v_WorldPos;
        out vec4 o_Color;

        uniform vec3 u_BaseColor;
        uniform vec3 u_CameraWorldPos;

        void main() {
            vec3 normal = normalize(v_Normal);
            vec3 viewDir = normalize(u_CameraWorldPos - v_WorldPos);

            // Face voltada para "dentro" (de costas para a camera) - ver
            // comentario grande acima de s_VertexSrc sobre o motivo de
            // usar discard em vez de glCullFace aqui.
            if (dot(normal, viewDir) < 0.0)
                discard;

            vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3));
            float diffuse = max(dot(normal, lightDir), 0.0);
            vec3 ambient = u_BaseColor * 0.25;
            vec3 color = ambient + u_BaseColor * diffuse;
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

    void Renderer::DrawMesh(PrimitiveMesh meshType, const float* viewProjection, const float* model, const float* color) {
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

        mesh->Bind();
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

}
