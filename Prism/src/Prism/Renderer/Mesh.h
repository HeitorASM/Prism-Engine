#pragma once

// ============================================================================
// Mesh.h
// Wrapper generico de uma malha na GPU (VAO + VBO + EBO). Layout de vertice
// FIXO por enquanto: posicao (3 floats) + normal (3 floats) - o suficiente
// para o shader basico com iluminacao direcional fake que o Renderer usa
// hoje (ver Renderer.cpp). UVs entram quando texturas existirem.
//
// Existe para o Renderer nao repetir a mesma sequencia de
// glCreateVertexArrays/glCreateBuffers/glVertexAttribPointer uma vez por
// primitiva (Cube, Sphere, Capsule, Cylinder, Plane) - cada primitiva vira
// so "gerar vertices/indices na CPU" + "new Mesh(vertices, indices)".
// ============================================================================

#include "../Core/Base.h"
#include <vector>
#include <utility>
#include <cstdint>

struct GLFWwindow;


namespace Prism {

    struct MeshVertex {
        float Position[3];
        float Normal[3];
    };

    class Mesh {
    public:
        Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh();

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;

        void Bind() const;
        uint32_t GetIndexCount() const { return m_IndexCount; }

        static Scope<Mesh> Create(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);

        // VAOs NAO sao compartilhados entre contextos OpenGL com share list
        // (diferente de buffers/texturas/shaders - ver
        // https://www.khronos.org/opengl/wiki/OpenGL_Object#Object_Sharing,
        // secao "container objects"). Isso significa que o m_VAO criado no
        // construtor (sempre no contexto do editor, ja que todo Mesh e
        // criado por Renderer::Init(), chamado uma vez so) e INVALIDO em
        // qualquer outro contexto - como o da PlayWindow, mesmo com
        // sharedContextWindow passado para glfwCreateWindow.
        //
        // BindForCurrentContext() resolve isso: cria (uma vez, e cacheia)
        // um VAO ADICIONAL valido no contexto atualmente current, apontando
        // para os MESMOS m_VBO/m_EBO (esses sim compartilhados de verdade -
        // sao buffer objects, nao um container object). Chamado pela
        // PlayWindow antes do primeiro desenho em seu proprio contexto (ver
        // PlayWindow::Open). No contexto do editor continua tudo
        // funcionando via o m_VAO original / Bind() normal.
        void BindForCurrentContext() const;

    private:
        uint32_t m_VAO = 0;
        uint32_t m_VBO = 0;
        uint32_t m_EBO = 0;
        uint32_t m_IndexCount = 0;

        // Um VAO extra por GLFWwindow* cujo contexto ja tenha desenhado
        // este Mesh (na pratica, hoje: no maximo 2 entradas - editor
        // [m_VAO acima, criado no construtor] e uma PlayWindow por vez).
        // mutable porque BindForCurrentContext() e const (Bind() tambem e)
        // - criar um VAO sob demanda no primeiro uso e um detalhe de cache
        // interno, nao muda o "estado logico" do Mesh do ponto de vista de
        // quem chama.
        mutable std::vector<std::pair<::GLFWwindow*, uint32_t>> m_ContextVAOs;
    };

}
