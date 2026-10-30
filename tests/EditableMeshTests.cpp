// EditableMeshTests.cpp - malha poligonal do Level Builder (Level/EditableMesh.h, so std + glm).
#include <Prism/Level/EditableMesh.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <tuple>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>
#include <string>

using namespace Prism::Level;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps * (1.0f + std::fabs(b)); }
static bool NearV(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) { return glm::length(a - b) <= eps; }

// Volume assinado pelos triangulos de Build() (teorema da divergencia): > 0 = normais para fora.
static float SignedVolume(const EditableMesh& mesh) {
    float volume = 0.0f;
    for (const BuiltSubmesh& sub : mesh.Build())
        for (size_t i = 0; i + 2 < sub.Indices.size(); i += 3) {
            const glm::vec3& a = sub.Vertices[sub.Indices[i]].Position;
            const glm::vec3& b = sub.Vertices[sub.Indices[i + 1]].Position;
            const glm::vec3& c = sub.Vertices[sub.Indices[i + 2]].Position;
            volume += glm::dot(a, glm::cross(b, c)) / 6.0f;
        }
    return volume;
}
static float SurfaceArea(const EditableMesh& mesh) {
    float area = 0.0f;
    for (uint32_t f = 0; f < mesh.Faces.size(); ++f) area += mesh.FaceArea(f);
    return area;
}
static uint32_t FindFace(const EditableMesh& mesh, const glm::vec3& normal) {
    for (uint32_t f = 0; f < mesh.Faces.size(); ++f)
        if (glm::dot(mesh.FaceNormal(f), normal) > 0.99f) return f;
    return UINT32_MAX;
}

static void TestShapes() {
    // Caixa
    {
        EditableMesh box = EditableMesh::CreateBox({ 2.0f, 3.0f, 4.0f });
        CHECK(box.Vertices.size() == 8 && box.Faces.size() == 6);
        CHECK(box.IsValid() && box.IsClosed());
        CHECK(Near(SignedVolume(box), 24.0f));
        CHECK(Near(SurfaceArea(box), 2 * (2 * 3 + 2 * 4 + 3 * 4)));
        glm::vec3 mn, mx; box.GetBounds(mn, mx);
        CHECK(NearV(mn, { -1.0f, -1.5f, -2.0f }) && NearV(mx, { 1.0f, 1.5f, 2.0f }));
        // as 6 normais sao os 6 eixos, todas para fora (apontam no mesmo sentido do centro da face)
        for (uint32_t f = 0; f < 6; ++f) CHECK(glm::dot(box.FaceNormal(f), box.FaceCenter(f)) > 0.0f);
        CHECK(FindFace(box, { 1, 0, 0 }) != UINT32_MAX && FindFace(box, { 0, -1, 0 }) != UINT32_MAX && FindFace(box, { 0, 0, 1 }) != UINT32_MAX);

        auto built = box.Build();
        CHECK(built.size() == 1 && built[0].Vertices.size() == 24 && built[0].Indices.size() == 36);
        // cada triangulo tem a mesma normal do vertice (sombreamento chapado) e fica dentro da caixa
        bool normalsAgree = true, inside = true;
        for (const BuiltSubmesh& sub : built)
            for (size_t i = 0; i + 2 < sub.Indices.size(); i += 3) {
                const BuiltVertex& a = sub.Vertices[sub.Indices[i]];
                const BuiltVertex& b = sub.Vertices[sub.Indices[i + 1]];
                const BuiltVertex& c = sub.Vertices[sub.Indices[i + 2]];
                if (glm::dot(glm::cross(b.Position - a.Position, c.Position - a.Position), a.Normal) <= 0.0f) normalsAgree = false;
                if (std::fabs(a.Position.x) > 1.001f || std::fabs(a.Position.y) > 1.501f || std::fabs(a.Position.z) > 2.001f) inside = false;
            }
        CHECK(normalsAgree && inside);
    }
    // Rampa e escada (a escada tem faces laterais CONCAVAS: o volume so bate se a triangulacao estiver certa)
    {
        EditableMesh ramp = EditableMesh::CreateRamp({ 2.0f, 3.0f, 4.0f });
        CHECK(ramp.IsValid() && ramp.IsClosed() && Near(SignedVolume(ramp), 0.5f * 3 * 4 * 2));

        for (uint32_t steps : { 1u, 2u, 5u, 12u }) {
            EditableMesh stairs = EditableMesh::CreateStairs({ 2.0f, 3.0f, 4.0f }, steps);
            const float profileArea = 4.0f * 3.0f * (float)(steps + 1) / (2.0f * (float)steps);
            CHECK(stairs.IsValid() && stairs.IsClosed());
            CHECK(Near(SignedVolume(stairs), profileArea * 2.0f));
        }
        EditableMesh stairs = EditableMesh::CreateStairs({ 2.0f, 3.0f, 4.0f }, 6);
        glm::vec3 mn, mx; stairs.GetBounds(mn, mx);
        CHECK(NearV(mn, { -1.0f, -1.5f, -2.0f }) && NearV(mx, { 1.0f, 1.5f, 2.0f }));
    }
    // Cilindro (lados suavizados, tampas chapadas)
    {
        const uint32_t n = 12;
        EditableMesh cyl = EditableMesh::CreateCylinder(1.5f, 2.0f, n);
        CHECK(cyl.Vertices.size() == 2 * n && cyl.Faces.size() == n + 2);
        CHECK(cyl.IsValid() && cyl.IsClosed());
        const float expected = 0.5f * (float)n * 1.5f * 1.5f * std::sin(2.0f * 3.14159265f / (float)n) * 2.0f;
        CHECK(Near(SignedVolume(cyl), expected));
        bool radialOk = true, capsOk = true;
        for (const BuiltSubmesh& sub : cyl.Build())
            for (const BuiltVertex& v : sub.Vertices) {
                if (std::fabs(v.Normal.y) < 0.01f) { // lado: normal radial no plano XZ
                    glm::vec3 radial = glm::normalize(glm::vec3(v.Position.x, 0.0f, v.Position.z));
                    if (glm::dot(radial, v.Normal) < 0.999f) radialOk = false;
                } else if (!(std::fabs(v.Normal.y) > 0.999f)) capsOk = false;
            }
        CHECK(radialOk && capsOk);
    }
    // Plano subdividido
    {
        EditableMesh plane = EditableMesh::CreatePlane({ 4.0f, 2.0f }, 2, 1);
        CHECK(plane.Faces.size() == 2 && plane.Vertices.size() == 6 && plane.IsValid() && !plane.IsClosed());
        CHECK(Near(SurfaceArea(plane), 8.0f));
        for (uint32_t f = 0; f < 2; ++f) CHECK(NearV(plane.FaceNormal(f), { 0, 1, 0 }));
    }
}

