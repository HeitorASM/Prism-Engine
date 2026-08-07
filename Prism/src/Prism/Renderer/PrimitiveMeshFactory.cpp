#include "PrimitiveMeshFactory.h"
#include <cmath>

namespace Prism {

    static constexpr float kPi = 3.14159265358979323846f;

    GeneratedMesh PrimitiveMeshFactory::CreateCube() {
        GeneratedMesh mesh;

        // Mesmos dados que ja existiam em Renderer.cpp antes desta
        // refatoracao - 4 vertices por face (normais nao suavizadas),
        // reorganizados aqui no formato MeshVertex.
        float raw[] = {
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

        for (int i = 0; i < 24; i++) {
            MeshVertex v;
            v.Position[0] = raw[i * 6 + 0]; v.Position[1] = raw[i * 6 + 1]; v.Position[2] = raw[i * 6 + 2];
            v.Normal[0]   = raw[i * 6 + 3]; v.Normal[1]   = raw[i * 6 + 4]; v.Normal[2]   = raw[i * 6 + 5];
            mesh.Vertices.push_back(v);
        }

        mesh.Indices = {
             0, 1, 2,  0, 2, 3,       // +X
             4, 5, 6,  4, 6, 7,       // -X
             8, 9,10,  8,10,11,       // +Y
            12,13,14, 12,14,15,       // -Y
            16,17,18, 16,18,19,       // +Z
            20,21,22, 20,22,23,       // -Z
        };

        return mesh;
    }

    GeneratedMesh PrimitiveMeshFactory::CreateSphere(uint32_t latSegments, uint32_t lonSegments) {
        GeneratedMesh mesh;
        constexpr float radius = 0.5f;

        // Esfera UV padrao: um anel de vertices por "linha de latitude",
        // do polo sul (lat=0) ao polo norte (lat=latSegments). Cada
        // vertice tem normal = posicao normalizada (esfera centrada na
        // origem - a normal de um ponto na superficie e sempre a direcao
        // radial a partir do centro).
        for (uint32_t lat = 0; lat <= latSegments; lat++) {
            float theta = lat * kPi / latSegments;         // 0 (polo sul) .. PI (polo norte)
            float sinTheta = sinf(theta), cosTheta = cosf(theta);

            for (uint32_t lon = 0; lon <= lonSegments; lon++) {
                float phi = lon * 2.0f * kPi / lonSegments; // 0 .. 2PI ao redor do eixo Y
                float sinPhi = sinf(phi), cosPhi = cosf(phi);

                MeshVertex v;
                float nx = cosPhi * sinTheta, ny = cosTheta, nz = sinPhi * sinTheta;
                v.Normal[0] = nx; v.Normal[1] = ny; v.Normal[2] = nz;
                v.Position[0] = nx * radius; v.Position[1] = ny * radius; v.Position[2] = nz * radius;
                mesh.Vertices.push_back(v);
            }
        }

        // Cada "quad" entre duas linhas de latitude/longitude vira 2
        // triangulos - (lonSegments+1) vertices por linha por causa do
        // vertice duplicado no lon=0/lon=2PI (necessario para UV/costura
        // correta - mesmo sem UV ainda hoje, mantem a topologia pronta).
        uint32_t vertsPerRow = lonSegments + 1;
        for (uint32_t lat = 0; lat < latSegments; lat++) {
            for (uint32_t lon = 0; lon < lonSegments; lon++) {
                uint32_t a = lat * vertsPerRow + lon;
                uint32_t b = a + vertsPerRow;
                mesh.Indices.push_back(a);
                mesh.Indices.push_back(b);
                mesh.Indices.push_back(a + 1);

                mesh.Indices.push_back(a + 1);
                mesh.Indices.push_back(b);
                mesh.Indices.push_back(b + 1);
            }
        }

        return mesh;
    }

    GeneratedMesh PrimitiveMeshFactory::CreateCapsule(float height, uint32_t segments) {
        GeneratedMesh mesh;
        constexpr float radius = 0.5f;
        float halfHeight = height * 0.5f;

        // Capsula = cilindro lateral + meia-esfera em cada tampa. Construida
        // como uma unica "esfera esticada": percorremos aneis de latitude
        // de -90 (polo sul) a +90 (polo norte) igual CreateSphere, mas
        // deslocamos os aneis da metade de cima para +halfHeight e os da
        // metade de baixo para -halfHeight, criando o trecho cilindrico reto
        // no meio em vez de uma esfera comum.
        uint32_t latSegments = segments; // resolucao vertical (metade em cada polo)
        uint32_t vertsPerRow = segments + 1;

        for (uint32_t lat = 0; lat <= latSegments; lat++) {
            float theta = lat * kPi / latSegments; // 0 (polo sul) .. PI (polo norte)
            float sinTheta = sinf(theta), cosTheta = cosf(theta);

            // Desloca o anel para a tampa correspondente: metade inferior
            // (theta > PI/2, cosTheta < 0) desloca para baixo; metade
            // superior desloca para cima. Isso mantem as duas meias-esferas
            // completas, unidas por um cilindro reto de altura 'height'.
            float yOffset = (cosTheta >= 0.0f) ? halfHeight : -halfHeight;

            for (uint32_t lon = 0; lon <= segments; lon++) {
                float phi = lon * 2.0f * kPi / segments;
                float sinPhi = sinf(phi), cosPhi = cosf(phi);

                MeshVertex v;
                float nx = cosPhi * sinTheta, ny = cosTheta, nz = sinPhi * sinTheta;
                v.Normal[0] = nx; v.Normal[1] = ny; v.Normal[2] = nz;
                v.Position[0] = nx * radius;
                v.Position[1] = ny * radius + yOffset;
                v.Position[2] = nz * radius;
                mesh.Vertices.push_back(v);
            }
        }

        for (uint32_t lat = 0; lat < latSegments; lat++) {
            for (uint32_t lon = 0; lon < segments; lon++) {
                uint32_t a = lat * vertsPerRow + lon;
                uint32_t b = a + vertsPerRow;
                mesh.Indices.push_back(a);
                mesh.Indices.push_back(b);
                mesh.Indices.push_back(a + 1);

                mesh.Indices.push_back(a + 1);
                mesh.Indices.push_back(b);
                mesh.Indices.push_back(b + 1);
            }
        }

        return mesh;
    }

    GeneratedMesh PrimitiveMeshFactory::CreateCylinder(uint32_t segments) {
        GeneratedMesh mesh;
        constexpr float radius = 0.5f;
        constexpr float halfHeight = 0.5f;

        // Corpo lateral: dois aneis (topo e base), normais apontando para
        // fora radialmente (sem componente Y - lateral reta, nao cone).
        for (uint32_t ring = 0; ring < 2; ring++) {
            float y = (ring == 0) ? -halfHeight : halfHeight;
            for (uint32_t i = 0; i <= segments; i++) {
                float phi = i * 2.0f * kPi / segments;
                float cosPhi = cosf(phi), sinPhi = sinf(phi);

                MeshVertex v;
                v.Position[0] = cosPhi * radius; v.Position[1] = y; v.Position[2] = sinPhi * radius;
                v.Normal[0] = cosPhi; v.Normal[1] = 0.0f; v.Normal[2] = sinPhi;
                mesh.Vertices.push_back(v);
            }
        }

        uint32_t vertsPerRing = segments + 1;
        for (uint32_t i = 0; i < segments; i++) {
            uint32_t bottomA = i, bottomB = i + 1;
            uint32_t topA = vertsPerRing + i, topB = vertsPerRing + i + 1;
            mesh.Indices.push_back(bottomA);
            mesh.Indices.push_back(topA);
            mesh.Indices.push_back(bottomB);

            mesh.Indices.push_back(bottomB);
            mesh.Indices.push_back(topA);
            mesh.Indices.push_back(topB);
        }

        // Tampas: um "leque de triangulos" a partir de um vertice central,
        // com normal reta para cima (topo) ou para baixo (base) - vertices
        // proprios (nao reaproveita os do corpo lateral) porque a normal e
        // completamente diferente (reta vs radial), e normais nao podem
        // ser compartilhadas entre um vertice usado para superficies com
        // orientacoes diferentes sem criar sombreamento errado.
        auto addCap = [&](float y, float normalY, bool reverseWinding) {
            uint32_t centerIndex = (uint32_t)mesh.Vertices.size();
            MeshVertex center;
            center.Position[0] = 0.0f; center.Position[1] = y; center.Position[2] = 0.0f;
            center.Normal[0] = 0.0f; center.Normal[1] = normalY; center.Normal[2] = 0.0f;
            mesh.Vertices.push_back(center);

            uint32_t ringStart = (uint32_t)mesh.Vertices.size();
            for (uint32_t i = 0; i <= segments; i++) {
                float phi = i * 2.0f * kPi / segments;
                MeshVertex v;
                v.Position[0] = cosf(phi) * radius; v.Position[1] = y; v.Position[2] = sinf(phi) * radius;
                v.Normal[0] = 0.0f; v.Normal[1] = normalY; v.Normal[2] = 0.0f;
                mesh.Vertices.push_back(v);
            }

            for (uint32_t i = 0; i < segments; i++) {
                uint32_t a = ringStart + i, b = ringStart + i + 1;
                if (reverseWinding) {
                    mesh.Indices.push_back(centerIndex);
                    mesh.Indices.push_back(b);
                    mesh.Indices.push_back(a);
                } else {
                    mesh.Indices.push_back(centerIndex);
                    mesh.Indices.push_back(a);
                    mesh.Indices.push_back(b);
                }
            }
        };

        addCap(-halfHeight, -1.0f, true);  // base - vista de baixo, winding invertido para continuar culling-friendly
        addCap(halfHeight, 1.0f, false);   // topo

        return mesh;
    }

    GeneratedMesh PrimitiveMeshFactory::CreatePlane() {
        GeneratedMesh mesh;
        constexpr float half = 0.5f;

        mesh.Vertices = {
            { { -half, 0.0f, -half }, { 0, 1, 0 } },
            { {  half, 0.0f, -half }, { 0, 1, 0 } },
            { {  half, 0.0f,  half }, { 0, 1, 0 } },
            { { -half, 0.0f,  half }, { 0, 1, 0 } },
        };
        mesh.Indices = { 0, 1, 2, 0, 2, 3 };

        return mesh;
    }

}
