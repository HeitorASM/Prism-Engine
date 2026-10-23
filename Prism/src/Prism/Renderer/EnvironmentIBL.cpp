// glad sempre antes de qualquer header que toque OpenGL/GLFW.
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "EnvironmentIBL.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cmath>

namespace Prism {

    namespace {
        constexpr uint32_t kEnvResolution = 256;
        constexpr uint32_t kIrradianceResolution = 32;
        constexpr uint32_t kPrefilterResolution = 128;

        Ref<Shader> s_EquirectShader;
        Ref<Shader> s_IrradianceShader;
        Ref<Shader> s_PrefilterShader;

        uint32_t s_EnvCube = 0;
        uint32_t s_IrradianceCube = 0;
        uint32_t s_PrefilterCube = 0;
        std::string s_BakedKey;
        bool s_Ready = false;

        uint32_t MakeCube(uint32_t resolution, int levels) {
            GLuint texture = 0;
            glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &texture);
            glTextureStorage2D(texture, levels, GL_RGBA16F, (GLsizei)resolution, (GLsizei)resolution);
            glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            return texture;
        }

        void DestroyCubes() {
            if (s_EnvCube) { glDeleteTextures(1, &s_EnvCube); s_EnvCube = 0; }
            if (s_IrradianceCube) { glDeleteTextures(1, &s_IrradianceCube); s_IrradianceCube = 0; }
            if (s_PrefilterCube) { glDeleteTextures(1, &s_PrefilterCube); s_PrefilterCube = 0; }
        }

