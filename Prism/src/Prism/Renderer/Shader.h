#pragma once

// ============================================================================
// Shader.h
// Wrapper simples de programa de shader GLSL (vertex + fragment). Sem cache
// de uniform locations (cada Set* consulta o local na hora) - e isso que
// torna seguro trocar o programa por baixo durante o hot reload.
//
// Duas formas de criar:
//   - Shader::Create(nome, srcVertex, srcFragment): codigo-fonte em memoria.
//   - Shader::CreateFromFiles(nome, .vert, .frag, defines): arquivos em disco
//     (ver ShaderSource.h: '#include' e defines). So esta forma suporta
//     ReloadIfChanged().
// ============================================================================

#include "../Core/Base.h"
#include "ShaderSource.h"
#include <filesystem>
#include <string>
#include <cstdint>
#include <vector>

namespace Prism {

    class Shader {
    public:
        // Constroi a partir do CODIGO FONTE em memoria (nao de um caminho de
        // arquivo). Para arquivos, use Shader::CreateFromFiles.
        Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        ~Shader();

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        void Bind() const;
        void Unbind() const;

        void SetMat4(const std::string& name, const float* matrix4x4) const;

        // 'matrix3x3' aponta para 9 floats em ordem column-major (o mesmo
        // layout de glm::mat3 / glm::value_ptr). Usado para a matriz normal
        // (ver u_NormalMatrix em Renderer.cpp).
        void SetMat3(const std::string& name, const float* matrix3x3) const;
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

        // Carrega de arquivos .vert/.frag (com '#include' e 'defines', ver
        // ShaderSource.h). Se a primeira compilacao falhar o Shader e
        // devolvido mesmo assim (IsValid() == false, erro no log) e continua
        // observando os arquivos: corrigir e salvar faz o proximo
        // ReloadIfChanged() compilar de novo.
        static Ref<Shader> CreateFromFiles(const std::string& name,
                                           const std::filesystem::path& vertexPath,
                                           const std::filesystem::path& fragmentPath,
                                           const ShaderDefines& defines = {});

        // Hot reload: se algum arquivo do shader (inclusive os incluidos)
        // mudou desde a ultima compilacao, recompila. Se a nova versao NAO
        // compilar, o programa anterior continua em uso (erro no log) - um
        // typo durante a edicao nunca derruba a viewport. Precisa de um
        // contexto OpenGL atual. Retorna true so se um programa novo foi
        // instalado. No-op para shaders criados de codigo em memoria.
        bool ReloadIfChanged();

        bool IsFileBacked() const { return !m_VertexPath.empty(); }
        bool IsValid() const { return m_RendererID != 0; }

    private:
        explicit Shader(const std::string& name); // usado por CreateFromFiles

        bool BuildFromFiles();

        static uint32_t BuildProgram(const std::string& shaderName,
                                     const std::string& vertexSrc, const std::string& fragmentSrc,
                                     const std::vector<std::filesystem::path>* vertexFiles,
                                     const std::vector<std::filesystem::path>* fragmentFiles);

        // 'files' (opcional) lista os arquivos do shader no log de erro, para
        // traduzir o "N:linha" do driver (N = indice do arquivo).
        static uint32_t CompileStage(uint32_t glStage, const std::string& source, const char* stageNameForLog,
                                     const std::string& shaderName, const std::vector<std::filesystem::path>* files);

    private:
        struct WatchedFile {
            std::filesystem::path Path;
            std::filesystem::file_time_type WriteTime{};
        };

        uint32_t m_RendererID = 0;
        std::string m_Name;

        // So para shaders de arquivo (CreateFromFiles).
        std::filesystem::path m_VertexPath;
        std::filesystem::path m_FragmentPath;
        ShaderDefines m_Defines;
        std::vector<WatchedFile> m_Watched;
    };

}
