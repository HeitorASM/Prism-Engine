#pragma once

// ============================================================================
// Mesh.h
// Wrapper generico de uma malha na GPU (VAO + VBO + EBO). Layout de vertice:
// posicao (3 floats) + normal (3 floats) + UV (2 floats) + tangente (3
// floats) - ver MeshVertex abaixo. UV/Tangente sao usados pelo sistema de
// Material/PBR (MaterialComponent, Components.h) para amostrar texturas
// (albedo/normal/roughness-metallic) e orientar normal mapping
// corretamente em qualquer superficie, plana ou curva.
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

        // UV (coordenadas de textura, 0..1). Convencao padrao OpenGL: (0,0) e o
        // canto inferior-esquerdo da textura, U cresce para a direita, V
        // cresce para cima.
        float UV[2];

        // Tangente (espaco de MUNDO, apos transformacao pelo Model no
        // vertex shader - aqui em espaco de OBJETO/local, igual Normal
        // acima) - necessaria para NORMAL MAPPING: o valor lido de um
        // mapa de normais esta em TANGENT SPACE (Z = "para fora" da
        // superficie lisa, X/Y = variacao local) e precisa ser
        // transformado de volta para world-space usando a base
        // TBN (Tangent, Bitangent, Normal) antes de ser usado na
        // iluminacao - Bitangent e calculado no shader via
        // cross(Normal, Tangent), nao precisa ser armazenado aqui.
        // Calculada uma vez, por triangulo, a partir das diferencas de
        // Position/UV dos 3 vertices (ver
        // PrimitiveMeshFactory::CalculateTangents) - nunca em runtime.
        float Tangent[3];
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