        // Desenha as 6 faces do nivel 'level' de 'cube' com o shader ja configurado.
        bool DrawFaces(uint32_t framebuffer, uint32_t cube, int level, uint32_t resolution, const Shader& shader) {
            glViewport(0, 0, (GLsizei)resolution, (GLsizei)resolution);
            for (int face = 0; face < 6; ++face) {
                glNamedFramebufferTextureLayer(framebuffer, GL_COLOR_ATTACHMENT0, cube, level, face);
                if (glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    return false;
                shader.SetInt("u_Face", face);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            return true;
        }
    }

    bool EnvironmentIBL::Init(const std::filesystem::path& shaderDir) {
        s_EquirectShader = Shader::CreateFromFiles("IBLEquirect", shaderDir / "ibl.vert", shaderDir / "ibl_equirect.frag");
        s_IrradianceShader = Shader::CreateFromFiles("IBLIrradiance", shaderDir / "ibl.vert", shaderDir / "ibl_irradiance.frag");
        s_PrefilterShader = Shader::CreateFromFiles("IBLPrefilter", shaderDir / "ibl.vert", shaderDir / "ibl_prefilter.frag");
        return s_EquirectShader && s_IrradianceShader && s_PrefilterShader
            && s_EquirectShader->IsValid() && s_IrradianceShader->IsValid() && s_PrefilterShader->IsValid();
    }

    void EnvironmentIBL::Shutdown() {
        s_EquirectShader.reset();
        s_IrradianceShader.reset();
        s_PrefilterShader.reset();
        Invalidate();
    }

    bool EnvironmentIBL::ReloadChangedShaders() {
        bool any = false;
        if (s_EquirectShader) any |= s_EquirectShader->ReloadIfChanged();
        if (s_IrradianceShader) any |= s_IrradianceShader->ReloadIfChanged();
        if (s_PrefilterShader) any |= s_PrefilterShader->ReloadIfChanged();
        if (any)
            s_BakedKey.clear(); // refaz com os shaders novos
        return any;
    }

    void EnvironmentIBL::Invalidate() {
        DestroyCubes();
        s_BakedKey.clear();
        s_Ready = false;
    }

    bool EnvironmentIBL::Update(const SkyTexture& panorama, const std::string& key) {
        if (s_Ready && key == s_BakedKey)
            return true;
        if (key == s_BakedKey)
            return false; // ja tentou e falhou: nao repete todo quadro
        s_BakedKey = key;
        s_Ready = false;

        if (!panorama.IsValid() || !s_EquirectShader || !s_IrradianceShader || !s_PrefilterShader
            || !s_EquirectShader->IsValid() || !s_IrradianceShader->IsValid() || !s_PrefilterShader->IsValid())
            return false;

        // Estado do GL: guarda e restaura (o bake roda no meio de DrawScene).
        GLint previousFramebuffer = 0, previousViewport[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean blendWas = glIsEnabled(GL_BLEND);
        const GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
        const GLboolean scissorWas = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        DestroyCubes();
        const int envLevels = 1 + (int)std::floor(std::log2((double)kEnvResolution));
        s_EnvCube = MakeCube(kEnvResolution, envLevels);
        s_IrradianceCube = MakeCube(kIrradianceResolution, 1);
        s_PrefilterCube = MakeCube(kPrefilterResolution, kPrefilterLevels);

        GLuint framebuffer = 0, vao = 0;
        glCreateFramebuffers(1, &framebuffer);
        glNamedFramebufferDrawBuffer(framebuffer, GL_COLOR_ATTACHMENT0);
        glCreateVertexArrays(1, &vao); // VAO vazio: o triangulo nasce no shader
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glBindVertexArray(vao);

        bool ok = true;

        // 1) panorama -> cubemap de ambiente
        s_EquirectShader->Bind();
        s_EquirectShader->SetInt("u_Panorama", 0);
        panorama.Bind(0);
        ok = ok && DrawFaces(framebuffer, s_EnvCube, 0, kEnvResolution, *s_EquirectShader);
        s_EquirectShader->Unbind();
        glGenerateTextureMipmap(s_EnvCube);

        // 2) irradiancia difusa
        glBindTextureUnit(0, s_EnvCube);
        s_IrradianceShader->Bind();
        s_IrradianceShader->SetInt("u_Env", 0);
        ok = ok && DrawFaces(framebuffer, s_IrradianceCube, 0, kIrradianceResolution, *s_IrradianceShader);
        s_IrradianceShader->Unbind();

        // 3) especular pre-filtrado: um mip por rugosidade
        s_PrefilterShader->Bind();
        s_PrefilterShader->SetInt("u_Env", 0);
        s_PrefilterShader->SetFloat("u_EnvResolution", (float)kEnvResolution);
        for (int level = 0; ok && level < kPrefilterLevels; ++level) {
            const uint32_t resolution = std::max<uint32_t>(1u, kPrefilterResolution >> level);
            s_PrefilterShader->SetFloat("u_Roughness", (float)level / (float)(kPrefilterLevels - 1));
            ok = DrawFaces(framebuffer, s_PrefilterCube, level, resolution, *s_PrefilterShader);
        }
        s_PrefilterShader->Unbind();

        glBindTextureUnit(0, 0);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        glDeleteVertexArrays(1, &vao);
        glDeleteFramebuffers(1, &framebuffer);
        if (depthWas) glEnable(GL_DEPTH_TEST);
        if (blendWas) glEnable(GL_BLEND);
        if (cullWas) glEnable(GL_CULL_FACE);
        if (scissorWas) glEnable(GL_SCISSOR_TEST);

        if (!ok) {
            PRISM_CORE_ERROR("EnvironmentIBL: falha ao gerar os mapas de ambiente; usando o gradiente analitico.");
            DestroyCubes();
            return false;
        }
        s_Ready = true;
        PRISM_CORE_INFO("EnvironmentIBL: ambiente gerado a partir de ", key);
        return true;
    }

    void EnvironmentIBL::Upload(const Shader& shader, bool active, float scale, float rotationRadians) {
        // Os samplers de cubemap SEMPRE apontam para units proprios: dois tipos de
        // sampler no mesmo unit invalidam o desenho.
        shader.SetInt("u_IrradianceMap", (int)kUnitIrradiance);
        shader.SetInt("u_PrefilterMap", (int)kUnitPrefilter);
        const bool use = active && s_Ready && s_IrradianceCube && s_PrefilterCube;
        shader.SetInt("u_UseIBL", use ? 1 : 0);
        if (!use)
            return;
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); // estado por contexto
        glBindTextureUnit(kUnitIrradiance, s_IrradianceCube);
        glBindTextureUnit(kUnitPrefilter, s_PrefilterCube);
        shader.SetFloat("u_IBLScale", scale);
        shader.SetFloat("u_IBLRotation", rotationRadians);
        shader.SetFloat("u_IBLMaxLod", (float)(kPrefilterLevels - 1));
    }

}
