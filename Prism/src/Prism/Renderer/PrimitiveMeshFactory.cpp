#include "PrimitiveMeshFactory.h"
#include <cmath>
#include <glm/glm.hpp>

namespace Prism {

    static constexpr float kPi = 3.14159265358979323846f;

    void PrimitiveMeshFactory::CalculateTangents(GeneratedMesh& mesh) {
        std::vector<glm::vec3> accumulatedTangents(mesh.Vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
            uint32_t i0 = mesh.Indices[i + 0];
            uint32_t i1 = mesh.Indices[i + 1];
            uint32_t i2 = mesh.Indices[i + 2];

            const MeshVertex& v0 = mesh.Vertices[i0];
            const MeshVertex& v1 = mesh.Vertices[i1];
            const MeshVertex& v2 = mesh.Vertices[i2];

            glm::vec3 pos0(v0.Position[0], v0.Position[1], v0.Position[2]);
            glm::vec3 pos1(v1.Position[0], v1.Position[1], v1.Position[2]);
            glm::vec3 pos2(v2.Position[0], v2.Position[1], v2.Position[2]);

            glm::vec2 uv0(v0.UV[0], v0.UV[1]);
            glm::vec2 uv1(v1.UV[0], v1.UV[1]);
            glm::vec2 uv2(v2.UV[0], v2.UV[1]);

            glm::vec3 edge1 = pos1 - pos0;
            glm::vec3 edge2 = pos2 - pos0;
            glm::vec2 deltaUV1 = uv1 - uv0;
            glm::vec2 deltaUV2 = uv2 - uv0;

            float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;

            glm::vec3 tangent(1.0f, 0.0f, 0.0f);
            if (std::fabs(denom) > 1e-8f) {
                float invDenom = 1.0f / denom;
                tangent = invDenom * (deltaUV2.y * edge1 - deltaUV1.y * edge2);
            }

            accumulatedTangents[i0] += tangent;
            accumulatedTangents[i1] += tangent;
            accumulatedTangents[i2] += tangent;
        }

        for (size_t i = 0; i < mesh.Vertices.size(); i++) {
            glm::vec3 normal(mesh.Vertices[i].Normal[0], mesh.Vertices[i].Normal[1], mesh.Vertices[i].Normal[2]);
            glm::vec3 tangent = accumulatedTangents[i];

            if (glm::length(tangent) > 1e-8f) {
                tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));
            } else {
                glm::vec3 arbitrary = (std::fabs(normal.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                tangent = glm::normalize(glm::cross(normal, arbitrary));
            }

            mesh.Vertices[i].Tangent[0] = tangent.x;
            mesh.Vertices[i].Tangent[1] = tangent.y;
            mesh.Vertices[i].Tangent[2] = tangent.z;
        }
    }

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

            static const float faceUVs[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
            int cornerInFace = i % 4;
            v.UV[0] = faceUVs[cornerInFace][0];
            v.UV[1] = faceUVs[cornerInFace][1];

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

        CalculateTangents(mesh);
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
                v.UV[0] = (float)lon / (float)lonSegments;
                v.UV[1] = (float)lat / (float)latSegments;
                mesh.Vertices.push_back(v);
            }
        }

        // Cada "quad" entre duas linhas de latitude/longitude vira 2
        // triangulos - (lonSegments+1) vertices por linha por causa do
        // vertice duplicado no lon=0/lon=2PI (necessario para a costura de
        // UV ficar correta).
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

        CalculateTangents(mesh);
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

                float totalHalfHeight = halfHeight + radius;
                v.UV[0] = (float)lon / (float)segments;
                v.UV[1] = (v.Position[1] + totalHalfHeight) / (2.0f * totalHalfHeight);

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

        CalculateTangents(mesh);
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
                v.UV[0] = (float)i / (float)segments;
                v.UV[1] = (float)ring;
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
            center.UV[0] = 0.5f; center.UV[1] = 0.5f;
            mesh.Vertices.push_back(center);

            uint32_t ringStart = (uint32_t)mesh.Vertices.size();
            for (uint32_t i = 0; i <= segments; i++) {
                float phi = i * 2.0f * kPi / segments;
                MeshVertex v;
                v.Position[0] = cosf(phi) * radius; v.Position[1] = y; v.Position[2] = sinf(phi) * radius;
                v.Normal[0] = 0.0f; v.Normal[1] = normalY; v.Normal[2] = 0.0f;
                v.UV[0] = 0.5f + cosf(phi) * 0.5f;
                v.UV[1] = 0.5f + sinf(phi) * 0.5f;
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

        CalculateTangents(mesh);
        return mesh;
    }

    GeneratedMesh PrimitiveMeshFactory::CreatePlane() {
        GeneratedMesh mesh;
        constexpr float half = 0.5f;

        mesh.Vertices = {
            { { -half, 0.0f, -half }, { 0, 1, 0 }, { 0.0f, 0.0f } },
            { {  half, 0.0f, -half }, { 0, 1, 0 }, { 1.0f, 0.0f } },
            { {  half, 0.0f,  half }, { 0, 1, 0 }, { 1.0f, 1.0f } },
            { { -half, 0.0f,  half }, { 0, 1, 0 }, { 0.0f, 1.0f } },
        };
        mesh.Indices = { 0, 1, 2, 0, 2, 3 };

        CalculateTangents(mesh);
        return mesh;
    }

    LocalBounds PrimitiveMeshFactory::GetLocalBounds(PrimitiveMesh mesh) {
        switch (mesh) {
            case PrimitiveMesh::Cube:
                // Cubo unitario 1x1x1 centrado na origem (ver CreateCube).
                return { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };

            case PrimitiveMesh::Sphere:
                // Esfera unitaria de raio 0.5 centrada na origem (ver CreateSphere).
                return { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };

            case PrimitiveMesh::Capsule: {
                // Capsula = cilindro de altura 'height' (default 1.0, ver
                // CreateCapsule) entre os centros das tampas + uma
                // meia-esfera de raio 0.5 em cada ponta - altura TOTAL e
                // height + 2*0.5 (raio das pontas), largura/profundidade
                // sempre 0.5 (mesmo raio dos hemisferios). Usamos o
                // 'height' default (1.0) aqui: GetLocalBounds nao recebe os
                // parametros de resolucao/altura usados na geracao porque
                // ColliderComponent (que teria o dado real por entidade) e
                // um component separado do MeshRendererComponent - mesma
                // limitacao que ColliderComponent::Size ja documenta
                // (visual e collider sao independentes nesta engine, ver
                // Components.h). Serve bem o suficiente para picking, que
                // so precisa de um teste aproximado.
                constexpr float defaultHeight = 1.0f;
                float halfHeight = defaultHeight * 0.5f + 0.5f;
                return { { -0.5f, -halfHeight, -0.5f }, { 0.5f, halfHeight, 0.5f } };
            }

            case PrimitiveMesh::Cylinder:
                // Cilindro de raio 0.5, altura 1.0, centrado na origem (ver CreateCylinder).
                return { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };

            case PrimitiveMesh::Plane:
                // Plano 1x1 no plano XZ (ver CreatePlane) - espessura zero
                // no Y por definicao. Scene::Raycast trata este caso
                // especial (ver comentario la) para nao falhar sempre que
                // o raio for quase paralelo ao plano.
                return { { -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f } };
        }

        // Nunca deveria chegar aqui (switch cobre todos os valores do enum) -
        // fallback conservador do tamanho do cubo, so por seguranca.
        return { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };
    }

}