static void TestTriangulationConcave() {
    // "L": quadrado 2x2 sem o canto 1x1 => area 3
    EditableMesh mesh;
    mesh.Vertices = { { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, -1 }, { 1, 0, -1 }, { 1, 0, -2 }, { 0, 0, -2 } };
    Face face; face.Indices = { 0, 1, 2, 3, 4, 5 };
    mesh.Faces.push_back(face);
    CHECK(NearV(mesh.FaceNormal(0), { 0, 1, 0 }));
    CHECK(Near(mesh.FaceArea(0), 3.0f));
    auto built = mesh.Build();
    CHECK(built.size() == 1 && built[0].Indices.size() == 12); // 6 vertices => 4 triangulos
    float area = 0.0f; bool facingUp = true;
    for (size_t i = 0; i + 2 < built[0].Indices.size(); i += 3) {
        const glm::vec3 a = built[0].Vertices[built[0].Indices[i]].Position;
        const glm::vec3 b = built[0].Vertices[built[0].Indices[i + 1]].Position;
        const glm::vec3 c = built[0].Vertices[built[0].Indices[i + 2]].Position;
        const glm::vec3 n = glm::cross(b - a, c - a);
        area += 0.5f * glm::length(n);
        if (n.y <= 0.0f) facingUp = false;
    }
    CHECK(Near(area, 3.0f) && facingUp); // soma bate (sem sobreposicao nem buraco) e todos viram para +Y
}

