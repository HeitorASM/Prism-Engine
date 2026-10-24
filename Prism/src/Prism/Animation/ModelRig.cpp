#include "ModelRig.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Prism {

    static std::string LowerCopy(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return r;
    }

    int ModelRig::FindClip(const std::string& name) const {
        for (size_t i = 0; i < Clips.size(); i++)
            if (Clips[i].Name == name)
                return (int)i;
        std::string lower = LowerCopy(name);
        for (size_t i = 0; i < Clips.size(); i++)
            if (LowerCopy(Clips[i].Name) == lower)
                return (int)i;
        return -1;
    }

    // Indice da ultima chave com Time <= t (0 se t antes da primeira).
    template<typename T>
    static size_t FindKey(const std::vector<AnimKey<T>>& keys, float t) {
        auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float value, const AnimKey<T>& key) { return value < key.Time; });
        if (it == keys.begin())
            return 0;
        return (size_t)(it - keys.begin()) - 1;
    }

    static glm::vec3 SampleVec3(const std::vector<AnimKey<glm::vec3>>& keys, float t, const glm::vec3& fallback) {
        if (keys.empty())
            return fallback;
        if (keys.size() == 1 || t <= keys.front().Time)
            return keys.front().Value;
        if (t >= keys.back().Time)
            return keys.back().Value;
        size_t i = FindKey(keys, t);
        const auto& a = keys[i];
        const auto& b = keys[i + 1];
        float span = b.Time - a.Time;
        float f = span > 1e-6f ? (t - a.Time) / span : 0.0f;
        return glm::mix(a.Value, b.Value, f);
    }

    static glm::quat SampleQuat(const std::vector<AnimKey<glm::quat>>& keys, float t, const glm::quat& fallback) {
        if (keys.empty())
            return fallback;
        if (keys.size() == 1 || t <= keys.front().Time)
            return keys.front().Value;
        if (t >= keys.back().Time)
            return keys.back().Value;
        size_t i = FindKey(keys, t);
        const auto& a = keys[i];
        const auto& b = keys[i + 1];
        float span = b.Time - a.Time;
        float f = span > 1e-6f ? (t - a.Time) / span : 0.0f;
        return glm::normalize(glm::slerp(a.Value, b.Value, f));
    }

    void ModelRig::SamplePose(int clip, float time, std::vector<NodeTRS>& out) const {
        out = BindTRS;
        if (clip < 0 || clip >= (int)Clips.size())
            return;
        const AnimClip& c = Clips[clip];
        time = std::clamp(time, 0.0f, std::max(c.Duration, 0.0f));
        for (const AnimChannel& ch : c.Channels) {
            if (ch.Node < 0 || ch.Node >= (int)out.size())
                continue;
            NodeTRS& trs = out[ch.Node];
            trs.Translation = SampleVec3(ch.Position, time, trs.Translation);
            trs.Rotation = SampleQuat(ch.Rotation, time, trs.Rotation);
            trs.Scale = SampleVec3(ch.Scale, time, trs.Scale);
        }
    }

    glm::vec3 ModelRig::SampleNodeTranslation(int clip, float time, int node) const {
        if (node < 0 || node >= (int)BindTRS.size())
            return glm::vec3(0.0f);
        glm::vec3 bind = BindTRS[node].Translation;
        if (clip < 0 || clip >= (int)Clips.size())
            return bind;
        const AnimClip& c = Clips[clip];
        time = std::clamp(time, 0.0f, std::max(c.Duration, 0.0f));
        for (const AnimChannel& ch : c.Channels)
            if (ch.Node == node)
                return SampleVec3(ch.Position, time, bind);
        return bind;
    }

    int ModelRig::FindRootBoneNode() const {
        int best = -1, bestDepth = 1 << 30;
        for (const RigBone& bone : Bones) {
            int depth = 0;
            for (int n = bone.Node; n >= 0 && n < (int)Nodes.size() && depth < 256; n = Nodes[n].Parent)
                depth++;
            if (depth < bestDepth) {
                bestDepth = depth;
                best = bone.Node;
            }
        }
        return best;
    }

    void ModelRig::BlendPoses(const std::vector<NodeTRS>& a, const std::vector<NodeTRS>& b, float w, std::vector<NodeTRS>& out) {
        const size_t count = std::min(a.size(), b.size());
        if (&out != &a && &out != &b)
            out.resize(count);
        w = std::clamp(w, 0.0f, 1.0f);
        for (size_t i = 0; i < count; i++) {
            NodeTRS r;
            r.Translation = glm::mix(a[i].Translation, b[i].Translation, w);
            // Caminho curto: se os quaternions apontam para lados opostos, inverte um.
            glm::quat qb = b[i].Rotation;
            if (glm::dot(a[i].Rotation, qb) < 0.0f)
                qb = -qb;
            r.Rotation = glm::normalize(glm::slerp(a[i].Rotation, qb, w));
            r.Scale = glm::mix(a[i].Scale, b[i].Scale, w);
            out[i] = r;
        }
    }

    glm::mat4 ModelRig::Compose(const NodeTRS& trs) {
        glm::mat4 m = glm::mat4_cast(trs.Rotation);
        m[0] *= trs.Scale.x;
        m[1] *= trs.Scale.y;
        m[2] *= trs.Scale.z;
        m[3] = glm::vec4(trs.Translation, 1.0f);
        return m;
    }

    NodeTRS ModelRig::Decompose(const glm::mat4& m) {
        NodeTRS trs;
        trs.Translation = glm::vec3(m[3]);
        glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
        glm::vec3 scale(glm::length(c0), glm::length(c1), glm::length(c2));
        // Matriz com determinante negativo (espelho): joga o sinal em X.
        if (glm::determinant(glm::mat3(m)) < 0.0f)
            scale.x = -scale.x;
        for (int i = 0; i < 3; i++)
            if (std::abs(scale[i]) < 1e-8f)
                scale[i] = 1e-8f;
        glm::mat3 rot(c0 / scale.x, c1 / scale.y, c2 / scale.z);
        trs.Rotation = glm::normalize(glm::quat_cast(rot));
        trs.Scale = scale;
        return trs;
    }

    void ModelRig::ComputePalette(const std::vector<NodeTRS>& pose, std::vector<glm::mat4>& palette) const {
        // Globais dos nos: pre-ordem (pai antes do filho).
        thread_local std::vector<glm::mat4> globals;
        globals.resize(Nodes.size());
        for (size_t i = 0; i < Nodes.size(); i++) {
            glm::mat4 local = i < pose.size() ? Compose(pose[i]) : Nodes[i].LocalBind;
            int parent = Nodes[i].Parent;
            globals[i] = (parent >= 0 && parent < (int)i) ? globals[parent] * local : local;
        }
        palette.resize(Bones.size());
        for (size_t b = 0; b < Bones.size(); b++) {
            int node = Bones[b].Node;
            palette[b] = (node >= 0 && node < (int)globals.size()) ? globals[node] * Bones[b].InverseBind : glm::mat4(1.0f);
        }
    }

}
