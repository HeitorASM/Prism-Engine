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
#include <cstdint>

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

    private:
        uint32_t m_VAO = 0;
        uint32_t m_VBO = 0;
        uint32_t m_EBO = 0;
        uint32_t m_IndexCount = 0;
    };

}