static void TestExtrude() {
    // Topo da caixa +1: volume cresce w*d*1; segue fechada e valida
    {
        EditableMesh box = EditableMesh::CreateBox({ 2.0f, 2.0f, 2.0f });
        const uint32_t top = FindFace(box, { 0, 1, 0 });
        auto result = box.ExtrudeFaces({ top }, 1.0f);
        CHECK(result.size() == 1 && result[0] == top);
        CHECK(box.Faces.size() == 6 + 4 && box.IsValid() && box.IsClosed());
        CHECK(Near(SignedVolume(box), 8.0f + 4.0f));
        glm::vec3 mn, mx; box.GetBounds(mn, mx);
        CHECK(Near(mx.y, 2.0f) && Near(mn.y, -1.0f));
        CHECK(NearV(box.FaceNormal(top), { 0, 1, 0 }));
    }
    // Para dentro (buraco): volume diminui, paredes viradas para o vao
    {
        EditableMesh box = EditableMesh::CreateBox({ 2.0f, 2.0f, 2.0f });
        box.ExtrudeFaces({ FindFace(box, { 0, 1, 0 }) }, -0.5f);
        CHECK(box.IsClosed() && Near(SignedVolume(box), 8.0f - 4.0f * 0.5f));
    }
    // Duas faces vizinhas: so a borda externa ganha parede (6 paredes, nao 8)
    {
        EditableMesh plane = EditableMesh::CreatePlane({ 2.0f, 1.0f }, 2, 1);
        plane.ExtrudeFaces({ 0, 1 }, 1.0f);
        CHECK(plane.Faces.size() == 2 + 6);
        CHECK(Near(SurfaceArea(plane), 2.0f + 1.0f * (2 * 2 + 2 * 1)));
        CHECK(plane.IsValid());
    }
    // Indices repetidos / invalidos nao quebram
    {
        EditableMesh box = EditableMesh::CreateBox({ 1, 1, 1 });
        const uint32_t top = FindFace(box, { 0, 1, 0 });
        box.ExtrudeFaces({ top, top, 999 }, 1.0f);
        CHECK(box.IsClosed() && Near(SignedVolume(box), 2.0f));
        CHECK(box.ExtrudeFaces({}, 1.0f).empty() && box.ExtrudeFaces({ 999 }, 1.0f).empty());
        CHECK(box.ExtrudeFaces({ 0 }, std::numeric_limits<float>::quiet_NaN()).empty());
    }
}

static void TestInset() {
    EditableMesh box = EditableMesh::CreateBox({ 4.0f, 2.0f, 4.0f });
    const uint32_t top = FindFace(box, { 0, 1, 0 });
    auto result = box.InsetFaces({ top }, 0.5f);
    CHECK(result.size() == 1 && box.Faces.size() == 6 + 4);
    CHECK(box.IsValid() && box.IsClosed());
    CHECK(Near(box.FaceArea(top), 3.0f * 3.0f));                 // 4x4 menos 0.5 de cada lado
    CHECK(Near(SignedVolume(box), 32.0f));                       // volume nao muda
    // janela: inset + extrude para dentro
    box.ExtrudeFaces({ top }, -0.5f);
    CHECK(box.IsClosed() && Near(SignedVolume(box), 32.0f - 9.0f * 0.5f));
    // inset invalido
    EditableMesh other = EditableMesh::CreateBox({ 1, 1, 1 });
    CHECK(other.InsetFaces({ 0 }, 0.0f).empty() && other.InsetFaces({ 0 }, -1.0f).empty() && other.Faces.size() == 6);
    // inset em face concava (L) mantem a moldura com largura constante: area interna menor, valida
    EditableMesh mesh;
    mesh.Vertices = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 0, -2 }, { 2, 0, -2 }, { 2, 0, -4 }, { 0, 0, -4 } };
    Face face; face.Indices = { 0, 1, 2, 3, 4, 5 };
    mesh.Faces.push_back(face);
    const float before = mesh.FaceArea(0);
    mesh.InsetFaces({ 0 }, 0.25f);
    CHECK(mesh.IsValid() && mesh.FaceArea(0) < before && mesh.Faces.size() == 1 + 6);
}

