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
#include "../Scene/Components.h" // PrimitiveMesh
#include <glm/glm.hpp>
#include <vector>

namespace Prism {

    struct GeneratedMesh {
        std::vector<MeshVertex> Vertices;
        std::vector<uint32_t> Indices;
    };

    // Bounding box axis-aligned MINIMA/MAXIMA no espaco LOCAL (nao-transformado)
    // de uma primitiva - usada por raycast/picking (ver Scene::Raycast) para
    // um teste rapido de intersecao sem precisar iterar vertice por vertice
    // do mesh de verdade. Cada primitiva e gerada centrada na origem com
    // dimensoes unitarias (ver cada Create* acima) - por isso estes bounds
    // sao constantes conhecidas, nao precisam ser calculados a partir da
    // geometria gerada.
    struct LocalBounds {
        glm::vec3 Min;
        glm::vec3 Max;
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

        // Bounding box local (nao-transformada) da primitiva 'mesh' - ver
        // comentario de LocalBounds acima. Usada por Scene::Raycast para
        // testar um raio contra a entidade sem tocar na geometria de GPU.
        // O Plano tem espessura zero no eixo Y por definicao (e um
        // quadrilatero, nao um solido) - Scene::Raycast da a ele uma
        // pequena espessura minima so para o teste de raio nao falhar
        // sempre que o raio for exatamente paralelo ao plano (ver
        // comentario la).
        static LocalBounds GetLocalBounds(PrimitiveMesh mesh);

    private:
        // Preenche MeshVertex::Tangent de cada vertice em 'mesh', a partir
        // das UVs ja preenchidas (ver cada Create* acima) - chamada
        // internamente no FIM de cada Create*, depois que Vertices/Indices
        // estao completos. Ver PrimitiveMeshFactory.cpp para o algoritmo
        // (media dos tangentes por-triangulo, ortogonalizada via
        // Gram-Schmidt).
        static void CalculateTangents(GeneratedMesh& mesh);
    };

}
