#include <glad/gl.h>
#include "Renderer.h"
#include "../Core/Log.h"

namespace Prism {

    Ref<Shader> Renderer::s_BasicShader = nullptr;
    uint32_t Renderer::s_CubeVAO = 0;
    uint32_t Renderer::s_CubeVBO = 0;
    uint32_t Renderer::s_CubeEBO = 0;

    // Shader minimo: posicao + normal, iluminacao direcional simples "fake"
    // (um unico dot product) so para o cubo nao parecer uma silhueta plana
    // sem nenhuma pista de profundidade/forma.
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

    void Renderer::Init() {
        s_BasicShader = Shader::Create("BasicLit", s_VertexSrc, s_FragmentSrc);

        // Cubo unitario: 24 vertices (4 por face, para normais corretas por
        // face em vez de normais suavizadas de vertice compartilhado).
        // Layout por vertice: posicao (3 floats) + normal (3 floats).
        float vertices[] = {
            // +X
             0.5f, -0.5f, -0.5f,  1,0,0,   0.5f,  0.5f, -0.5f,  1,0,0,   0.5f,  0.5f,  0.5f,  1,0,0,   0.5f, -0.5f,  0.5f,  1,0,0,
            // -X
            -0.5f, -0.5f,  0.5f, -1,0,0,  -0.5f,  0.5f,  0.5f, -1,0,0,  -0.5f,  0.5f, -0.5f, -1,0,0,  -0.5f, -0.5f, -0.5f, -1,0,0,
            // +Y
            -0.5f,  0.5f, -0.5f,  0,1,0,  -0.5f,  0.5f,  0.5f,  0,1,0,   0.5f,  0.5f,  0.5f,  0,1,0,   0.5f,  0.5f, -0.5f,  0,1,0,
            // -Y
            -0.5f, -0.5f,  0.5f,  0,-1,0, -0.5f, -0.5f, -0.5f,  0,-1,0,  0.5f, -0.5f, -0.5f,  0,-1,0,  0.5f, -0.5f,  0.5f,  0,-1,0,
            // +Z
            -0.5f, -0.5f,  0.5f,  0,0,1,   0.5f, -0.5f,  0.5f,  0,0,1,   0.5f,  0.5f,  0.5f,  0,0,1,  -0.5f,  0.5f,  0.5f,  0,0,1,
            // -Z
             0.5f, -0.5f, -0.5f,  0,0,-1, -0.5f, -0.5f, -0.5f,  0,0,-1, -0.5f,  0.5f, -0.5f,  0,0,-1,  0.5f,  0.5f, -0.5f,  0,0,-1,
        };

        uint32_t indices[] = {
             0, 1, 2,  0, 2, 3,       // +X
             4, 5, 6,  4, 6, 7,       // -X
             8, 9,10,  8,10,11,       // +Y
            12,13,14, 12,14,15,       // -Y
            16,17,18, 16,18,19,       // +Z
            20,21,22, 20,22,23,       // -Z
        };

        glCreateVertexArrays(1, &s_CubeVAO);
        glBindVertexArray(s_CubeVAO);

        glCreateBuffers(1, &s_CubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, s_CubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glCreateBuffers(1, &s_CubeEBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_CubeEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        const GLsizei stride = 6 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));

        glBindVertexArray(0);

        PRISM_CORE_INFO("Renderer inicializado (shader basico + cubo de teste).");
    }

    void Renderer::Shutdown() {
        glDeleteVertexArrays(1, &s_CubeVAO);
        glDeleteBuffers(1, &s_CubeVBO);
        glDeleteBuffers(1, &s_CubeEBO);
        s_BasicShader.reset();
    }

    void Renderer::Clear(float r, float g, float b, float a) {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void Renderer::SetViewport(uint32_t width, uint32_t height) {
        glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    }

    void Renderer::DrawTestCube(const float* viewProjection, const float* model, const float* color) {
        if (!s_BasicShader) return;

        s_BasicShader->Bind();
        s_BasicShader->SetMat4("u_ViewProjection", viewProjection);
        s_BasicShader->SetMat4("u_Model", model);
        if (color)
            s_BasicShader->SetFloat3("u_BaseColor", color[0], color[1], color[2]);
        else
            s_BasicShader->SetFloat3("u_BaseColor", 0.85f, 0.55f, 0.2f);

        glBindVertexArray(s_CubeVAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        s_BasicShader->Unbind();
    }

}