static void TestEditing() {
    // Mover vertices de uma face: a face segue
    EditableMesh box = EditableMesh::CreateBox({ 2, 2, 2 });
    const uint32_t top = FindFace(box, { 0, 1, 0 });
    box.MoveVertices(box.VerticesOfFaces({ top }), { 0, 1, 0 });
    CHECK(Near(box.FaceCenter(top).y, 2.0f) && box.IsClosed());
    CHECK(Near(SignedVolume(box), 12.0f)); // 2x2x3

    // Girar: transformar vertices mantem a forma
    EditableMesh rot = EditableMesh::CreateBox({ 2, 2, 2 });
    std::vector<uint32_t> all(rot.Vertices.size());
    for (uint32_t i = 0; i < all.size(); ++i) all[i] = i;
    rot.TransformVertices(all, glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0, 1, 0)));
    CHECK(Near(SignedVolume(rot), 8.0f) && rot.IsClosed());

    // Inverter: volume negativo; inverter de novo volta
    EditableMesh flipped = EditableMesh::CreateBox({ 2, 2, 2 });
    flipped.FlipFaces({ 0, 1, 2, 3, 4, 5 });
    CHECK(SignedVolume(flipped) < 0.0f && flipped.IsClosed());
    flipped.FlipFaces({ 0, 1, 2, 3, 4, 5 });
    CHECK(Near(SignedVolume(flipped), 8.0f));

    // Apagar face: aberta, e os indices restantes continuam validos
    EditableMesh open = EditableMesh::CreateBox({ 2, 2, 2 });
    open.DeleteFaces({ FindFace(open, { 0, 1, 0 }), 999 });
    CHECK(open.Faces.size() == 5 && open.IsValid() && !open.IsClosed());
    open.RemoveUnusedVertices();
    CHECK(open.Vertices.size() == 8 && open.IsValid()); // as 4 do topo ainda sao usadas pelas laterais

    // Material e suavizacao
    EditableMesh mat = EditableMesh::CreateBox({ 1, 1, 1 });
    mat.SetFaceMaterial({ 0, 1 }, 3);
    auto built = mat.Build();
    CHECK(built.size() == 2 && built[0].MaterialSlot == 0 && built[1].MaterialSlot == 3);
    CHECK(built[0].Vertices.size() == 16 && built[1].Vertices.size() == 8);
    mat.SetSmoothingGroup({ 0, 1, 2, 3, 4, 5 }, 1); // caixa toda suave: normais diagonais nos cantos
    bool diagonal = true;
    for (const BuiltSubmesh& sub : mat.Build())
        for (const BuiltVertex& v : sub.Vertices)
            if (std::fabs(std::fabs(v.Normal.x) - 0.57735f) > 0.01f || std::fabs(std::fabs(v.Normal.y) - 0.57735f) > 0.01f) diagonal = false;
    CHECK(diagonal);
}

static void TestMerge() {
    // Cada face com os proprios 4 vertices (24) -> soldar -> 8, fechada
    EditableMesh box = EditableMesh::CreateBox({ 2, 2, 2 });
    EditableMesh split;
    for (const Face& face : box.Faces) {
        Face copy = face;
        for (uint32_t& index : copy.Indices) {
            split.Vertices.push_back(box.Vertices[index]);
            index = (uint32_t)split.Vertices.size() - 1;
        }
        split.Faces.push_back(copy);
    }
    CHECK(split.Vertices.size() == 24 && !split.IsClosed());
    split.MergeVertices();
    CHECK(split.Vertices.size() == 8 && split.Faces.size() == 6 && split.IsClosed() && Near(SignedVolume(split), 8.0f));

    // Vertices colapsados: o quad vira triangulo; o que degenera some
    EditableMesh tri;
    tri.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 } };
    Face quad; quad.Indices = { 0, 1, 2, 3 };
    Face degenerate; degenerate.Indices = { 1, 2, 1 };
    tri.Faces = { quad, degenerate };
    tri.MergeVertices();
    CHECK(tri.Faces.size() == 1 && tri.Faces[0].Indices.size() == 3 && tri.Vertices.size() == 3 && tri.IsValid());

    // Cadeia de vertices proximos (a-b-c) vira um so
    EditableMesh chain;
    chain.Vertices = { { 0, 0, 0 }, { 0.0006f, 0, 0 }, { 0.0012f, 0, 0 }, { 5, 0, 0 }, { 5, 0, -1 } };
    Face f; f.Indices = { 0, 3, 4 }; Face g; g.Indices = { 2, 3, 4 };
    chain.Faces = { f, g };
    chain.MergeVertices(0.001f);
    CHECK(chain.Vertices.size() == 3);
}

