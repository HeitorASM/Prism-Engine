#include <glad/gl.h>
#include "Renderer.h"
#include "PrimitiveMeshFactory.h"
#include "../Core/Log.h"

namespace Prism {

    Ref<Shader> Renderer::s_BasicShader = nullptr;
    Scope<Mesh> Renderer::s_Meshes[5] = {};

    // Shader minimo: posicao + normal, iluminacao direcional simples "fake"
    // (um unico dot product) so para as primitivas nao parecerem uma
    // silhueta plana sem nenhuma pista de profundidade/forma.
    static const char* s_VertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Model;

        out vec3 v_Normal;

        void main() {
            v_Normal = mat3(u_Model) * a_Normal;
            gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_FragmentSrc = R"(
        #version 450 core
        in vec3 v_Normal;
        out vec4 o_Color;

        uniform vec3 u_BaseColor;

        void main() {
            vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3));
            float diffuse = max(dot(normalize(v_Normal), lightDir), 0.0);
            vec3 ambient = u_BaseColor * 0.25;
            vec3 color = ambient + u_BaseColor * diffuse;
            o_Color = vec4(color, 1.0);
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

        PRISM_CORE_INFO("Renderer inicializado (shader basico + 5 primitivas: Cube, Sphere, Capsule, Cylinder, Plane).");
    }

    void Renderer::Shutdown() {
        for (auto& mesh : s_Meshes)
            mesh.reset();
        s_BasicShader.reset();
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
        if (color)
            s_BasicShader->SetFloat3("u_BaseColor", color[0], color[1], color[2]);
        else
            s_BasicShader->SetFloat3("u_BaseColor", 0.85f, 0.55f, 0.2f);

        mesh->Bind();
        glDrawElements(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        s_BasicShader->Unbind();
    }

}
