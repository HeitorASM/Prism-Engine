#pragma once

// ============================================================================
// ModelRig.h
// Esqueleto + clipes de animacao de um modelo importado (ver ModelLoader).
// Dado IMUTAVEL depois de criado, compartilhado (shared_ptr) entre o cache do
// Renderer e todos os Animators que usam o mesmo modelo.
//
//   Nodes   : a hierarquia de nos do arquivo, em PRE-ORDEM (o pai sempre vem
//             antes dos filhos - o calculo das matrizes globais e um laco so).
//   Bones   : um osso por (no, matriz de bind inversa). Os ids gravados nos
//             vertices (MeshVertex::BoneIds) indexam este vetor e a paleta
//             enviada ao shader (u_Bones). Maximo kMaxBones.
//             Malhas RIGIDAS de nos animados (portas, helices) ganham um osso
//             "sintetico" de peso 1 - assim usam o mesmo caminho do skinning.
//   Clips   : animacoes (tempos em SEGUNDOS).
//
// Convencoes: matrizes glm column-major; espaco do rig = espaco do no raiz do
// arquivo (as malhas skinadas NAO tem a transformacao de no "gravada" nos
// vertices - o osso faz isso).
// ============================================================================

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace Prism {

    // Limite da paleta de ossos (uniform mat4 u_Bones[128] nos shaders).
    constexpr uint32_t kMaxBones = 128;

    struct NodeTRS {
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };
    };

    struct RigNode {
        std::string Name;
        int Parent = -1;
        glm::mat4 LocalBind{ 1.0f };
    };

    struct RigBone {
        int Node = 0;
        glm::mat4 InverseBind{ 1.0f }; // espaco da malha -> espaco do osso
    };

    template<typename T>
    struct AnimKey {
        float Time = 0.0f;
        T Value{};
    };

    // Animacao de UM no. Canal sem chaves de um tipo = mantem o valor do bind.
    struct AnimChannel {
        int Node = 0;
        std::vector<AnimKey<glm::vec3>> Position;
        std::vector<AnimKey<glm::quat>> Rotation;
        std::vector<AnimKey<glm::vec3>> Scale;
    };

    struct AnimClip {
        std::string Name;
        float Duration = 0.0f; // segundos
        std::vector<AnimChannel> Channels;
    };

    struct ModelRig {
        std::vector<RigNode> Nodes;
        std::vector<NodeTRS> BindTRS;          // LocalBind decomposto (1 por no)
        std::vector<RigBone> Bones;
        std::vector<AnimClip> Clips;
        std::vector<glm::mat4> BindPalette;    // paleta na pose de bind (Bones.size())

        // Indice do clipe pelo nome (exato; se nao achar, sem diferenciar
        // maiusculas). -1 = nao existe.
        int FindClip(const std::string& name) const;

        // 'out' = pose de bind com os canais do clipe aplicados em 'time'
        // (segundos, ja dentro de [0, Duration]).
        void SamplePose(int clip, float time, std::vector<NodeTRS>& out) const;

        // out = mistura de a (w=0) e b (w=1). Os tres podem ser o mesmo vetor.
        static void BlendPoses(const std::vector<NodeTRS>& a, const std::vector<NodeTRS>& b, float w, std::vector<NodeTRS>& out);

        // Matrizes globais dos nos a partir da pose, e a paleta final
        // (global do osso * bind inversa) - 'palette' ganha Bones.size() itens.
        void ComputePalette(const std::vector<NodeTRS>& pose, std::vector<glm::mat4>& palette) const;

        // Translacao do no 'node' no clipe em 'time' (so o canal desse no; sem canal = bind).
        glm::vec3 SampleNodeTranslation(int clip, float time, int node) const;

        // No do osso mais proximo da raiz (menor profundidade; empate = primeiro) - usado
        // pelo root motion (normalmente o "Hips"/"Pelvis"). -1 se nao ha ossos.
        int FindRootBoneNode() const;

        // Decompoe uma matriz em T/R/S (sem cisalhamento).
        static NodeTRS Decompose(const glm::mat4& m);
        static glm::mat4 Compose(const NodeTRS& trs);
    };

}
