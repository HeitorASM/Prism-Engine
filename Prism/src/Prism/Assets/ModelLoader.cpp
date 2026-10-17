#include "ModelLoader.h"
#include "AssetMeta.h" // AssetTypeFromExtension - fonte unica de verdade das extensoes suportadas
#include "../Core/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

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

    // Estado compartilhado pela travessia da arvore de nos: para cada
    // indice de material do Assimp, qual ImportedSubmesh (em 'Submeshes')
    // ja foi criado para ele (-1 = ainda nenhum). Criado sob demanda, na
    // ordem em que os meshes aparecem - assim materiais sem nenhum
    // triangulo valido nao geram submesh vazio.
    struct ImportContext {
        const aiScene* Scene = nullptr;
        std::vector<ImportedSubmesh>* Submeshes = nullptr;
        std::vector<int> MaterialToSubmesh;
    };

    // Percorre a arvore de nos (aiNode) recursivamente, acumulando a
    // transformacao de cada no (aiNode::mTransformation, relativa ao PAI)
    // e aplicando-a aos vertices de cada aiMesh referenciado por aquele
    // no ANTES de acrescentar ao submesh do material dele - e assim que um
    // arquivo com "Carroceria" e "Rodas" como nos-filhos, cada um com sua
    // propria posicao/rotacao relativa ao no raiz, aparece na engine no
    // lugar e orientacao CORRETOS: a hierarquia de transformacoes e
    // "gravada" (baked) direto nos vertices, ja que MeshRendererComponent
    // nao tem como preservar uma arvore de sub-transformacoes.
    static void ProcessNode(const aiNode* node, ImportContext& ctx, const aiMatrix4x4& parentTransform) {
        aiMatrix4x4 worldTransform = parentTransform * node->mTransformation;
        bool isIdentity = worldTransform.IsIdentity();

        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            const aiMesh* mesh = ctx.Scene->mMeshes[node->mMeshes[i]];
            if (!mesh->HasPositions() || !mesh->HasFaces())
                continue;

            unsigned int materialIndex = mesh->mMaterialIndex;
            if (materialIndex >= ctx.MaterialToSubmesh.size())
                materialIndex = 0; // indice invalido (arquivo estranho): usa o primeiro material
            int& submeshIndex = ctx.MaterialToSubmesh[materialIndex];
            if (submeshIndex < 0) {
                submeshIndex = (int)ctx.Submeshes->size();
                ctx.Submeshes->emplace_back();
            }
            // nullptr quando identidade: AppendAiMesh usa isso para pular
            // o trabalho de transformar vertice a vertice quando nao ha
            // nada a fazer (caso comum: arquivo com um unico no raiz sem
            // rotacao/escala/translacao propria).
            AppendAiMesh(mesh, isIdentity ? nullptr : &worldTransform, (*ctx.Submeshes)[submeshIndex].Mesh);
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
            ProcessNode(node->mChildren[i], ctx, worldTransform);
    }

    static std::string SanitizeFileName(std::string name) {
        for (char& c : name) {
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
                c = '_';
        }
        if (name.empty() || name == "." || name == "..")
            name = "texture";
        return name;
    }

    // Resolve a textura de 'type' (primeira do tipo, indice 0) de 'material'
    // para um caminho ABSOLUTO em disco, ou vazio se nao houver / nao puder
    // ser resolvida. Dois casos:
    //   - Textura EMBUTIDA no arquivo (referencia "*N" ou nome casando com
    //     scene->GetEmbeddedTexture): so e usada se options pedir extracao;
    //     os bytes (PNG/JPG ja comprimidos, mHeight == 0) sao gravados em
    //     options.TextureOutputDir e o caminho gravado e devolvido. Texturas
    //     embutidas NAO comprimidas (mHeight > 0, pixels crus) sao raras e
    //     nao sao suportadas (aviso no log).
    //   - Textura EXTERNA: o Assimp devolve o caminho como escrito no
    //     arquivo (geralmente relativo a pasta do modelo, as vezes com '\\'
    //     do Windows ou absoluto de outra maquina). Tenta relativo a pasta
    //     do modelo e, se nao existir, so o nome do arquivo na pasta do
    //     modelo.
    static std::filesystem::path ResolveTexture(const aiScene* scene, const aiMaterial* material, aiTextureType type,
        const std::filesystem::path& modelDir, const std::string& modelStem, const ModelImportOptions& options) {
        aiString ref;
        if (material->GetTextureCount(type) == 0 || material->GetTexture(type, 0, &ref) != AI_SUCCESS)
            return {};
        std::string reference = ref.C_Str();
        if (reference.empty())
            return {};

        if (const aiTexture* embedded = scene->GetEmbeddedTexture(reference.c_str())) {
            if (!options.ExtractEmbeddedTextures || options.TextureOutputDir.empty())
                return {};
            if (embedded->mHeight != 0) {
                PRISM_CORE_WARN("ModelLoader: textura embutida '", reference, "' nao esta comprimida (PNG/JPG) - ignorada.");
                return {};
            }

            std::string extension = embedded->achFormatHint; // ex: "png", "jpg"
            if (extension.empty() || extension.size() > 4)
                extension = "png";
            std::string baseName;
            if (reference[0] == '*')
                baseName = modelStem + "_tex" + reference.substr(1);
            else
                baseName = std::filesystem::path(reference).stem().string();
            std::filesystem::path outPath = options.TextureOutputDir / (SanitizeFileName(baseName) + "." + SanitizeFileName(extension));

            std::error_code ec;
            std::filesystem::create_directories(options.TextureOutputDir, ec);
            if (!std::filesystem::exists(outPath, ec)) {
                std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) {
                    PRISM_CORE_ERROR("ModelLoader: nao foi possivel gravar a textura extraida: ", outPath.string());
                    return {};
                }
                out.write(reinterpret_cast<const char*>(embedded->pcData), (std::streamsize)embedded->mWidth);
            }
            return outPath;
        }

        std::replace(reference.begin(), reference.end(), '\\', '/');
        std::filesystem::path texturePath(reference);
        std::error_code ec;
        if (texturePath.is_relative())
            texturePath = modelDir / texturePath;
        if (!std::filesystem::exists(texturePath, ec))
            texturePath = modelDir / std::filesystem::path(reference).filename();
        if (!std::filesystem::exists(texturePath, ec)) {
            PRISM_CORE_WARN("ModelLoader: textura '", reference, "' referenciada pelo modelo nao foi encontrada.");
            return {};
        }
        return texturePath.lexically_normal();
    }

    // Traduz um aiMaterial para ImportedMaterial. Para glTF, a textura
    // metallic-roughness vem como aiTextureType_UNKNOWN (indice 0) e ja usa
    // a convencao da engine (G = roughness, B = metallic); para .obj/.fbx
    // nao ha equivalente, e os fatores ficam nos valores padrao.
    static ImportedMaterial BuildMaterial(const aiScene* scene, const aiMaterial* material, bool isGltf,
        const std::filesystem::path& modelDir, const std::string& modelStem, const ModelImportOptions& options) {
        ImportedMaterial result;

        aiString name;
        if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
            result.Name = name.C_Str();

        result.AlbedoPath = ResolveTexture(scene, material, aiTextureType_BASE_COLOR, modelDir, modelStem, options);
        if (result.AlbedoPath.empty())
            result.AlbedoPath = ResolveTexture(scene, material, aiTextureType_DIFFUSE, modelDir, modelStem, options);

        result.NormalPath = ResolveTexture(scene, material, aiTextureType_NORMALS, modelDir, modelStem, options);
        if (result.NormalPath.empty())
            result.NormalPath = ResolveTexture(scene, material, aiTextureType_HEIGHT, modelDir, modelStem, options); // .obj: "map_bump" cai em HEIGHT

        if (isGltf)
            result.RoughnessMetallicPath = ResolveTexture(scene, material, aiTextureType_UNKNOWN, modelDir, modelStem, options);

        aiColor4D color;
        if (material->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS)
            result.AlbedoTint = { color.r, color.g, color.b };
        else if (result.AlbedoPath.empty() && material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            result.AlbedoTint = { color.r, color.g, color.b };
        // Com textura de albedo e sem fator PBR explicito (.obj/.fbx), o
        // "Kd" costuma ser so um cinza de fallback - multiplicar a textura
        // por ele escureceria o modelo, entao o tint fica branco.

        float factor = 0.0f;
        if (material->Get(AI_MATKEY_METALLIC_FACTOR, factor) == AI_SUCCESS)
            result.Metallic = std::clamp(factor, 0.0f, 1.0f);
        if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS)
            result.Roughness = std::clamp(factor, 0.0f, 1.0f);

        return result;
    }

    ModelImportResult ModelLoader::Load(const std::filesystem::path& absolutePath, const ModelImportOptions& options) {
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
        // NAO usar aiProcess_FlipUVs: Texture2D ja carrega a imagem
        //   invertida em Y (stbi_set_flip_vertically_on_load, ver
        //   Texture.cpp), ou seja, espera UV na convencao OpenGL (V cresce
        //   para CIMA). O Assimp ja entrega UV nessa convencao para .obj e
        //   .fbx, e o importador de glTF ja converte (v = 1 - v) por conta
        //   propria - aplicar FlipUVs por cima invertia a textura de TODO
        //   modelo importado.
        // aiProcess_LimitBoneWeights: sem efeito em meshes sem esqueleto
        //   (a maioria dos casos aqui, ja que skinning nao e importado -
        //   ver comentario no .h), inofensivo deixar ligado para os que tem.
        unsigned int flags = aiProcess_Triangulate
            | aiProcess_CalcTangentSpace
            | aiProcess_GenSmoothNormals
            | aiProcess_JoinIdenticalVertices
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

        ImportContext ctx;
        ctx.Scene = scene;
        ctx.Submeshes = &result.Submeshes;
        ctx.MaterialToSubmesh.assign(std::max(1u, scene->mNumMaterials), -1);
        ProcessNode(scene->mRootNode, ctx, aiMatrix4x4());

        // Materiais dos submeshes + malha mesclada (todos os submeshes
        // concatenados, com os indices deslocados pelo numero de vertices ja
        // acumulados).
        std::string extension = absolutePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        bool isGltf = (extension == ".gltf" || extension == ".glb");
        std::filesystem::path modelDir = absolutePath.parent_path();
        std::string modelStem = absolutePath.stem().string();

        for (unsigned int materialIndex = 0; materialIndex < ctx.MaterialToSubmesh.size(); materialIndex++) {
            int submeshIndex = ctx.MaterialToSubmesh[materialIndex];
            if (submeshIndex < 0)
                continue;
            ImportedSubmesh& submesh = result.Submeshes[submeshIndex];
            if (materialIndex < scene->mNumMaterials)
                submesh.Material = BuildMaterial(scene, scene->mMaterials[materialIndex], isGltf, modelDir, modelStem, options);
            submesh.Name = submesh.Material.Name.empty() ? ("Submesh " + std::to_string(submeshIndex)) : submesh.Material.Name;
        }

        for (const ImportedSubmesh& submesh : result.Submeshes) {
            uint32_t baseVertex = (uint32_t)result.Mesh.Vertices.size();
            result.Mesh.Vertices.insert(result.Mesh.Vertices.end(), submesh.Mesh.Vertices.begin(), submesh.Mesh.Vertices.end());
            result.Mesh.Indices.reserve(result.Mesh.Indices.size() + submesh.Mesh.Indices.size());
            for (uint32_t index : submesh.Mesh.Indices)
                result.Mesh.Indices.push_back(baseVertex + index);
        }

        if (result.Mesh.Vertices.empty() || result.Mesh.Indices.empty()) {
            result.Success = false;
            result.Error = "O arquivo nao produziu nenhum triangulo valido apos a importacao.";
            PRISM_CORE_ERROR("ModelLoader: '", absolutePath.string(), "' importou 0 vertices/indices.");
            return result;
        }

        result.Success = true;
        PRISM_CORE_INFO("ModelLoader: '", absolutePath.string(), "' importado (", result.Mesh.Vertices.size(),
            " vertices, ", result.Mesh.Indices.size() / 3, " triangulos, ", result.Submeshes.size(), " material(is)/submesh(es)).");
        return result;
    }

}
