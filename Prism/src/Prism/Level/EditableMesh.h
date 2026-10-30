#pragma once

// ============================================================================
// EditableMesh.h
// Malha POLIGONAL editavel, a base do Level Builder (estilo ProBuilder).
//
// Diferente do Mesh (GPU, imutavel, so triangulos), aqui o dado e o que um
// editor de niveis precisa manipular: vertices COMPARTILHADOS (indexados) e
// faces com N lados (quad, n-gon, ate poligonos concavos planares). Cada face
// guarda o seu slot de material, grupo de suavizacao e ajustes de UV.
//
// So depende de std + glm (nenhuma chamada OpenGL): compila e e testado
// isoladamente (tests/EditableMeshTests.cpp). O resultado para a GPU/fisica
// sai de Build() (por slot de material) e e convertido em Mesh pelo renderer.
//
// CONVENCOES (as mesmas da engine, ver PrimitiveMeshFactory):
//  - Frente da face = sentido ANTI-HORARIO visto de FORA (normal para fora).
//  - Y para cima; formas sao centradas na origem (centro da caixa envolvente).
//  - UV automatico POR POSICAO: o mesmo ponto 3D sempre tem o mesmo UV
//    (1 unidade = 1 repeticao por padrao), entao faces coplanares vizinhas
//    emendam sem costura e a textura nao estica quando a face muda de tamanho.
//
// OPERACOES devolvem os indices das faces resultantes (para o editor manter a
// selecao). Indices de FACE mudam apos DeleteFaces/MergeVertices; os de
// vertice mudam apos RemoveUnusedVertices/MergeVertices.
// ============================================================================

