#include "EditableMesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <istream>
#include <limits>
#include <map>
#include <numeric>
#include <ostream>
#include <unordered_map>
#include <unordered_set>

namespace Prism::Level {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;

        // Vetor de Newell: normal NAO normalizada com comprimento = 2 x area (anti-horario => para fora).
        glm::vec3 NewellVector(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices) {
            glm::vec3 n(0.0f);
            const size_t count = indices.size();
            for (size_t i = 0; i < count; ++i) {
                const glm::vec3& a = vertices[indices[i]];
                const glm::vec3& b = vertices[indices[(i + 1) % count]];
                n.x += (a.y - b.y) * (a.z + b.z);
                n.y += (a.z - b.z) * (a.x + b.x);
                n.z += (a.x - b.x) * (a.y + b.y);
            }
            return n;
        }

        glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
            const float length = glm::length(v);
            return (std::isfinite(length) && length > 1e-12f) ? v / length : fallback;
        }

        bool FaceIsUsable(const std::vector<glm::vec3>& vertices, const Face& face) {
            if (face.Indices.size() < 3)
                return false;
            for (uint32_t index : face.Indices)
                if (index >= vertices.size())
                    return false;
            return true;
        }

        // Base (u, v) do plano da face, com u x v = normal: anti-horario em 3D == anti-horario em (u, v).
        // Chao/teto: u = +X. Parede: u = "direita" de quem olha de fora, v = para cima.
        // Mesma base em todas as faces coplanares => UV continuo entre elas.
        void FaceBasis(const glm::vec3& normal, glm::vec3& u, glm::vec3& v) {
            const glm::vec3 reference = (std::abs(normal.y) >= 0.70710678f)
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : SafeNormalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), normal), glm::vec3(1.0f, 0.0f, 0.0f));
            u = SafeNormalize(reference - normal * glm::dot(normal, reference), glm::vec3(1.0f, 0.0f, 0.0f));
            v = glm::cross(normal, u);
        }

        // Ear clipping de um poligono PLANAR anti-horario em 2D (aceita concavo). Devolve triangulos como
        // indices locais (de 3 em 3). Sempre termina: sem orelha valida, corta o canto mais convexo.
        void Triangulate2D(const std::vector<glm::vec2>& p, std::vector<uint32_t>& out) {
            const size_t n = p.size();
            if (n < 3)
                return;
            std::vector<uint32_t> remaining(n);
            std::iota(remaining.begin(), remaining.end(), 0u);

            auto cross2 = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
                return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            };
            auto insideTriangle = [&](const glm::vec2& pt, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
                const float eps = -1e-9f;
                return cross2(a, b, pt) >= eps && cross2(b, c, pt) >= eps && cross2(c, a, pt) >= eps;
            };

            while (remaining.size() > 3) {
                const size_t m = remaining.size();
                int bestIndex = -1;
                float bestCross = -std::numeric_limits<float>::max();
                int earIndex = -1;
                for (size_t i = 0; i < m && earIndex < 0; ++i) {
                    const uint32_t ia = remaining[(i + m - 1) % m], ib = remaining[i], ic = remaining[(i + 1) % m];
                    const float c = cross2(p[ia], p[ib], p[ic]);
                    if (c > bestCross) { bestCross = c; bestIndex = (int)i; }
                    if (c <= 1e-12f)
                        continue; // reflexo ou colinear: nao e orelha
                    bool empty = true;
                    for (size_t k = 0; k < m && empty; ++k) {
                        const uint32_t other = remaining[k];
                        if (other == ia || other == ib || other == ic)
                            continue;
                        if (insideTriangle(p[other], p[ia], p[ib], p[ic]))
                            empty = false;
                    }
                    if (empty)
                        earIndex = (int)i;
                }
                const int clip = earIndex >= 0 ? earIndex : bestIndex;
                const size_t i = (size_t)clip;
                out.push_back(remaining[(i + m - 1) % m]);
                out.push_back(remaining[i]);
                out.push_back(remaining[(i + 1) % m]);
                remaining.erase(remaining.begin() + (long)i);
            }
            out.push_back(remaining[0]);
            out.push_back(remaining[1]);
            out.push_back(remaining[2]);
        }

        // Triangulacao de uma face em indices LOCAIS (posicao dentro de face.Indices).
        void TriangulateFace(const std::vector<glm::vec3>& vertices, const Face& face, const glm::vec3& normal, std::vector<uint32_t>& out) {
            glm::vec3 u, v;
            FaceBasis(normal, u, v);
            std::vector<glm::vec2> projected;
            projected.reserve(face.Indices.size());
            for (uint32_t index : face.Indices)
                projected.emplace_back(glm::dot(vertices[index], u), glm::dot(vertices[index], v));
            Triangulate2D(projected, out);
        }

        uint64_t EdgeKey(uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | (uint64_t)b; }

        std::vector<uint32_t> UniqueValidFaces(const std::vector<glm::vec3>& vertices, const std::vector<Face>& faces, const std::vector<uint32_t>& requested) {
            std::vector<uint32_t> result;
            for (uint32_t f : requested)
                if (f < faces.size() && FaceIsUsable(vertices, faces[f]))
                    result.push_back(f);
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        // Prisma: perfil 2D (z, y) anti-horario extrudado ao longo de X (largura). Fecha com duas tampas.
        EditableMesh ExtrudeProfile(const std::vector<glm::vec2>& profileZY, float width) {
            EditableMesh mesh;
            const uint32_t n = (uint32_t)profileZY.size();
            const float half = width * 0.5f;
            for (uint32_t i = 0; i < n; ++i) mesh.Vertices.emplace_back(-half, profileZY[i].y, profileZY[i].x);
            for (uint32_t i = 0; i < n; ++i) mesh.Vertices.emplace_back(+half, profileZY[i].y, profileZY[i].x);

            Face left;   // perfil anti-horario em (z, y) tem normal -X
            for (uint32_t i = 0; i < n; ++i) left.Indices.push_back(i);
            Face right;  // lado +X: ordem invertida
            for (uint32_t i = n; i-- > 0;) right.Indices.push_back(n + i);
            mesh.Faces.push_back(left);
            mesh.Faces.push_back(right);

            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t j = (i + 1) % n;
                Face wall;
                wall.Indices = { i, n + i, n + j, j };
                mesh.Faces.push_back(wall);
            }
            return mesh;
        }

        // --- E/S binaria -----------------------------------------------------------
        template <typename T> void WriteRaw(std::ostream& out, const T& value) {
            out.write(reinterpret_cast<const char*>(&value), sizeof(T));
        }
        template <typename T> bool ReadRaw(std::istream& in, T& value) {
            in.read(reinterpret_cast<char*>(&value), sizeof(T));
            return (bool)in;
        }
        constexpr uint32_t kMagic = 0x48534D50u; // 'PMSH'
        constexpr uint32_t kFormatVersion = 1;
        constexpr uint32_t kMaxVertices = 4u << 20;
        constexpr uint32_t kMaxFaces = 4u << 20;
        constexpr uint32_t kMaxFaceCorners = 4096;
    }

    // ==========================================================================
    // Formas
    // ==========================================================================

    EditableMesh EditableMesh::CreateBox(const glm::vec3& size) {
        const float hz = size.z * 0.5f, hy = size.y * 0.5f;
        return ExtrudeProfile({ { -hz, -hy }, { hz, -hy }, { hz, hy }, { -hz, hy } }, size.x);
    }

    EditableMesh EditableMesh::CreateRamp(const glm::vec3& size) {
        const float hz = size.z * 0.5f, hy = size.y * 0.5f;
        return ExtrudeProfile({ { -hz, -hy }, { hz, -hy }, { hz, hy } }, size.x);
    }

    EditableMesh EditableMesh::CreateStairs(const glm::vec3& size, uint32_t steps) {
        steps = std::min(std::max(steps, 1u), 256u);
        const float depth = size.z, height = size.y;
        const float n = (float)steps;
        std::vector<glm::vec2> profile = { { 0.0f, 0.0f }, { depth, 0.0f }, { depth, height } };
        for (uint32_t i = steps; i-- > 0;) {
            profile.emplace_back(depth * (float)i / n, height * (float)(i + 1) / n); // fim do degrau (piso)
            if (i > 0)
                profile.emplace_back(depth * (float)i / n, height * (float)i / n);   // espelho descendo
        }
        for (glm::vec2& point : profile) { point.x -= depth * 0.5f; point.y -= height * 0.5f; }
        return ExtrudeProfile(profile, size.x);
    }

    EditableMesh EditableMesh::CreatePlane(const glm::vec2& size, uint32_t subdivisionsX, uint32_t subdivisionsZ) {
        subdivisionsX = std::min(std::max(subdivisionsX, 1u), 256u);
        subdivisionsZ = std::min(std::max(subdivisionsZ, 1u), 256u);
        EditableMesh mesh;
        const uint32_t columns = subdivisionsX + 1;
        for (uint32_t z = 0; z <= subdivisionsZ; ++z)
            for (uint32_t x = 0; x <= subdivisionsX; ++x)
                mesh.Vertices.emplace_back(size.x * ((float)x / (float)subdivisionsX - 0.5f), 0.0f, size.y * ((float)z / (float)subdivisionsZ - 0.5f));
        for (uint32_t z = 0; z < subdivisionsZ; ++z) {
            for (uint32_t x = 0; x < subdivisionsX; ++x) {
                const uint32_t p00 = z * columns + x, p10 = p00 + 1, p01 = p00 + columns, p11 = p01 + 1;
                Face face;
                face.Indices = { p00, p01, p11, p10 }; // anti-horario visto de cima (+Y)
                mesh.Faces.push_back(face);
            }
        }
        return mesh;
    }

    EditableMesh EditableMesh::CreateCylinder(float radius, float height, uint32_t segments) {
        segments = std::min(std::max(segments, 3u), 512u);
        EditableMesh mesh;
        const float hy = height * 0.5f;
        for (uint32_t i = 0; i < segments; ++i) {
            const float angle = 2.0f * kPi * (float)i / (float)segments;
            mesh.Vertices.emplace_back(radius * std::cos(angle), -hy, -radius * std::sin(angle)); // anti-horario visto de cima
        }
        for (uint32_t i = 0; i < segments; ++i)
            mesh.Vertices.emplace_back(mesh.Vertices[i].x, hy, mesh.Vertices[i].z);

        Face top;
        for (uint32_t i = 0; i < segments; ++i) top.Indices.push_back(segments + i);
        Face bottom;
        for (uint32_t i = segments; i-- > 0;) bottom.Indices.push_back(i);
        mesh.Faces.push_back(top);
        mesh.Faces.push_back(bottom);
        for (uint32_t i = 0; i < segments; ++i) {
            const uint32_t j = (i + 1) % segments;
            Face side;
            side.Indices = { i, j, segments + j, segments + i };
            side.SmoothingGroup = 1;
            mesh.Faces.push_back(side);
        }
        return mesh;
    }

    // ==========================================================================
    // Consulta
    // ==========================================================================

    bool EditableMesh::IsValid(std::string* why) const {
        auto fail = [&](const std::string& reason) { if (why) *why = reason; return false; };
        for (size_t i = 0; i < Vertices.size(); ++i)
            if (!std::isfinite(Vertices[i].x) || !std::isfinite(Vertices[i].y) || !std::isfinite(Vertices[i].z))
                return fail("vertice " + std::to_string(i) + " com coordenada nao finita");
        for (size_t f = 0; f < Faces.size(); ++f) {
            if (Faces[f].Indices.size() < 3)
                return fail("face " + std::to_string(f) + " com menos de 3 vertices");
            for (uint32_t index : Faces[f].Indices)
                if (index >= Vertices.size())
                    return fail("face " + std::to_string(f) + " usa o vertice " + std::to_string(index) + " que nao existe");
        }
        return true;
    }

    bool EditableMesh::IsClosed() const {
        if (Faces.empty() || !IsValid())
            return false;
        std::unordered_map<uint64_t, int> directed;
        for (const Face& face : Faces) {
            const size_t count = face.Indices.size();
            for (size_t i = 0; i < count; ++i) {
                const uint32_t a = face.Indices[i], b = face.Indices[(i + 1) % count];
                if (a == b)
                    return false;
                if (++directed[EdgeKey(a, b)] > 1)
                    return false;
            }
        }
        for (const auto& [key, count] : directed) {
            const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFFu);
            if (directed.find(EdgeKey(b, a)) == directed.end())
                return false;
        }
        return true;
    }

    glm::vec3 EditableMesh::FaceNormal(uint32_t face) const {
        if (face >= Faces.size() || !FaceIsUsable(Vertices, Faces[face]))
            return glm::vec3(0.0f, 1.0f, 0.0f);
        return SafeNormalize(NewellVector(Vertices, Faces[face].Indices), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::vec3 EditableMesh::FaceCenter(uint32_t face) const {
        if (face >= Faces.size() || !FaceIsUsable(Vertices, Faces[face]))
            return glm::vec3(0.0f);
        glm::vec3 sum(0.0f);
        for (uint32_t index : Faces[face].Indices)
            sum += Vertices[index];
        return sum / (float)Faces[face].Indices.size();
    }

    float EditableMesh::FaceArea(uint32_t face) const {
        if (face >= Faces.size() || !FaceIsUsable(Vertices, Faces[face]))
            return 0.0f;
        return 0.5f * glm::length(NewellVector(Vertices, Faces[face].Indices));
    }

    void EditableMesh::GetBounds(glm::vec3& outMin, glm::vec3& outMax) const {
        if (Vertices.empty()) {
            outMin = outMax = glm::vec3(0.0f);
            return;
        }
        outMin = outMax = Vertices[0];
        for (const glm::vec3& v : Vertices) {
            outMin = glm::min(outMin, v);
            outMax = glm::max(outMax, v);
        }
    }

    std::vector<uint32_t> EditableMesh::VerticesOfFaces(const std::vector<uint32_t>& faces) const {
        std::vector<uint32_t> result;
        for (uint32_t f : faces) {
            if (f >= Faces.size())
                continue;
            for (uint32_t index : Faces[f].Indices)
                if (index < Vertices.size())
                    result.push_back(index);
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    // ==========================================================================
    // Edicao
    // ==========================================================================

    std::vector<uint32_t> EditableMesh::ExtrudeFaces(const std::vector<uint32_t>& facesRequested, float distance) {
        const std::vector<uint32_t> faces = UniqueValidFaces(Vertices, Faces, facesRequested);
        if (faces.empty() || !std::isfinite(distance))
            return {};

        // Arestas de BORDA da selecao: (a,b) cuja inversa (b,a) nao pertence a outra face selecionada.
        std::unordered_set<uint64_t> directed;
        for (uint32_t f : faces) {
            const std::vector<uint32_t>& idx = Faces[f].Indices;
            for (size_t i = 0; i < idx.size(); ++i)
                directed.insert(EdgeKey(idx[i], idx[(i + 1) % idx.size()]));
        }
        struct Wall { uint32_t A, B; uint32_t SourceFace; };
        std::vector<Wall> walls;
        for (uint32_t f : faces) {
            const std::vector<uint32_t>& idx = Faces[f].Indices;
            for (size_t i = 0; i < idx.size(); ++i) {
                const uint32_t a = idx[i], b = idx[(i + 1) % idx.size()];
                if (directed.find(EdgeKey(b, a)) == directed.end())
                    walls.push_back({ a, b, f });
            }
        }

        // Um vertice novo por vertice usado; anda pela media das normais das faces selecionadas que o usam.
        std::unordered_map<uint32_t, uint32_t> clone;
        std::unordered_map<uint32_t, glm::vec3> normalSum;
        for (uint32_t f : faces) {
            const glm::vec3 normal = FaceNormal(f);
            for (uint32_t index : Faces[f].Indices) {
                if (clone.find(index) == clone.end()) {
                    clone[index] = (uint32_t)Vertices.size();
                    const glm::vec3 position = Vertices[index];
                    Vertices.push_back(position);
                }
                normalSum[index] += normal;
            }
        }
        for (const auto& [original, copy] : clone) {
            const glm::vec3 direction = SafeNormalize(normalSum[original], glm::vec3(0.0f));
            Vertices[copy] = Vertices[original] + direction * distance;
        }

        // As faces selecionadas passam a usar os vertices movidos.
        for (uint32_t f : faces)
            for (uint32_t& index : Faces[f].Indices)
                index = clone[index];

        // Paredes nas bordas. A mesma ordem serve para os dois sentidos: ela contem a aresta A->B, que fecha
        // com a vizinha nao selecionada (que tem B->A), e B2->A2, que fecha com a face movida. Para dentro
        // (distance < 0) o segmento A2/B2 fica abaixo da borda, entao a normal inverte sozinha e a parede
        // fica virada para o vao.
        for (const Wall& wall : walls) {
            Face side;
            const Face& source = Faces[wall.SourceFace];
            side.MaterialSlot = source.MaterialSlot;
            side.UV = source.UV;
            const uint32_t a2 = clone[wall.A], b2 = clone[wall.B];
            side.Indices = { wall.A, wall.B, b2, a2 };
            Faces.push_back(side);
        }
        return faces;
    }

    std::vector<uint32_t> EditableMesh::InsetFaces(const std::vector<uint32_t>& facesRequested, float amount) {
        const std::vector<uint32_t> faces = UniqueValidFaces(Vertices, Faces, facesRequested);
        if (faces.empty() || !(amount > 0.0f) || !std::isfinite(amount))
            return {};

        std::vector<uint32_t> done;
        for (uint32_t f : faces) {
            const std::vector<uint32_t> outer = Faces[f].Indices;
            const size_t m = outer.size();
            const glm::vec3 normal = FaceNormal(f);

            std::vector<glm::vec3> inwardOfEdge(m); // para dentro, no plano, de cada aresta i -> i+1
            bool degenerate = false;
            for (size_t i = 0; i < m; ++i) {
                const glm::vec3 edge = Vertices[outer[(i + 1) % m]] - Vertices[outer[i]];
                if (glm::length(edge) < 1e-6f) { degenerate = true; break; }
                inwardOfEdge[i] = glm::cross(normal, glm::normalize(edge));
            }
            if (degenerate)
                continue;

            std::vector<uint32_t> inner(m);
            for (size_t i = 0; i < m; ++i) {
                const glm::vec3& before = inwardOfEdge[(i + m - 1) % m];
                const glm::vec3& after = inwardOfEdge[i];
                const float denominator = std::max(1.0f + glm::dot(before, after), 1e-3f); // canto muito agudo: limita o avanço
                const glm::vec3 offset = (before + after) * (amount / denominator);
                inner[i] = (uint32_t)Vertices.size();
                const glm::vec3 position = Vertices[outer[i]] + offset;
                Vertices.push_back(position);
            }

            const Face source = Faces[f];
            for (size_t i = 0; i < m; ++i) {
                const size_t j = (i + 1) % m;
                Face ring;
                ring.MaterialSlot = source.MaterialSlot;
                ring.UV = source.UV;
                ring.Indices = { outer[i], outer[j], inner[j], inner[i] };
                Faces.push_back(ring);
            }
            Faces[f].Indices = inner;
            done.push_back(f);
        }
        return done;
    }

    void EditableMesh::MoveVertices(const std::vector<uint32_t>& vertices, const glm::vec3& delta) {
        std::vector<uint32_t> unique = vertices;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        for (uint32_t index : unique)
            if (index < Vertices.size())
                Vertices[index] += delta;
    }

    void EditableMesh::TransformVertices(const std::vector<uint32_t>& vertices, const glm::mat4& m) {
        std::vector<uint32_t> unique = vertices;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        for (uint32_t index : unique)
            if (index < Vertices.size())
                Vertices[index] = glm::vec3(m * glm::vec4(Vertices[index], 1.0f));
    }

    void EditableMesh::FlipFaces(const std::vector<uint32_t>& faces) {
        for (uint32_t f : UniqueValidFaces(Vertices, Faces, faces))
            std::reverse(Faces[f].Indices.begin(), Faces[f].Indices.end());
    }

    void EditableMesh::DeleteFaces(const std::vector<uint32_t>& faces) {
        std::vector<char> remove(Faces.size(), 0);
        for (uint32_t f : faces)
            if (f < Faces.size())
                remove[f] = 1;
        std::vector<Face> kept;
        kept.reserve(Faces.size());
        for (size_t f = 0; f < Faces.size(); ++f)
            if (!remove[f])
                kept.push_back(std::move(Faces[f]));
        Faces = std::move(kept);
    }

    void EditableMesh::SetFaceMaterial(const std::vector<uint32_t>& faces, uint32_t slot) {
        for (uint32_t f : faces)
            if (f < Faces.size())
                Faces[f].MaterialSlot = slot;
    }

    void EditableMesh::SetSmoothingGroup(const std::vector<uint32_t>& faces, uint32_t group) {
        for (uint32_t f : faces)
            if (f < Faces.size())
                Faces[f].SmoothingGroup = group;
    }

    void EditableMesh::RemoveUnusedVertices() {
        std::vector<uint32_t> remap(Vertices.size(), UINT32_MAX);
        for (const Face& face : Faces)
            for (uint32_t index : face.Indices)
                if (index < Vertices.size())
                    remap[index] = 0;
        std::vector<glm::vec3> compact;
        for (size_t i = 0; i < Vertices.size(); ++i) {
            if (remap[i] == UINT32_MAX)
                continue;
            remap[i] = (uint32_t)compact.size();
            compact.push_back(Vertices[i]);
        }
        for (Face& face : Faces)
            for (uint32_t& index : face.Indices)
                index = index < remap.size() ? remap[index] : 0;
        Vertices = std::move(compact);
    }

    void EditableMesh::MergeVertices(float epsilon) {
        const size_t count = Vertices.size();
        std::vector<uint32_t> order(count);
        std::iota(order.begin(), order.end(), 0u);
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return Vertices[a].x < Vertices[b].x; });

        // Varredura por X: cada vertice se junta ao de menor indice dentro da distancia.
        std::vector<uint32_t> representative(count);
        std::iota(representative.begin(), representative.end(), 0u);
        const float epsilonSquared = epsilon * epsilon;
        for (size_t i = 0; i < count; ++i) {
            const uint32_t a = order[i];
            for (size_t j = i + 1; j < count; ++j) {
                const uint32_t b = order[j];
                if (Vertices[b].x - Vertices[a].x > epsilon)
                    break;
                const glm::vec3 d = Vertices[a] - Vertices[b];
                if (glm::dot(d, d) <= epsilonSquared) {
                    const uint32_t low = std::min(representative[a], representative[b]);
                    representative[a] = representative[b] = low;
                }
            }
        }
        // Resolve cadeias (a -> b -> c).
        for (size_t i = 0; i < count; ++i) {
            uint32_t root = representative[i];
            while (representative[root] != root)
                root = representative[root];
            representative[i] = root;
        }

        std::vector<Face> kept;
        for (Face& face : Faces) {
            std::vector<uint32_t> indices;
            for (uint32_t index : face.Indices) {
                const uint32_t mapped = index < count ? representative[index] : index;
                if (indices.empty() || indices.back() != mapped)
                    indices.push_back(mapped);
            }
            while (indices.size() > 1 && indices.front() == indices.back())
                indices.pop_back();
            if (indices.size() < 3)
                continue;
            face.Indices = std::move(indices);
            kept.push_back(std::move(face));
        }
        Faces = std::move(kept);
        RemoveUnusedVertices();
    }

    // ==========================================================================
    // Saida
    // ==========================================================================

    std::vector<BuiltSubmesh> EditableMesh::Build() const {
        // Para suavizacao: faces que usam cada vertice (so as com grupo != 0 precisam).
        std::vector<std::vector<uint32_t>> facesOfVertex(Vertices.size());
        std::vector<glm::vec3> weightedNormal(Faces.size(), glm::vec3(0.0f)); // Newell: comprimento = 2 x area
        for (uint32_t f = 0; f < Faces.size(); ++f) {
            if (!FaceIsUsable(Vertices, Faces[f]))
                continue;
            weightedNormal[f] = NewellVector(Vertices, Faces[f].Indices);
            if (Faces[f].SmoothingGroup != 0)
                for (uint32_t index : Faces[f].Indices)
                    facesOfVertex[index].push_back(f);
        }

        std::map<uint32_t, BuiltSubmesh> bySlot;
        std::vector<uint32_t> triangles;
        for (uint32_t f = 0; f < Faces.size(); ++f) {
            const Face& face = Faces[f];
            if (!FaceIsUsable(Vertices, face))
                continue;

            const glm::vec3 faceNormal = SafeNormalize(weightedNormal[f], glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 u, v;
            FaceBasis(faceNormal, u, v);

            const float angle = glm::radians(face.UV.RotationDeg);
            const float c = std::cos(angle), s = std::sin(angle);
            glm::vec3 tangentDirection = u * c - v * s; // direcao em que U cresce
            if (face.UV.FlipU) tangentDirection = -tangentDirection;

            BuiltSubmesh& submesh = bySlot[face.MaterialSlot];
            submesh.MaterialSlot = face.MaterialSlot;
            const uint32_t base = (uint32_t)submesh.Vertices.size();

            for (uint32_t index : face.Indices) {
                BuiltVertex out;
                out.Position = Vertices[index];

                glm::vec3 normal = faceNormal;
                if (face.SmoothingGroup != 0) {
                    glm::vec3 sum(0.0f);
                    for (uint32_t neighbor : facesOfVertex[index])
                        if (Faces[neighbor].SmoothingGroup == face.SmoothingGroup)
                            sum += weightedNormal[neighbor];
                    normal = SafeNormalize(sum, faceNormal);
                }
                out.Normal = normal;

                const float pu = glm::dot(out.Position, u), pv = glm::dot(out.Position, v);
                float ru = c * pu - s * pv;
                float rv = s * pu + c * pv;
                if (face.UV.FlipU) ru = -ru;
                if (face.UV.FlipV) rv = -rv;
                out.UV = glm::vec2(ru * face.UV.Tiling.x + face.UV.Offset.x, rv * face.UV.Tiling.y + face.UV.Offset.y);

                const glm::vec3 tangent = tangentDirection - normal * glm::dot(normal, tangentDirection);
                out.Tangent = SafeNormalize(tangent, u);
                submesh.Vertices.push_back(out);
            }

            triangles.clear();
            TriangulateFace(Vertices, face, faceNormal, triangles);
            for (uint32_t local : triangles)
                submesh.Indices.push_back(base + local);
        }

        std::vector<BuiltSubmesh> result;
        result.reserve(bySlot.size());
        for (auto& [slot, submesh] : bySlot)
            result.push_back(std::move(submesh));
        return result;
    }

    bool EditableMesh::Raycast(const glm::vec3& origin, const glm::vec3& direction, float& outDistance, int& outFace, bool cullBackfaces) const {
        bool hit = false;
        float best = std::numeric_limits<float>::max();
        std::vector<uint32_t> triangles;
        for (uint32_t f = 0; f < Faces.size(); ++f) {
            const Face& face = Faces[f];
            if (!FaceIsUsable(Vertices, face))
                continue;
            const glm::vec3 normal = FaceNormal(f);
            if (cullBackfaces && glm::dot(normal, direction) >= 0.0f)
                continue;

            triangles.clear();
            TriangulateFace(Vertices, face, normal, triangles);
            for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
                const glm::vec3& a = Vertices[face.Indices[triangles[t]]];
                const glm::vec3& b = Vertices[face.Indices[triangles[t + 1]]];
                const glm::vec3& cpos = Vertices[face.Indices[triangles[t + 2]]];
                // Moller-Trumbore (dupla face; o culling por face ja foi feito acima).
                const glm::vec3 e1 = b - a, e2 = cpos - a;
                const glm::vec3 p = glm::cross(direction, e2);
                const float det = glm::dot(e1, p);
                if (std::abs(det) < 1e-12f)
                    continue;
                const float inv = 1.0f / det;
                const glm::vec3 tvec = origin - a;
                const float bu = glm::dot(tvec, p) * inv;
                if (bu < 0.0f || bu > 1.0f)
                    continue;
                const glm::vec3 q = glm::cross(tvec, e1);
                const float bv = glm::dot(direction, q) * inv;
                if (bv < 0.0f || bu + bv > 1.0f)
                    continue;
                const float distance = glm::dot(e2, q) * inv;
                if (distance >= 0.0f && distance < best) {
                    best = distance;
                    outFace = (int)f;
                    hit = true;
                }
            }
        }
        if (hit)
            outDistance = best;
        return hit;
    }

    // ==========================================================================
    // Operacoes extras (4d)
    // ==========================================================================

    std::vector<std::pair<uint32_t, uint32_t>> EditableMesh::UniqueEdges() const {
        std::vector<std::pair<uint32_t, uint32_t>> edges;
        for (const Face& face : Faces) {
            const size_t n = face.Indices.size();
            for (size_t i = 0; i < n; ++i) {
                uint32_t a = face.Indices[i], b = face.Indices[(i + 1) % n];
                if (a == b) continue;
                if (a > b) std::swap(a, b);
                edges.emplace_back(a, b);
            }
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        return edges;
    }

    std::vector<uint32_t> EditableMesh::FacesUsingVertices(const std::vector<uint32_t>& vertices) const {
        std::vector<char> wanted(Vertices.size(), 0);
        for (uint32_t v : vertices)
            if (v < wanted.size()) wanted[v] = 1;
        std::vector<uint32_t> result;
        for (uint32_t f = 0; f < Faces.size(); ++f) {
            for (uint32_t index : Faces[f].Indices)
                if (index < wanted.size() && wanted[index]) { result.push_back(f); break; }
        }
        return result;
    }

    std::vector<uint32_t> EditableMesh::SubdivideFaces(const std::vector<uint32_t>& facesRequested) {
        const std::vector<uint32_t> faces = UniqueValidFaces(Vertices, Faces, facesRequested);
        if (faces.empty())
            return {};
        // Ponto medio de cada aresta e compartilhado entre faces vizinhas (mesma aresta, mesmo vertice),
        // para nao abrir buracos; so entram arestas das faces divididas.
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;
        auto midpointOf = [&](uint32_t a, uint32_t b) {
            const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            auto it = midpoints.find(key);
            if (it != midpoints.end()) return it->second;
            const uint32_t index = (uint32_t)Vertices.size();
            Vertices.push_back((Vertices[a] + Vertices[b]) * 0.5f);
            midpoints.emplace(key, index);
            return index;
        };

        std::vector<Face> created;
        std::vector<char> remove(Faces.size(), 0);
        for (uint32_t f : faces) {
            const Face source = Faces[f]; // copia: Faces nao e alterado durante o laco, mas Vertices cresce
            const size_t n = source.Indices.size();
            glm::vec3 centerSum(0.0f);
            for (uint32_t index : source.Indices) centerSum += Vertices[index];
            const uint32_t centerIndex = (uint32_t)Vertices.size();
            Vertices.push_back(centerSum / (float)n);
            for (size_t i = 0; i < n; ++i) {
                const uint32_t prev = source.Indices[(i + n - 1) % n];
                const uint32_t cur = source.Indices[i];
                const uint32_t next = source.Indices[(i + 1) % n];
                Face piece = source;
                piece.Indices = { midpointOf(prev, cur), cur, midpointOf(cur, next), centerIndex };
                created.push_back(std::move(piece));
            }
            remove[f] = 1;
        }
        std::vector<Face> kept;
        kept.reserve(Faces.size() + created.size());
        for (size_t f = 0; f < Faces.size(); ++f)
            if (!remove[f]) kept.push_back(std::move(Faces[f]));
        const uint32_t firstNew = (uint32_t)kept.size();
        for (Face& face : created) kept.push_back(std::move(face));
        Faces = std::move(kept);
        std::vector<uint32_t> result;
        for (uint32_t f = firstNew; f < Faces.size(); ++f) result.push_back(f);
        return result;
    }

    std::vector<uint32_t> EditableMesh::DetachFaces(const std::vector<uint32_t>& facesRequested) {
        const std::vector<uint32_t> faces = UniqueValidFaces(Vertices, Faces, facesRequested);
        std::vector<char> selected(Faces.size(), 0);
        for (uint32_t f : faces) selected[f] = 1;
        std::map<uint32_t, uint32_t> copies;
        for (uint32_t f : faces) {
            for (uint32_t& index : Faces[f].Indices) {
                auto it = copies.find(index);
                if (it == copies.end()) {
                    it = copies.emplace(index, (uint32_t)Vertices.size()).first;
                    Vertices.push_back(Vertices[index]);
                }
                index = it->second;
            }
        }
        return faces;
    }

    void EditableMesh::Mirror(int axis) {
        if (axis < 0 || axis > 2) return;
        for (glm::vec3& v : Vertices) v[axis] = -v[axis];
        for (Face& face : Faces) std::reverse(face.Indices.begin(), face.Indices.end());
    }

    uint32_t EditableMesh::Append(const EditableMesh& other, const glm::mat4& m) {
        const uint32_t firstFace = (uint32_t)Faces.size();
        const uint32_t base = (uint32_t)Vertices.size();
        const bool mirrored = glm::determinant(glm::mat3(m)) < 0.0f;
        for (const glm::vec3& v : other.Vertices)
            Vertices.push_back(glm::vec3(m * glm::vec4(v, 1.0f)));
        for (const Face& source : other.Faces) {
            Face face = source;
            for (uint32_t& index : face.Indices) index += base;
            if (mirrored) std::reverse(face.Indices.begin(), face.Indices.end());
            Faces.push_back(std::move(face));
        }
        return firstFace;
    }

    glm::vec3 EditableMesh::RecenterOnBounds() {
        if (Vertices.empty()) return glm::vec3(0.0f);
        glm::vec3 lo, hi;
        GetBounds(lo, hi);
        const glm::vec3 center = (lo + hi) * 0.5f;
        for (glm::vec3& v : Vertices) v -= center;
        return center;
    }

    // ==========================================================================
    // Arquivo
    // ==========================================================================

    void EditableMesh::Write(std::ostream& out) const {
        WriteRaw(out, kMagic);
        WriteRaw(out, kFormatVersion);
        WriteRaw(out, (uint32_t)Vertices.size());
        for (const glm::vec3& v : Vertices) { WriteRaw(out, v.x); WriteRaw(out, v.y); WriteRaw(out, v.z); }
        WriteRaw(out, (uint32_t)Faces.size());
        for (const Face& face : Faces) {
            WriteRaw(out, (uint32_t)face.Indices.size());
            for (uint32_t index : face.Indices) WriteRaw(out, index);
            WriteRaw(out, face.MaterialSlot);
            WriteRaw(out, face.SmoothingGroup);
            WriteRaw(out, face.UV.Offset.x); WriteRaw(out, face.UV.Offset.y);
            WriteRaw(out, face.UV.Tiling.x); WriteRaw(out, face.UV.Tiling.y);
            WriteRaw(out, face.UV.RotationDeg);
            WriteRaw(out, (uint8_t)(face.UV.FlipU ? 1 : 0));
            WriteRaw(out, (uint8_t)(face.UV.FlipV ? 1 : 0));
        }
    }

    bool EditableMesh::Read(std::istream& in, EditableMesh& outMesh) {
        uint32_t magic = 0, version = 0, vertexCount = 0;
        if (!ReadRaw(in, magic) || magic != kMagic) return false;
        if (!ReadRaw(in, version) || version == 0 || version > kFormatVersion) return false;
        if (!ReadRaw(in, vertexCount) || vertexCount > kMaxVertices) return false;

        EditableMesh mesh;
        // push_back em vez de reservar 'vertexCount': uma contagem corrompida nao aloca memoria absurda
        // (a leitura falha no fim do arquivo antes disso).
        for (uint32_t i = 0; i < vertexCount; ++i) {
            glm::vec3 v;
            if (!ReadRaw(in, v.x) || !ReadRaw(in, v.y) || !ReadRaw(in, v.z)) return false;
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
            mesh.Vertices.push_back(v);
        }
        uint32_t faceCount = 0;
        if (!ReadRaw(in, faceCount) || faceCount > kMaxFaces) return false;
        for (uint32_t f = 0; f < faceCount; ++f) {
            uint32_t corners = 0;
            if (!ReadRaw(in, corners) || corners < 3 || corners > kMaxFaceCorners) return false;
            Face face;
            for (uint32_t i = 0; i < corners; ++i) {
                uint32_t index = 0;
                if (!ReadRaw(in, index) || index >= vertexCount) return false;
                face.Indices.push_back(index);
            }
            uint8_t flipU = 0, flipV = 0;
            if (!ReadRaw(in, face.MaterialSlot) || !ReadRaw(in, face.SmoothingGroup)
                || !ReadRaw(in, face.UV.Offset.x) || !ReadRaw(in, face.UV.Offset.y)
                || !ReadRaw(in, face.UV.Tiling.x) || !ReadRaw(in, face.UV.Tiling.y)
                || !ReadRaw(in, face.UV.RotationDeg) || !ReadRaw(in, flipU) || !ReadRaw(in, flipV))
                return false;
            if (!std::isfinite(face.UV.Offset.x) || !std::isfinite(face.UV.Offset.y) || !std::isfinite(face.UV.Tiling.x)
                || !std::isfinite(face.UV.Tiling.y) || !std::isfinite(face.UV.RotationDeg))
                return false;
            face.UV.FlipU = flipU != 0;
            face.UV.FlipV = flipV != 0;
            mesh.Faces.push_back(std::move(face));
        }
        outMesh = std::move(mesh);
        return true;
    }

}