static void TestUV() {
    // Faces coplanares vizinhas: o mesmo ponto 3D tem o mesmo UV nas duas (textura emenda)
    EditableMesh plane = EditableMesh::CreatePlane({ 2.0f, 1.0f }, 2, 1);
    auto built = plane.Build();
    CHECK(built.size() == 1);
    std::map<std::tuple<int, int, int>, glm::vec2> seen;
    bool consistent = true;
    for (const BuiltVertex& v : built[0].Vertices) {
        auto key = std::make_tuple((int)std::lround(v.Position.x * 1000), (int)std::lround(v.Position.y * 1000), (int)std::lround(v.Position.z * 1000));
        auto it = seen.find(key);
        if (it == seen.end()) seen[key] = v.UV;
        else if (glm::length(it->second - v.UV) > 1e-4f) consistent = false;
    }
    CHECK(consistent);

    // 1 unidade = 1 tile; Tiling 2 dobra; offset soma
    glm::vec2 a = built[0].Vertices[0].UV, b;
    for (const BuiltVertex& v : built[0].Vertices) if (glm::length(v.Position - built[0].Vertices[0].Position - glm::vec3(1, 0, 0)) < 1e-4f) { b = v.UV; break; }
    CHECK(Near(std::fabs(b.x - a.x), 1.0f) && Near(b.y, a.y));
    plane.Faces[0].UV.Tiling = { 2.0f, 2.0f };
    plane.Faces[0].UV.Offset = { 0.25f, 0.0f };
    auto changed = plane.Build();
    glm::vec2 a2 = changed[0].Vertices[0].UV;
    CHECK(glm::length((a2 - glm::vec2(0.25f, 0.0f)) - a * 2.0f) < 1e-3f);

    // Tangente: unitaria, perpendicular a normal; UV-aware (gira com a rotacao, inverte com FlipU)
    EditableMesh box = EditableMesh::CreateBox({ 2, 2, 2 });
    bool tangentsOk = true;
    for (const BuiltSubmesh& sub : box.Build())
        for (const BuiltVertex& v : sub.Vertices)
            if (!Near(glm::length(v.Tangent), 1.0f) || std::fabs(glm::dot(v.Tangent, v.Normal)) > 1e-4f) tangentsOk = false;
    CHECK(tangentsOk);
    EditableMesh floor = EditableMesh::CreatePlane({ 2, 2 });
    const glm::vec3 t0 = floor.Build()[0].Vertices[0].Tangent;
    floor.Faces[0].UV.FlipU = true;
    CHECK(NearV(floor.Build()[0].Vertices[0].Tangent, -t0));
    floor.Faces[0].UV.FlipU = false;
    floor.Faces[0].UV.RotationDeg = 90.0f;
    const glm::vec3 t90 = floor.Build()[0].Vertices[0].Tangent;
    CHECK(std::fabs(glm::dot(t0, t90)) < 1e-3f);

    // Paredes: V sobe com Y
    EditableMesh wall = EditableMesh::CreateBox({ 2, 2, 2 });
    const uint32_t front = FindFace(wall, { 0, 0, 1 });
    float vLow = 1e9f, vHigh = -1e9f, yLow = 0, yHigh = 0;
    for (const BuiltSubmesh& sub : wall.Build())
        for (const BuiltVertex& v : sub.Vertices)
            if (glm::dot(v.Normal, glm::vec3(0, 0, 1)) > 0.99f) {
                if (v.UV.y < vLow) { vLow = v.UV.y; yLow = v.Position.y; }
                if (v.UV.y > vHigh) { vHigh = v.UV.y; yHigh = v.Position.y; }
            }
    (void)front;
    CHECK(yHigh > yLow);
}