#include <glm/glm.hpp>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace Prism::Level {

    // Ajustes de UV de UMA face (modo automatico: projecao no plano da face).
    struct FaceUV {
        glm::vec2 Offset{ 0.0f };   // deslocamento em "tiles"
        glm::vec2 Tiling{ 1.0f };   // repeticoes por unidade de mundo
        float RotationDeg = 0.0f;   // gira a textura sobre a face
        bool FlipU = false;
        bool FlipV = false;
    };

    struct Face {
        std::vector<uint32_t> Indices;  // >= 3, anti-horario visto de fora
        uint32_t MaterialSlot = 0;      // indice na lista de materiais do objeto
        uint32_t SmoothingGroup = 0;    // 0 = sombreamento chapado; igual e != 0 = normais suavizadas entre vizinhas
        FaceUV UV;
    };

    // Saida de Build(): vertices ja separados por face (normal/UV por canto).
    struct BuiltVertex {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
        glm::vec2 UV{ 0.0f };
        glm::vec3 Tangent{ 1.0f, 0.0f, 0.0f };   // direcao em que U cresce (normal mapping)
    };
    struct BuiltSubmesh {
        uint32_t MaterialSlot = 0;
        std::vector<BuiltVertex> Vertices;
        std::vector<uint32_t> Indices;           // triangulos anti-horarios
    };

    class EditableMesh {
    public:
        std::vector<glm::vec3> Vertices;
        std::vector<Face> Faces;

        // --- formas (centradas na origem) -------------------------------------
        static EditableMesh CreateBox(const glm::vec3& size);
        // Plano no XZ virado para +Y, dividido em subX x subZ quadrados.
        static EditableMesh CreatePlane(const glm::vec2& size, uint32_t subdivisionsX = 1, uint32_t subdivisionsZ = 1);
        // Cilindro fechado alinhado a Y (lados suavizados, tampas chapadas).
        static EditableMesh CreateCylinder(float radius, float height, uint32_t segments = 16);
        // Escada fechada que sobe em direcao a +Z (tamanho total = largura, altura, profundidade).
        static EditableMesh CreateStairs(const glm::vec3& size, uint32_t steps = 6);
        // Rampa (cunha) que sobe em direcao a +Z.
        static EditableMesh CreateRamp(const glm::vec3& size);

        // --- consulta -----------------------------------------------------------
        // false se algum indice sai do intervalo, uma face tem < 3 vertices ou
        // coordenada nao finita. 'why' (opcional) recebe o motivo.
        bool IsValid(std::string* why = nullptr) const;
        // Toda aresta aparece exatamente uma vez em cada sentido (superficie fechada,
        // sem buracos nem arestas de 3 faces): requisito para colisao solida confiavel.
        bool IsClosed() const;
        glm::vec3 FaceNormal(uint32_t face) const;   // unitaria (Newell); (0,1,0) se degenerada
        glm::vec3 FaceCenter(uint32_t face) const;   // media dos vertices
        float FaceArea(uint32_t face) const;
        void GetBounds(glm::vec3& outMin, glm::vec3& outMax) const;
        // Indices unicos (ordenados) dos vertices usados pelas faces dadas.
        std::vector<uint32_t> VerticesOfFaces(const std::vector<uint32_t>& faces) const;

        // --- edicao ---------------------------------------------------------------
        // Puxa as faces para fora (distance > 0) ou para dentro (< 0) e fecha as laterais
        // so nas arestas de BORDA da selecao (arestas entre duas faces selecionadas nao
        // ganham parede). Cada vertice anda pela media das normais das faces selecionadas
        // que o usam. Devolve as mesmas faces (agora movidas); as paredes novas vao ao fim de Faces.
        std::vector<uint32_t> ExtrudeFaces(const std::vector<uint32_t>& faces, float distance);
        // Encolhe CADA face para dentro por 'amount' (largura da moldura constante) e cria a
        // moldura de quads em volta. Devolve as faces internas (as mesmas indicadas).
        // Use um 'amount' menor que a metade da menor largura da face.
        std::vector<uint32_t> InsetFaces(const std::vector<uint32_t>& faces, float amount);
        void MoveVertices(const std::vector<uint32_t>& vertices, const glm::vec3& delta);
        // Aplica 'm' (em torno da origem do espaco do objeto) aos vertices dados.
        void TransformVertices(const std::vector<uint32_t>& vertices, const glm::mat4& m);
        void FlipFaces(const std::vector<uint32_t>& faces);
        void DeleteFaces(const std::vector<uint32_t>& faces);
        void SetFaceMaterial(const std::vector<uint32_t>& faces, uint32_t slot);
        void SetSmoothingGroup(const std::vector<uint32_t>& faces, uint32_t group);
        // Solda vertices a menos de 'epsilon', remove vertices repetidos dentro de uma face,
        // faces que ficaram com < 3 vertices e vertices sem uso.
        void MergeVertices(float epsilon = 1e-4f);
        void RemoveUnusedVertices();

        // --- operacoes extras (4d) ---------------------------------------------------
        // Arestas unicas (a < b) de todas as faces, em ordem estavel.
        std::vector<std::pair<uint32_t, uint32_t>> UniqueEdges() const;
        // Faces que usam algum dos vertices dados.
        std::vector<uint32_t> FacesUsingVertices(const std::vector<uint32_t>& vertices) const;
        // Divide cada face em quadrilateros ao redor do centro (um por canto; vira 4 quads num quad).
        // Mantem material/UV/grupo. Faces originais sao substituidas; devolve as novas faces.
        std::vector<uint32_t> SubdivideFaces(const std::vector<uint32_t>& faces);
        // Duplica os vertices das faces para que deixem de ser compartilhados com as outras
        // (a geometria fica no lugar). Devolve as mesmas faces. Util antes de mover so elas.
        std::vector<uint32_t> DetachFaces(const std::vector<uint32_t>& faces);
        // Reflete a malha no plano que passa pela origem e e perpendicular ao eixo (0=X,1=Y,2=Z)
        // e inverte o sentido das faces (as normais continuam para fora).
        void Mirror(int axis);
        // Acrescenta uma copia de 'other' transformada por 'm' (slots de material preservados).
        // Se 'm' tem determinante negativo (espelho) o sentido das faces e corrigido. Devolve o
        // indice da primeira face acrescentada.
        uint32_t Append(const EditableMesh& other, const glm::mat4& m = glm::mat4(1.0f));
        // Move a malha para que o centro da caixa envolvente fique na origem. Devolve o deslocamento
        // aplicado ao CENTRO ANTIGO (posicao, no espaco do objeto, que passou a ser a origem): somar
        // este valor (rotacionado/escalado) a posicao do objeto mantem a malha parada no mundo.
        glm::vec3 RecenterOnBounds();

        // --- saida ------------------------------------------------------------------
        // Triangula (ear clipping, aceita concavas), calcula normais (chapadas ou suavizadas
        // por grupo), UV automatico e tangentes. Um BuiltSubmesh por slot USADO, em ordem crescente.
        std::vector<BuiltSubmesh> Build() const;
        // Raio contra as faces (espaco do objeto). Devolve a mais proxima; com cullBackfaces
        // so conta faces viradas para a origem do raio (selecao no editor).
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float& outDistance, int& outFace, bool cullBackfaces = true) const;

        // --- arquivo (binario, versionado; Read nunca trava com dados truncados/corrompidos) ---
        void Write(std::ostream& out) const;
        static bool Read(std::istream& in, EditableMesh& outMesh);
    };

}
