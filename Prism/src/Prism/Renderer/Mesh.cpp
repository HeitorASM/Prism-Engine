#include <glad/gl.h>
#include "Mesh.h"
#include <GLFW/glfw3.h>

namespace Prism {

    Scope<Mesh> Mesh::Create(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices) {
        return CreateScope<Mesh>(vertices, indices);
    }

    Mesh::Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices)
        : m_IndexCount((uint32_t)indices.size()) {
        glCreateVertexArrays(1, &m_VAO);
        glBindVertexArray(m_VAO);

        glCreateBuffers(1, &m_VBO);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(MeshVertex), vertices.data(), GL_STATIC_DRAW);

        glCreateBuffers(1, &m_EBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (const void*)offsetof(MeshVertex, Position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (const void*)offsetof(MeshVertex, Normal));

        glBindVertexArray(0);
    }

    Mesh::~Mesh() {
        glDeleteVertexArrays(1, &m_VAO);
        for (auto& [window, vao] : m_ContextVAOs)
            glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &m_VBO);
        glDeleteBuffers(1, &m_EBO);
    }

    void Mesh::Bind() const {
        glBindVertexArray(m_VAO);
    }

    void Mesh::BindForCurrentContext() const {
        GLFWwindow* current = glfwGetCurrentContext();

        // Contexto do editor (o mesmo que criou m_VAO no construtor) - o
        // VAO original ja e valido aqui, nada de especial a fazer.
        // glfwGetCurrentContext() dentro do construtor nao foi guardado
        // (Mesh nao sabia sobre GLFW ate agora), entao comparamos pelo
        // primeiro Bind() bem-sucedido de forma preguicosa: se ainda nao
        // ha nenhuma entrada em m_ContextVAOs E este e o primeiro contexto
        // a pedir bind, assumimos que e o mesmo contexto do construtor
        // (sempre verdade na pratica hoje - Renderer::Init() so roda uma
        // vez, no contexto do editor, antes de qualquer PlayWindow existir).
        static thread_local GLFWwindow* s_OriginalContext = nullptr;
        if (!s_OriginalContext)
            s_OriginalContext = current;

        if (current == s_OriginalContext) {
            glBindVertexArray(m_VAO);
            return;
        }

        for (auto& [window, vao] : m_ContextVAOs) {
            if (window == current) {
                glBindVertexArray(vao);
                return;
            }
        }

        // Primeiro desenho deste Mesh neste contexto novo - cria um VAO
        // ADICIONAL aqui (VAOs nao sao compartilhados entre contextos, ver
        // comentario em Mesh.h) apontando para os MESMOS m_VBO/m_EBO (esses
        // sim compartilhados de verdade, ja que sao buffer objects). O
        // layout de atributo (glVertexAttribPointer) precisa ser refeito
        // aqui porque e estado do VAO, nao do buffer.
        uint32_t vao;
        glCreateVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (const void*)offsetof(MeshVertex, Position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (const void*)offsetof(MeshVertex, Normal));

        m_ContextVAOs.emplace_back(current, vao);
    }

}
