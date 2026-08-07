#pragma once

// ============================================================================
// PrimitiveMeshFactory.h
// Gera os VERTICES/INDICES (na CPU, em memoria comum) das primitivas
// embutidas da engine (Cube, Sphere, Capsule, Cylinder, Plane). Separado do
// Renderer de proposito: isto e matematica pura (nenhuma chamada OpenGL
// aqui), o Renderer so pega o resultado e cria um Mesh (GPU) a partir dele
// em Renderer::Init(). Facilita testar a geometria isoladamente e mantem
// Renderer.cpp focado em orquestrar GPU/shader, nao em trigonometria.
// ============================================================================

#include "Mesh.h"
#include <vector>

namespace Prism {

    struct GeneratedMesh {
        std::vector<MeshVertex> Vertices;
        std::vector<uint32_t> Indices;
    };

    class PrimitiveMeshFactory {
    public:
        // Cubo unitario (1x1x1), centrado na origem. Normais por face (nao
        // suavizadas) - 24 vertices, 4 por face, para uma silhueta com
        // arestas nitidas em vez de sombreamento arredondado nos cantos.
        static GeneratedMesh CreateCube();

        // Esfera UV unitaria (raio 0.5), centrada na origem.
        // latSegments/lonSegments controlam a resolucao - valores baixos o
        // suficiente para nao pesar em cenas com varias esferas, altos o
        // suficiente para nao parecer um poliedro obvio.
        static GeneratedMesh CreateSphere(uint32_t latSegments = 16, uint32_t lonSegments = 24);

        // Capsula: cilindro (raio 0.5, altura 'height' entre os centros das
        // duas tampas) com uma meia-esfera (raio 0.5) em cada ponta,
        // alinhada ao eixo Y. Forma padrao para colliders/visual de
        // characters (ver ColliderShape::Capsule em Components.h).
        static GeneratedMesh CreateCapsule(float height = 1.0f, uint32_t segments = 16);

        // Cilindro (raio 0.5, altura 1.0), alinhado ao eixo Y, com tampas
        // solidas no topo e na base.
        static GeneratedMesh CreateCylinder(uint32_t segments = 24);

        // Plano unitario (1x1) no plano XZ, normal apontando para +Y - util
        // como chao/piso de teste antes do sistema de brushes/BSP existir.
        static GeneratedMesh CreatePlane();
    };

}