static void TestRaycast() {
    EditableMesh box = EditableMesh::CreateBox({ 2, 2, 2 });
    float t = 0; int face = -1;
    CHECK(box.Raycast({ 0, 5, 0 }, { 0, -1, 0 }, t, face));
    CHECK(Near(t, 4.0f) && face == (int)FindFace(box, { 0, 1, 0 }));
    CHECK(box.Raycast({ 5, 0.2f, 0.3f }, { -1, 0, 0 }, t, face) && face == (int)FindFace(box, { 1, 0, 0 }) && Near(t, 4.0f));
    CHECK(!box.Raycast({ 5, 5, 0 }, { 0, -1, 0 }, t, face));                // erra
    CHECK(!box.Raycast({ 0, 0, 0 }, { 0, 1, 0 }, t, face, true));           // de dentro, face de costas: ignorada
    CHECK(box.Raycast({ 0, 0, 0 }, { 0, 1, 0 }, t, face, false) && Near(t, 1.0f)); // sem culling: acerta por dentro
    // a mais proxima vence: duas caixas em fila
    EditableMesh nearBox = EditableMesh::CreateBox({ 2, 2, 2 });
    CHECK(nearBox.Raycast({ 0, 10, 0 }, { 0, -1, 0 }, t, face) && Near(t, 9.0f));
    // face concava: acerta o braco do L, nao o vazio
    EditableMesh mesh;
    mesh.Vertices = { { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, -1 }, { 1, 0, -1 }, { 1, 0, -2 }, { 0, 0, -2 } };
    Face l; l.Indices = { 0, 1, 2, 3, 4, 5 }; mesh.Faces.push_back(l);
    CHECK(mesh.Raycast({ 0.5f, 3, -1.5f }, { 0, -1, 0 }, t, face));          // dentro do L
    CHECK(!mesh.Raycast({ 1.5f, 3, -1.5f }, { 0, -1, 0 }, t, face));         // no "canto" vazio
}

static void TestSerialization() {
    EditableMesh mesh = EditableMesh::CreateStairs({ 2, 3, 4 }, 4);
    mesh.Faces[2].MaterialSlot = 5;
    mesh.Faces[3].SmoothingGroup = 2;
    mesh.Faces[4].UV.Offset = { 0.5f, 0.25f };
    mesh.Faces[4].UV.Tiling = { 2.0f, 0.5f };
    mesh.Faces[4].UV.RotationDeg = 45.0f;
    mesh.Faces[4].UV.FlipU = true;

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    mesh.Write(stream);
    const std::string bytes = stream.str();

    EditableMesh loaded;
    {
        std::stringstream in(bytes, std::ios::in | std::ios::binary);
        CHECK(EditableMesh::Read(in, loaded));
    }
    CHECK(loaded.Vertices.size() == mesh.Vertices.size() && loaded.Faces.size() == mesh.Faces.size());
    bool same = true;
    for (size_t i = 0; i < mesh.Vertices.size(); ++i) if (!NearV(loaded.Vertices[i], mesh.Vertices[i], 0.0f)) same = false;
    for (size_t f = 0; f < mesh.Faces.size(); ++f) {
        const Face& a = mesh.Faces[f]; const Face& b = loaded.Faces[f];
        if (a.Indices != b.Indices || a.MaterialSlot != b.MaterialSlot || a.SmoothingGroup != b.SmoothingGroup
            || a.UV.Offset != b.UV.Offset || a.UV.Tiling != b.UV.Tiling || a.UV.RotationDeg != b.UV.RotationDeg
            || a.UV.FlipU != b.UV.FlipU || a.UV.FlipV != b.UV.FlipV) same = false;
    }
    CHECK(same);
    CHECK(loaded.IsClosed() && Near(SignedVolume(loaded), SignedVolume(mesh)));

    // truncado em QUALQUER ponto: Read falha e nunca trava; o destino fica intacto
    bool allRejected = true;
    for (size_t length = 0; length < bytes.size(); ++length) {
        std::stringstream in(bytes.substr(0, length), std::ios::in | std::ios::binary);
        EditableMesh target = EditableMesh::CreateBox({ 1, 1, 1 });
        if (EditableMesh::Read(in, target) || target.Faces.size() != 6) allRejected = false;
    }
    CHECK(allRejected);

    // cabecalho errado, versao futura, contagem absurda, indice fora do intervalo
    {
        std::string bad = bytes; bad[0] = 'X';
        std::stringstream in(bad, std::ios::in | std::ios::binary); EditableMesh t;
        CHECK(!EditableMesh::Read(in, t));
    }
    {
        std::string future = bytes; future[4] = 99;
        std::stringstream in(future, std::ios::in | std::ios::binary); EditableMesh t;
        CHECK(!EditableMesh::Read(in, t));
    }
    {
        std::string huge = bytes; huge[8] = (char)0xFF; huge[9] = (char)0xFF; huge[10] = (char)0xFF; huge[11] = (char)0x7F; // ~2 bilhoes de vertices
        std::stringstream in(huge, std::ios::in | std::ios::binary); EditableMesh t;
        CHECK(!EditableMesh::Read(in, t));
    }
    {
        // 1 triangulo cujo indice aponta para fora
        std::stringstream out(std::ios::in | std::ios::out | std::ios::binary);
        EditableMesh tri; tri.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 } };
        Face f; f.Indices = { 0, 1, 2 }; tri.Faces.push_back(f);
        tri.Write(out);
        std::string data = out.str();
        // o primeiro indice da face fica logo apos: magic(4) version(4) vcount(4) 9 floats(36) fcount(4) corners(4)
        data[4 + 4 + 4 + 36 + 4 + 4] = 9;
        std::stringstream in(data, std::ios::in | std::ios::binary); EditableMesh t;
        CHECK(!EditableMesh::Read(in, t));
    }
}

