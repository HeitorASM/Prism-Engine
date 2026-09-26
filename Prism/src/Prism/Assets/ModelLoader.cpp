#include "ModelLoader.h"
#include "AssetMeta.h" // AssetTypeFromExtension - fonte unica de verdade das extensoes suportadas
#include "../Core/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Prism {

    bool ModelLoader::IsSupportedExtension(const std::filesystem::path& file) {
        // Reusa AssetTypeFromExtension em vez de manter uma segunda lista
        // de extensoes: as extensoes "suportadas pela engine" e "que viram
        // AssetType::Model" sao, por definicao, o mesmo conjunto (ver
        // comentario no .h e em AssetMeta.h).
        return AssetTypeFromExtension(file) == AssetType::Model;
    }

    // Converte um unico aiMesh (aplicando 'worldTransform' aos vertices
    // durante a conversao, se nao-nulo - ver comentario sobre bake de
    // hierarquia em ProcessNode) para MeshVertex/indices, acrescentando ao
    // final de 'out' (para permitir mesclar varios aiMesh em uma unica
    // GeneratedMesh - ver comentario grande no .h sobre por que a engine
    // mescla em vez de preservar submeshes separados). 'baseIndex' e o
    // numero de vertices ja existentes em 'out.Vertices' ANTES desta
    // chamada - os indices de 'mesh' precisam ser deslocados por esse
    // valor para continuar apontando para os vertices certos depois do
    // append.
    //
    // IMPORTANTE: esta funcao NUNCA copia/modifica a struct aiMesh (nem
    // por copia rasa) - aiMesh possui destrutor NAO-TRIVIAL (~aiMesh faz
    // delete[] em mVertices/mNormals/etc, ver assimp/mesh.h) e seus
    // arrays sao POSSUIDOS pelo aiScene devolvido por
    // Assimp::Importer::ReadFile (destruido junto do Importer, no fim de
    // ModelLoader::Load). Uma copia rasa tipo "aiMesh copia = *mesh"
    // duplicaria os PONTEIROS (nao os dados) - o destrutor dessa copia,
    // ao sair de escopo, daria delete[] em memoria que o aiScene original
    // ainda possui e vai tentar deletar de novo depois -> heap corruption
    // / double free (foi exatamente o crash "is_block_type_valid" visto
    // ao importar um .fbx: uma versao anterior desta funcao fazia essa
    // copia). Por isso a transformacao entra como parametro 'transform'
    // separado, aplicada vertice a vertice AQUI DENTRO, sem tocar em
    // 'mesh' (somente leitura, sempre o aiMesh original do aiScene).
    static void AppendAiMesh(const aiMesh* mesh, const aiMatrix4x4* worldTransform, GeneratedMesh& out) {
        if (!mesh->HasPositions() || !mesh->HasFaces())
            return; // mesh vazio ou so-pontos/so-linhas (raro; ignorado silenciosamente)

        uint32_t baseIndex = (uint32_t)out.Vertices.size();
        out.Vertices.reserve(out.Vertices.size() + mesh->mNumVertices);

        bool hasUV = mesh->HasTextureCoords(0);
        bool hasTangents = mesh->HasTangentsAndBitangents();
        bool hasNormals = mesh->HasNormals();

        // Matriz para normais/tangentes (rotacao/escala, SEM translacao -
        // usar worldTransform direto em vetores de direcao arrastaria a
        // translacao de posicao junto, o que e errado). Inversa-transposta
        // para escala nao-uniforme se comportar corretamente; suficiente
        // (nao perfeito) para escala uniforme, que e o caso comum de
        // modelos importados. So calculada quando ha o que transformar.
        aiMatrix3x3 normalMatrix;
        if (worldTransform && (hasNormals || hasTangents)) {
            normalMatrix = aiMatrix3x3(*worldTransform);
            normalMatrix.Inverse().Transpose();
        }

        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            MeshVertex vertex{};

            aiVector3D pos = mesh->mVertices[v];
            if (worldTransform)
                pos = (*worldTransform) * pos;
            vertex.Position[0] = pos.x;
            vertex.Position[1] = pos.y;
            vertex.Position[2] = pos.z;

            if (hasNormals) {
                aiVector3D n = mesh->mNormals[v];
                if (worldTransform)
                    n = (normalMatrix * n).Normalize();
                vertex.Normal[0] = n.x;
                vertex.Normal[1] = n.y;
                vertex.Normal[2] = n.z;
            }
            else {
                // Sem normais no arquivo (raro - .obj sem "vn", por
                // exemplo): aiProcess_GenSmoothNormals (ver Load()) ja
                // deveria ter preenchido isso antes de chegar aqui, mas
                // mantemos um fallback seguro (+Y) para nunca deixar
                // Normal (0,0,0) -> NaN na iluminacao (ver
                // Renderer::DrawMesh/s_VertexSrc).
                vertex.Normal[0] = 0.0f;
                vertex.Normal[1] = 1.0f;
                vertex.Normal[2] = 0.0f;
            }

            if (hasUV) {
                const aiVector3D& uv = mesh->mTextureCoords[0][v];
                vertex.UV[0] = uv.x;
                vertex.UV[1] = uv.y;
            }
            else {
                vertex.UV[0] = 0.0f;
                vertex.UV[1] = 0.0f;
            }

            if (hasTangents) {
                aiVector3D t = mesh->mTangents[v];
                if (worldTransform)
                    t = (normalMatrix * t).Normalize();
                vertex.Tangent[0] = t.x;
                vertex.Tangent[1] = t.y;
                vertex.Tangent[2] = t.z;
            }
            else {
                // aiProcess_CalcTangentSpace (ver Load()) exige UV para
                // calcular tangentes - um mesh sem UV chega aqui sem
                // tangente nenhuma. Eixo X como fallback e o mesmo usado
                // por PrimitiveMeshFactory::CalculateTangents quando o
                // denominador do calculo e proximo de zero.
                vertex.Tangent[0] = 1.0f;
                vertex.Tangent[1] = 0.0f;
                vertex.Tangent[2] = 0.0f;
            }

            out.Vertices.push_back(vertex);
        }

        out.Indices.reserve(out.Indices.size() + (size_t)mesh->mNumFaces * 3);
        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            const aiFace& face = mesh->mFaces[f];
            // aiProcess_Triangulate (ver Load()) garante 3 indices por
            // face - ainda assim checamos, ja que uma face degenerada
            // (linha/ponto sobrevivente) tecnicamente pode ter menos.
            if (face.mNumIndices != 3)
                continue;
            out.Indices.push_back(baseIndex + face.mIndices[0]);
            out.Indices.push_back(baseIndex + face.mIndices[1]);
            out.Indices.push_back(baseIndex + face.mIndices[2]);
        }
    }

    // Percorre a arvore de nos (aiNode) recursivamente, acumulando a
    // transformacao de cada no (aiNode::mTransformation, relativa ao PAI)
    // e aplicando-a aos vertices de cada aiMesh referenciado por aquele
    // no ANTES de acrescentar a 'out' - e assim que um arquivo com
    // "Carroceria" e "Rodas" como nos-filhos, cada um com sua propria
    // posicao/rotacao relativa ao no raiz, aparece na engine no lugar e
    // orientacao CORRETOS mesmo apos o merge (ver comentario grande no
    // .h): a hierarquia de transformacoes e "gravada" (baked) direto nos
    // vertices, ja que MeshRendererComponent nao tem como preservar uma
    // arvore de sub-transformacoes.
    static void ProcessNode(const aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, GeneratedMesh& out) {
        aiMatrix4x4 worldTransform = parentTransform * node->mTransformation;
        bool isIdentity = worldTransform.IsIdentity();

        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            // nullptr quando identidade: AppendAiMesh usa isso para pular
            // o trabalho de transformar vertice a vertice quando nao ha
            // nada a fazer (caso comum: arquivo com um unico no raiz sem
            // rotacao/escala/translacao propria).
            AppendAiMesh(mesh, isIdentity ? nullptr : &worldTransform, out);
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
            ProcessNode(node->mChildren[i], scene, worldTransform, out);
    }

    ModelImportResult ModelLoader::Load(const std::filesystem::path& absolutePath) {
        ModelImportResult result;

        Assimp::Importer importer;

        // aiProcess_Triangulate: garante 3 indices por face (arquivos com
        //   quads/n-gons sao comuns em .obj/.fbx exportados de DCCs como
        //   Blender/3ds Max) - o resto da engine (Mesh, shaders) so
        //   trabalha com triangulos.
        // aiProcess_CalcTangentSpace: gera MeshVertex::Tangent a partir de
        //   posicao+UV, MESMO algoritmo em espirito de
        //   PrimitiveMeshFactory::CalculateTangents - necessario para
        //   normal mapping funcionar em modelos importados (ver
        //   MaterialComponent::NormalPath, Components.h).
        // aiProcess_GenSmoothNormals: gera normais suavizadas (media entre
        //   faces adjacentes) SOMENTE se o arquivo nao ja tiver normais -
        //   Assimp pula este passo automaticamente para meshes que ja
        //   trazem normais no arquivo original, entao nunca sobrescreve
        //   normais "de autor" (ex: hard edges intencionais de um artista).
        // aiProcess_JoinIdenticalVertices: reduz o numero de vertices
        //   duplicados (varios .obj exportam um vertice por combinacao
        //   pos/uv/normal repetida) - reduz o tamanho do VBO final.
        // aiProcess_FlipUVs: Assimp usa a convencao "V cresce para BAIXO"
        //   (comum em DirectX/glTF); MeshVertex::UV documenta a convencao
        //   OPENGL desta engine ("V cresce para CIMA", ver Mesh.h) - sem
        //   este flag, toda textura de um modelo importado apareceria de
        //   cabeca para baixo.
        // aiProcess_LimitBoneWeights: sem efeito em meshes sem esqueleto
        //   (a maioria dos casos aqui, ja que skinning nao e importado -
        //   ver comentario no .h), inofensivo deixar ligado para os que tem.
        unsigned int flags = aiProcess_Triangulate
            | aiProcess_CalcTangentSpace
            | aiProcess_GenSmoothNormals
            | aiProcess_JoinIdenticalVertices
            | aiProcess_FlipUVs
            | aiProcess_ImproveCacheLocality
            | aiProcess_ValidateDataStructure;

        const aiScene* scene = importer.ReadFile(absolutePath.string(), flags);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
            result.Success = false;
            result.Error = importer.GetErrorString();
            if (result.Error.empty())
                result.Error = "Assimp nao conseguiu ler o arquivo (motivo desconhecido).";
            PRISM_CORE_ERROR("ModelLoader: falha ao importar '", absolutePath.string(), "': ", result.Error);
            return result;
        }

        if (scene->mNumMeshes == 0) {
            result.Success = false;
            result.Error = "O arquivo nao contem nenhuma malha (mesh) valida.";
            PRISM_CORE_ERROR("ModelLoader: '", absolutePath.string(), "' nao tem nenhum aiMesh.");
            return result;
        }

        ProcessNode(scene->mRootNode, scene, aiMatrix4x4(), result.Mesh);

        if (result.Mesh.Vertices.empty() || result.Mesh.Indices.empty()) {
            result.Success = false;
            result.Error = "O arquivo nao produziu nenhum triangulo valido apos a importacao.";
            PRISM_CORE_ERROR("ModelLoader: '", absolutePath.string(), "' importou 0 vertices/indices.");
            return result;
        }

        result.Success = true;
        PRISM_CORE_INFO("ModelLoader: '", absolutePath.string(), "' importado (", result.Mesh.Vertices.size(),
            " vertices, ", result.Mesh.Indices.size() / 3, " triangulos, ", scene->mNumMeshes, " submesh(es) mesclados).");
        return result;
    }

}
