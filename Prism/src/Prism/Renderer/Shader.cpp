#include <glad/gl.h>
#include "Shader.h"
#include "../Core/Log.h"

#include <vector>

namespace Prism {

    Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc) {
        return CreateRef<Shader>(name, vertexSrc, fragmentSrc);
    }

    Shader::Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
        : m_Name(name) {
        m_RendererID = BuildProgram(m_Name, vertexSrc, fragmentSrc, nullptr, nullptr);
    }

    Shader::Shader(const std::string& name)
        : m_Name(name) {}

    Shader::~Shader() {
        glDeleteProgram(m_RendererID); // 0 e ignorado pelo OpenGL
    }

    Ref<Shader> Shader::CreateFromFiles(const std::string& name,
                                        const std::filesystem::path& vertexPath,
                                        const std::filesystem::path& fragmentPath,
                                        const ShaderDefines& defines) {
        Ref<Shader> shader(new Shader(name)); // CreateRef nao enxerga o construtor privado
        shader->m_VertexPath = vertexPath;
        shader->m_FragmentPath = fragmentPath;
        shader->m_Defines = defines;
        shader->BuildFromFiles();
        return shader;
    }

    // Le os arquivos, compila e, SE deu certo, troca o programa atual. Sempre
    // atualiza a lista de arquivos observados (e as datas), mesmo em erro:
    // assim um shader quebrado nao e recompilado a cada poll, so quando
    // alguem salvar de novo.
    bool Shader::BuildFromFiles() {
        ShaderSourceResult vertex = ShaderSource::Load(m_VertexPath, m_Defines);
        ShaderSourceResult fragment = ShaderSource::Load(m_FragmentPath, m_Defines);

        m_Watched.clear();
        auto watch = [this](const std::vector<std::filesystem::path>& files) {
            for (const auto& file : files) {
                bool already = false;
                for (const auto& w : m_Watched)
                    if (w.Path == file) { already = true; break; }
                if (already)
                    continue;
                std::error_code ec;
                auto time = std::filesystem::last_write_time(file, ec);
                m_Watched.push_back({ file, ec ? std::filesystem::file_time_type{} : time });
            }
        };
        watch(vertex.Files);
        watch(fragment.Files);

        if (!vertex.Ok) {
            PRISM_CORE_ERROR("Shader '", m_Name, "' (vertex): ", vertex.Error);
            return false;
        }
        if (!fragment.Ok) {
            PRISM_CORE_ERROR("Shader '", m_Name, "' (fragment): ", fragment.Error);
            return false;
        }

        uint32_t program = BuildProgram(m_Name, vertex.Source, fragment.Source, &vertex.Files, &fragment.Files);
        if (program == 0)
            return false;

        if (m_RendererID != 0)
            glDeleteProgram(m_RendererID); // o driver adia a liberacao enquanto ainda em uso
        m_RendererID = program;
        return true;
    }

    bool Shader::ReloadIfChanged() {
        if (!IsFileBacked())
            return false;

        bool changed = false;
        for (const auto& w : m_Watched) {
            std::error_code ec;
            auto time = std::filesystem::last_write_time(w.Path, ec);
            // Arquivo momentaneamente ausente (editores que salvam via
            // temporario + rename): ignora, tenta no proximo poll.
            if (!ec && time != w.WriteTime) { changed = true; break; }
        }
        if (!changed)
            return false;

        PRISM_CORE_INFO("Recarregando shader '", m_Name, "'...");
        if (BuildFromFiles()) {
            PRISM_CORE_INFO("Shader '", m_Name, "' recarregado.");
            return true;
        }
        if (m_RendererID != 0)
            PRISM_CORE_WARN("Shader '", m_Name, "': recarga falhou, mantendo a versao anterior.");
        return false;
    }

    uint32_t Shader::BuildProgram(const std::string& shaderName,
                                  const std::string& vertexSrc, const std::string& fragmentSrc,
                                  const std::vector<std::filesystem::path>* vertexFiles,
                                  const std::vector<std::filesystem::path>* fragmentFiles) {
        uint32_t vertexShader = CompileStage(GL_VERTEX_SHADER, vertexSrc, "vertex", shaderName, vertexFiles);
        uint32_t fragmentShader = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc, "fragment", shaderName, fragmentFiles);
        if (vertexShader == 0 || fragmentShader == 0) {
            if (vertexShader) glDeleteShader(vertexShader);
            if (fragmentShader) glDeleteShader(fragmentShader);
            return 0;
        }

        uint32_t program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        int linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            int logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> log(logLength > 0 ? logLength : 1);
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            PRISM_CORE_ERROR("Falha ao linkar shader '", shaderName, "': ", log.data());
            glDeleteProgram(program);
            program = 0;
        }

        if (program != 0) {
            glDetachShader(program, vertexShader);
            glDetachShader(program, fragmentShader);
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return program;
    }

    uint32_t Shader::CompileStage(uint32_t glStage, const std::string& source, const char* stageNameForLog,
                                  const std::string& shaderName, const std::vector<std::filesystem::path>* files) {
        uint32_t shader = glCreateShader(glStage);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        int compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            int logLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> log(logLength > 0 ? logLength : 1);
            glGetShaderInfoLog(shader, logLength, nullptr, log.data());
            PRISM_CORE_ERROR("Falha ao compilar shader (", stageNameForLog, ") '", shaderName, "': ", log.data());
            if (files && files->size() > 1) { // com '#include', o "N" de "N:linha" e o indice do arquivo
                std::string legend;
                for (size_t i = 0; i < files->size(); i++)
                    legend += (i ? ", " : "") + std::to_string(i) + "=" + (*files)[i].generic_string();
                PRISM_CORE_ERROR("  arquivos do shader: ", legend);
            }
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void Shader::Bind() const {
        glUseProgram(m_RendererID);
    }

    void Shader::Unbind() const {
        glUseProgram(0);
    }

    void Shader::SetMat4(const std::string& name, const float* matrix4x4) const {
        int location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniformMatrix4fv(location, 1, GL_FALSE, matrix4x4);
    }

    void Shader::SetMat3(const std::string& name, const float* matrix3x3) const {
        int location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniformMatrix3fv(location, 1, GL_FALSE, matrix3x3);
    }

    void Shader::SetFloat3(const std::string& name, float x, float y, float z) const {
        int location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform3f(location, x, y, z);
    }

    void Shader::SetFloat2(const std::string& name, float x, float y) const {
        int location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform2f(location, x, y);
    }

    void Shader::SetFloat3Array(const std::string& name, const float* values, uint32_t count) const {
        int location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform3fv(location, (GLsizei)count, values);
    }

    void Shader::SetFloat(const std::string& name, float value) const {
        int location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform1f(location, value);
    }

    void Shader::SetInt(const std::string& name, int value) const {
        int location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform1i(location, value);
    }

}
