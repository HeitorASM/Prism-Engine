#include "ModelLoader.h"
#include "AssetMeta.h" // AssetTypeFromExtension - fonte unica de verdade das extensoes suportadas
#include "../Core/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <system_error>
#include <unordered_map>

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

    static glm::mat4 ToGlm(const aiMatrix4x4& m) {
        // aiMatrix4x4 e row-major; glm e column-major.
        return glm::transpose(glm::make_mat4(&m.a1));
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

        // --- Animacao (so quando HasRig) ---
        bool HasRig = false;
        std::shared_ptr<ModelRig> Rig;
        std::unordered_map<const aiNode*, int> NodeIndex;
        std::unordered_map<std::string, int> NodeByName;
        std::vector<glm::mat4> BindGlobal;       // por no
        std::vector<char> Dynamic;               // no (ou ancestral) animado por algum clipe
        std::unordered_map<int, int> NodeBone;   // no -> osso sintetico (malhas rigidas)
        std::unordered_map<std::string, std::vector<int>> BonesByName;
        bool WarnedBoneLimit = false;
    };

    static bool NearlyEqual(const glm::mat4& a, const glm::mat4& b) {
        const float* pa = glm::value_ptr(a);
        const float* pb = glm::value_ptr(b);
        for (int i = 0; i < 16; i++)
            if (std::abs(pa[i] - pb[i]) > 1e-4f)
                return false;
        return true;
    }

    // Osso para (nome do no, matriz de bind inversa). Mesma combinacao = mesmo osso.
    // -1 se a paleta estourou kMaxBones.
    static int GetOrCreateBone(ImportContext& ctx, const std::string& name, int node, const glm::mat4& inverseBind) {
        auto& list = ctx.BonesByName[name];
        for (int index : list)
            if (NearlyEqual(ctx.Rig->Bones[index].InverseBind, inverseBind))
                return index;
        if (ctx.Rig->Bones.size() >= kMaxBones) {
            if (!ctx.WarnedBoneLimit) {
                ctx.WarnedBoneLimit = true;
                PRISM_CORE_WARN("ModelLoader: o modelo tem mais de ", kMaxBones, " ossos - os excedentes sao ignorados (parte da malha fica sem animar).");
            }
            return -1;
        }
        RigBone bone;
        bone.Node = node;
        bone.InverseBind = inverseBind;
        ctx.Rig->Bones.push_back(bone);
        int index = (int)ctx.Rig->Bones.size() - 1;
        list.push_back(index);
        return index;
    }

    // Preenche BoneIds/BoneWeights dos vertices [baseIndex, ...) de 'out' a
    // partir dos aiBone do mesh (maximo 4 influencias por vertice, as mais pesadas).
    static void ApplySkin(const aiMesh* mesh, ImportContext& ctx, std::vector<MeshVertex>& vertices, uint32_t baseIndex) {
        struct Influence { int Bone; float Weight; };
        std::vector<std::vector<Influence>> influences(mesh->mNumVertices);

        for (unsigned int b = 0; b < mesh->mNumBones; b++) {
            const aiBone* bone = mesh->mBones[b];
            auto nodeIt = ctx.NodeByName.find(bone->mName.C_Str());
            if (nodeIt == ctx.NodeByName.end())
                continue; // osso sem no correspondente (arquivo estranho)
            int boneIndex = GetOrCreateBone(ctx, bone->mName.C_Str(), nodeIt->second, ToGlm(bone->mOffsetMatrix));
            if (boneIndex < 0)
                continue;
            for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                const aiVertexWeight& vw = bone->mWeights[w];
                if (vw.mVertexId < influences.size() && vw.mWeight > 0.0f)
                    influences[vw.mVertexId].push_back({ boneIndex, vw.mWeight });
            }
        }

        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            auto& list = influences[v];
            if (list.empty())
                continue;
            std::sort(list.begin(), list.end(), [](const Influence& a, const Influence& b) { return a.Weight > b.Weight; });
            if (list.size() > 4)
                list.resize(4);
            float sum = 0.0f;
            for (const Influence& i : list)
                sum += i.Weight;
            if (sum <= 0.0f)
                continue;
            MeshVertex& vertex = vertices[baseIndex + v];
            for (size_t i = 0; i < list.size(); i++) {
                vertex.BoneIds[i] = list[i].Bone;
                vertex.BoneWeights[i] = list[i].Weight / sum;
            }
        }
    }

    // Malha rigida de um no animado: todos os vertices 100% no osso sintetico do no.
    static void ApplyRigid(int node, ImportContext& ctx, std::vector<MeshVertex>& vertices, uint32_t baseIndex) {
        int boneIndex;
        auto it = ctx.NodeBone.find(node);
        if (it != ctx.NodeBone.end()) {
            boneIndex = it->second;
        }
        else {
            // Chave propria (prefixo) para nunca casar com um osso real de mesmo nome.
            boneIndex = GetOrCreateBone(ctx, "\x01rigid:" + ctx.Rig->Nodes[node].Name + "#" + std::to_string(node), node, glm::inverse(ctx.BindGlobal[node]));
            ctx.NodeBone[node] = boneIndex;
        }
        if (boneIndex < 0)
            return;
        for (size_t v = baseIndex; v < vertices.size(); v++) {
            vertices[v].BoneIds[0] = boneIndex;
            vertices[v].BoneWeights[0] = 1.0f;
        }
    }

    // Monta nos, clipes e tabelas de "no dinamico" do rig. Chamada so quando o
    // arquivo tem ossos ou animacoes (ctx.HasRig).
    static void BuildRig(ImportContext& ctx) {
        const aiScene* scene = ctx.Scene;
        ctx.Rig = std::make_shared<ModelRig>();
        ModelRig& rig = *ctx.Rig;

        // Nos em pre-ordem.
        struct Walker {
            ImportContext& ctx;
            ModelRig& rig;
            void Visit(const aiNode* node, int parent) {
                int index = (int)rig.Nodes.size();
                RigNode n;
                n.Name = node->mName.C_Str();
                n.Parent = parent;
                n.LocalBind = ToGlm(node->mTransformation);
                rig.Nodes.push_back(n);
                rig.BindTRS.push_back(ModelRig::Decompose(n.LocalBind));
                ctx.BindGlobal.push_back(parent >= 0 ? ctx.BindGlobal[parent] * n.LocalBind : n.LocalBind);
                ctx.NodeIndex[node] = index;
                ctx.NodeByName.emplace(n.Name, index); // nomes repetidos: vale o primeiro
                for (unsigned int i = 0; i < node->mNumChildren; i++)
                    Visit(node->mChildren[i], index);
            }
        } walker{ ctx, rig };
        walker.Visit(scene->mRootNode, -1);

        std::vector<char> animated(rig.Nodes.size(), 0);

        for (unsigned int a = 0; a < scene->mNumAnimations; a++) {
            const aiAnimation* anim = scene->mAnimations[a];
            double tps = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;

            AnimClip clip;
            clip.Name = anim->mName.C_Str();
            if (clip.Name.empty())
                clip.Name = "Animation " + std::to_string(a);
            // Nome unico dentro do modelo (PlayAnimation busca por nome).
            for (const AnimClip& other : rig.Clips)
                if (other.Name == clip.Name) {
                    clip.Name += "_" + std::to_string(a);
                    break;
                }

            float maxTime = 0.0f;
            for (unsigned int c = 0; c < anim->mNumChannels; c++) {
                const aiNodeAnim* nodeAnim = anim->mChannels[c];
                auto nodeIt = ctx.NodeByName.find(nodeAnim->mNodeName.C_Str());
                if (nodeIt == ctx.NodeByName.end())
                    continue;
                AnimChannel channel;
                channel.Node = nodeIt->second;

                for (unsigned int k = 0; k < nodeAnim->mNumPositionKeys; k++) {
                    const aiVectorKey& key = nodeAnim->mPositionKeys[k];
                    channel.Position.push_back({ (float)(key.mTime / tps), glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z) });
                }
                for (unsigned int k = 0; k < nodeAnim->mNumRotationKeys; k++) {
                    const aiQuatKey& key = nodeAnim->mRotationKeys[k];
                    channel.Rotation.push_back({ (float)(key.mTime / tps), glm::normalize(glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z)) });
                }
                for (unsigned int k = 0; k < nodeAnim->mNumScalingKeys; k++) {
                    const aiVectorKey& key = nodeAnim->mScalingKeys[k];
                    channel.Scale.push_back({ (float)(key.mTime / tps), glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z) });
                }
                if (!channel.Position.empty()) maxTime = std::max(maxTime, channel.Position.back().Time);
                if (!channel.Rotation.empty()) maxTime = std::max(maxTime, channel.Rotation.back().Time);
                if (!channel.Scale.empty())    maxTime = std::max(maxTime, channel.Scale.back().Time);

                animated[channel.Node] = 1;
                clip.Channels.push_back(std::move(channel));
            }
            clip.Duration = std::max((float)(anim->mDuration / tps), maxTime);
            if (!clip.Channels.empty())
                rig.Clips.push_back(std::move(clip));
        }

        // No "dinamico" = animado ou descendente de um animado (pre-ordem: pai primeiro).
        ctx.Dynamic.assign(rig.Nodes.size(), 0);
        for (size_t i = 0; i < rig.Nodes.size(); i++) {
            int parent = rig.Nodes[i].Parent;
            ctx.Dynamic[i] = animated[i] || (parent >= 0 && ctx.Dynamic[parent]);
        }
    }

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
            GeneratedMesh& target = (*ctx.Submeshes)[submeshIndex].Mesh;
            const uint32_t baseIndex = (uint32_t)target.Vertices.size();

            // Malha SKINADA: sem transformacao de no gravada (os ossos fazem
            // isso). Malha rigida de um no animado: gravada (pose de bind) +
            // osso sintetico, para seguir o no quando o clipe tocar.
            const bool skinned = ctx.HasRig && mesh->HasBones();
            const int nodeIndex = ctx.HasRig ? ctx.NodeIndex[node] : -1;
            const bool rigid = ctx.HasRig && !skinned && nodeIndex >= 0 && ctx.Dynamic[nodeIndex];

            // nullptr quando identidade: AppendAiMesh usa isso para pular
            // o trabalho de transformar vertice a vertice quando nao ha
            // nada a fazer (caso comum: arquivo com um unico no raiz sem
            // rotacao/escala/translacao propria).
            AppendAiMesh(mesh, (skinned || isIdentity) ? nullptr : &worldTransform, target);
            if (skinned)
                ApplySkin(mesh, ctx, target.Vertices, baseIndex);
            else if (rigid)
                ApplyRigid(nodeIndex, ctx, target.Vertices, baseIndex);
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
        //   limita a 4 ossos por vertice (o que o shader de skinning le).
        unsigned int flags = aiProcess_Triangulate
            | aiProcess_CalcTangentSpace
            | aiProcess_GenSmoothNormals
            | aiProcess_JoinIdenticalVertices
            | aiProcess_LimitBoneWeights
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

        // Tem esqueleto ou animacao? Entao monta o rig ANTES de percorrer as malhas.
        bool anyBones = false;
        for (unsigned int m = 0; m < scene->mNumMeshes && !anyBones; m++)
            anyBones = scene->mMeshes[m]->HasBones();
        ctx.HasRig = anyBones || scene->mNumAnimations > 0;
        if (ctx.HasRig)
            BuildRig(ctx);

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

        if (ctx.HasRig) {
            // Rig sem ossos nem clipes (animacao so de nos sem alvo valido) nao serve de nada.
            if (!ctx.Rig->Bones.empty() || !ctx.Rig->Clips.empty()) {
                ctx.Rig->ComputePalette(ctx.Rig->BindTRS, ctx.Rig->BindPalette);
                result.Rig = ctx.Rig;
            }
        }

        result.Success = true;
        PRISM_CORE_INFO("ModelLoader: '", absolutePath.string(), "' importado (", result.Mesh.Vertices.size(),
            " vertices, ", result.Mesh.Indices.size() / 3, " triangulos, ", result.Submeshes.size(), " material(is)/submesh(es)",
            result.Rig ? (", " + std::to_string(result.Rig->Bones.size()) + " osso(s), " + std::to_string(result.Rig->Clips.size()) + " animacao(oes)") : std::string(), ").");
        return result;
    }

}
