#pragma once

// ============================================================================
// Shader.h
// Wrapper simples de programa de shader GLSL (vertex + fragment). Nada
// sofisticado ainda (sem cache de uniform locations otimizado, sem hot
// reload) - so o suficiente para a engine conseguir desenhar geometria com
// uma matriz MVP, que e a base de qualquer coisa que a Renderer precisar
// desenhar dai pra frente.
// ============================================================================

#include "../Core/Base.h"
#include <string>
#include <cstdint>

namespace Prism {

    class Shader {
    public:
        // Constroi a partir do CODIGO FONTE em memoria (nao de um caminho de
        // arquivo). Um Shader::CreateFromFile pode ser adicionado depois
        // sem quebrar este construtor.
        Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        ~Shader();

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        void Bind() const;
        void Unbind() const;

        void SetMat4(const std::string& name, const float* matrix4x4) const;
        void SetFloat3(const std::string& name, float x, float y, float z) const;

        // 'x'/'y' - usado hoje so por u_ScreenSize (Renderer::DrawMesh) e
        // u_NoiseScale (Renderer::RenderSSAOPass).
        void SetFloat2(const std::string& name, float x, float y) const;

        void SetFloat(const std::string& name, float value) const;
        void SetInt(const std::string& name, int value) const;

        // Envia um array de vetores glm::vec3 contiguo como
        // 'uniform vec3 name[count]' no shader - usado hoje so por
        // u_Samples[16] (kernel de SSAO, ver Renderer::RenderSSAOPass).
        // 'values' deve apontar para 'count' glm::vec3 consecutivos (ex:
        // std::array<glm::vec3, N>::data()).
        void SetFloat3Array(const std::string& name, const float* values, uint32_t count) const;

        const std::string& GetName() const { return m_Name; }

        static Ref<Shader> Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);

    private:
        uint32_t CompileStage(uint32_t glStage, const std::string& source, const char* stageNameForLog);

    private:
        uint32_t m_RendererID = 0;
        std::string m_Name;
    };

}
