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
        uint32_t vertexShader = CompileStage(GL_VERTEX_SHADER, vertexSrc, "vertex");
        uint32_t fragmentShader = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc, "fragment");

        m_RendererID = glCreateProgram();
        glAttachShader(m_RendererID, vertexShader);
        glAttachShader(m_RendererID, fragmentShader);
        glLinkProgram(m_RendererID);

        int linked = 0;
        glGetProgramiv(m_RendererID, GL_LINK_STATUS, &linked);
        if (!linked) {
            int logLength = 0;
            glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> log(logLength > 0 ? logLength : 1);
            glGetProgramInfoLog(m_RendererID, logLength, nullptr, log.data());
            PRISM_CORE_ERROR("Falha ao linkar shader '", m_Name, "': ", log.data());
            glDeleteProgram(m_RendererID);
            m_RendererID = 0;
        }

        // Os estagios individuais ja foram linkados no programa; nao
        // precisamos mais deles soltos.
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    Shader::~Shader() {
        glDeleteProgram(m_RendererID);
    }

    uint32_t Shader::CompileStage(uint32_t glStage, const std::string& source, const char* stageNameForLog) {
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
            PRISM_CORE_ERROR("Falha ao compilar shader (", stageNameForLog, ") '", m_Name, "': ", log.data());
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