static void TestValidation() {
    EditableMesh mesh = EditableMesh::CreateBox({ 1, 1, 1 });
    std::string why;
    CHECK(mesh.IsValid(&why));
    mesh.Faces[0].Indices[0] = 99;
    CHECK(!mesh.IsValid(&why) && !why.empty());
    // Build/Raycast/Extrude toleram malha invalida sem travar
    CHECK(!mesh.Build().empty());
    float t; int f;
    mesh.Raycast({ 0, 5, 0 }, { 0, -1, 0 }, t, f);
    mesh.ExtrudeFaces({ 0, 1 }, 1.0f);
    EditableMesh bad = EditableMesh::CreateBox({ 1, 1, 1 });
    bad.Vertices[0].x = std::numeric_limits<float>::infinity();
    CHECK(!bad.IsValid(&why));
    EditableMesh empty;
    CHECK(empty.IsValid() && !empty.IsClosed() && empty.Build().empty());
    glm::vec3 mn, mx; empty.GetBounds(mn, mx);
    CHECK(NearV(mn, { 0, 0, 0 }) && NearV(mx, { 0, 0, 0 }));
}

static void TestExtraOps() {
    // Arestas unicas e faces por vertice
    {
        EditableMesh box = EditableMesh::CreateBox({ 1, 1, 1 });
        CHECK(box.UniqueEdges().size() == 12);
        CHECK(box.FacesUsingVertices({ 0 }).size() == 3);
        CHECK(box.FacesUsingVertices({ 9999 }).empty());
    }
    // Subdividir uma face de uma caixa: area e volume preservados, so uma face vira 4 quads
    {
        EditableMesh box = EditableMesh::CreateBox({ 2, 2, 2 });
        const float area = SurfaceArea(box);
        const uint32_t top = FindFace(box, { 0, 1, 0 });
        auto created = box.SubdivideFaces({ top });
        CHECK(created.size() == 4);
        CHECK(box.Faces.size() == 6 - 1 + 4);
        CHECK(box.IsValid());
        CHECK(Near(SurfaceArea(box), area));
        CHECK(Near(SignedVolume(box), 8.0f));
        for (uint32_t f : created) { CHECK(Near(box.FaceArea(f), 1.0f)); CHECK(NearV(box.FaceNormal(f), { 0, 1, 0 })); }
        // Subdividir tudo: fica fechada (arestas compartilhadas usam o mesmo ponto medio)
        EditableMesh all = EditableMesh::CreateBox({ 2, 2, 2 });
        std::vector<uint32_t> every;
        for (uint32_t f = 0; f < all.Faces.size(); ++f) every.push_back(f);
        CHECK(all.SubdivideFaces(every).size() == 24);
        CHECK(all.IsValid() && all.IsClosed() && Near(SignedVolume(all), 8.0f));
        // Entrada invalida e ignorada
        CHECK(all.SubdivideFaces({ 9999 }).empty());
        // Material/UV/grupo preservados
        EditableMesh one = EditableMesh::CreateBox({ 1, 1, 1 });
        one.SetFaceMaterial({ 0 }, 3);
        one.SetSmoothingGroup({ 0 }, 7);
        one.Faces[0].UV.Tiling = { 2, 2 };
        for (uint32_t f : one.SubdivideFaces({ 0 })) {
            CHECK(one.Faces[f].MaterialSlot == 3 && one.Faces[f].SmoothingGroup == 7 && one.Faces[f].UV.Tiling.x == 2.0f);
        }
    }
    // Destacar: mover so as faces destacadas nao arrasta as vizinhas
    {
        EditableMesh box = EditableMesh::CreateBox({ 2, 2, 2 });
        const uint32_t top = FindFace(box, { 0, 1, 0 });
        const size_t before = box.Vertices.size();
        box.DetachFaces({ top });
        CHECK(box.Vertices.size() == before + 4);
        CHECK(box.IsValid() && !box.IsClosed());
        box.MoveVertices(box.VerticesOfFaces({ top }), { 0, 1, 0 });
        float lo = 1e9f;
        for (uint32_t f = 0; f < box.Faces.size(); ++f)
            if (f != top && glm::dot(box.FaceNormal(f), glm::vec3(1, 0, 0)) > 0.99f) lo = std::min(lo, box.FaceCenter(f).y);
        CHECK(Near(lo, 0.0f));
    }
    // Espelhar: volume continua positivo e a caixa envolvente inverte
    {
        EditableMesh ramp = EditableMesh::CreateRamp({ 2, 2, 2 });
        glm::vec3 lo, hi;
        ramp.GetBounds(lo, hi);
        for (int axis = 0; axis < 3; ++axis) {
            EditableMesh m = ramp;
            m.TransformVertices(m.VerticesOfFaces({ 0, 1, 2, 3, 4 }), glm::translate(glm::mat4(1.0f), { 5, 6, 7 }));
            glm::vec3 a, b;
            m.GetBounds(a, b);
            m.Mirror(axis);
            glm::vec3 c, d;
            m.GetBounds(c, d);
            CHECK(Near(c[axis], -b[axis]) && Near(d[axis], -a[axis]));
            CHECK(m.IsValid() && m.IsClosed() && SignedVolume(m) > 0.0f);
        }
        EditableMesh bad = ramp; bad.Mirror(5);
        CHECK(bad.Vertices.size() == ramp.Vertices.size() && NearV(bad.Vertices[0], ramp.Vertices[0]));
    }
    // Combinar: indices e slots; espelho corrige o sentido
    {
        EditableMesh a = EditableMesh::CreateBox({ 1, 1, 1 });
        EditableMesh b = EditableMesh::CreateBox({ 1, 1, 1 });
        b.SetFaceMaterial({ 0 }, 2);
        const uint32_t first = a.Append(b, glm::translate(glm::mat4(1.0f), { 3, 0, 0 }));
        CHECK(first == 6 && a.Faces.size() == 12 && a.Vertices.size() == 16);
        CHECK(a.IsValid() && a.IsClosed() && Near(SignedVolume(a), 2.0f));
        CHECK(a.Faces[6].MaterialSlot == 2);
        EditableMesh c = EditableMesh::CreateBox({ 1, 1, 1 });
        c.Append(EditableMesh::CreateRamp({ 1, 1, 1 }), glm::scale(glm::mat4(1.0f), { -1, 1, 1 }));
        CHECK(c.IsValid() && c.IsClosed() && SignedVolume(c) > 1.0f);
        EditableMesh empty;
        CHECK(empty.Append(EditableMesh{}) == 0 && empty.Faces.empty());
    }
    // Recentralizar
    {
        EditableMesh box = EditableMesh::CreateBox({ 2, 2, 2 });
        box.TransformVertices(box.VerticesOfFaces({ 0, 1, 2, 3, 4, 5 }), glm::translate(glm::mat4(1.0f), { 4, 5, 6 }));
        const glm::vec3 offset = box.RecenterOnBounds();
        CHECK(NearV(offset, { 4, 5, 6 }));
        glm::vec3 lo, hi;
        box.GetBounds(lo, hi);
        CHECK(NearV(lo, { -1, -1, -1 }) && NearV(hi, { 1, 1, 1 }));
        EditableMesh empty;
        CHECK(NearV(empty.RecenterOnBounds(), { 0, 0, 0 }));
    }
}

int main() {
    TestShapes();
    TestTriangulationConcave();
    TestExtrude();
    TestInset();
    TestEditing();
    TestMerge();
    TestUV();
    TestRaycast();
    TestSerialization();
    TestValidation();
    TestExtraOps();
    std::printf("EditableMeshTests: %d verificacoes, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
