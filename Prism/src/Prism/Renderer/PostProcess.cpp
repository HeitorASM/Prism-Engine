// glad sempre antes de qualquer header que toque OpenGL/GLFW.
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "PostProcess.h"
#include "../Core/Log.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace Prism {

    namespace {
        Ref<Shader> s_BloomDown;
        Ref<Shader> s_BloomUp;
        Ref<Shader> s_Composite;
        Ref<Shader> s_Fxaa;

        // Uma instancia por contexto GLFW (FBOs nao sao compartilhados).
        std::vector<std::pair<GLFWwindow*, std::unique_ptr<PostProcess>>>& Instances() {
            static std::vector<std::pair<GLFWwindow*, std::unique_ptr<PostProcess>>> instances;
            return instances;
        }

        uint32_t MakeTexture(GLenum internalFormat, uint32_t width, uint32_t height, bool linear) {
            GLuint texture = 0;
            glCreateTextures(GL_TEXTURE_2D, 1, &texture);
            glTextureStorage2D(texture, 1, internalFormat, (GLsizei)width, (GLsizei)height);
            glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return texture;
        }

        uint32_t MakeFramebuffer(uint32_t color, uint32_t depthStencil) {
            GLuint framebuffer = 0;
            glCreateFramebuffers(1, &framebuffer);
            glNamedFramebufferTexture(framebuffer, GL_COLOR_ATTACHMENT0, color, 0);
            if (depthStencil)
                glNamedFramebufferTexture(framebuffer, GL_DEPTH_STENCIL_ATTACHMENT, depthStencil, 0);
            if (glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                glDeleteFramebuffers(1, &framebuffer);
                return 0;
            }
            return framebuffer;
        }
    }

    // --- Estaticos ---------------------------------------------------------

    bool PostProcess::Init(const std::filesystem::path& shaderDir) {
        auto load = [&](const char* name, const char* fragment) {
            return Shader::CreateFromFiles(name, shaderDir / "fullscreen_quad.vert", shaderDir / fragment, {});
        };
        s_BloomDown = load("BloomDown", "post_bloom_down.frag");
        s_BloomUp = load("BloomUp", "post_bloom_up.frag");
        s_Composite = load("PostComposite", "post_composite.frag");
        s_Fxaa = load("PostFXAA", "post_fxaa.frag");
        return s_BloomDown && s_BloomUp && s_Composite && s_Fxaa && s_BloomDown->IsValid() && s_BloomUp->IsValid() && s_Composite->IsValid() && s_Fxaa->IsValid();
    }

    void PostProcess::Shutdown() {
        Instances().clear(); // destrutores liberam texturas/FBOs (se o contexto ainda existir)
        s_BloomDown.reset();
        s_BloomUp.reset();
        s_Composite.reset();
        s_Fxaa.reset();
    }

    void PostProcess::ReloadChangedShaders() {
        for (Ref<Shader>* shader : { &s_BloomDown, &s_BloomUp, &s_Composite, &s_Fxaa })
            if (*shader)
                (*shader)->ReloadIfChanged();
    }

    PostProcess& PostProcess::ForCurrentContext() {
        GLFWwindow* current = glfwGetCurrentContext();
        for (auto& [window, instance] : Instances()) {
            if (window == current)
                return *instance;
        }
        Instances().emplace_back(current, std::unique_ptr<PostProcess>(new PostProcess()));
        return *Instances().back().second;
    }

    PostProcess::~PostProcess() {
        for (Targets& targets : m_Sets)
            DestroyTargets(targets);
        if (m_Vao) glDeleteVertexArrays(1, &m_Vao);
    }

    // --- Alvos ---------------------------------------------------------------

    void PostProcess::DestroyBloomLevels(Targets& t) {
        for (Level& level : t.BloomLevels) {
            if (level.Framebuffer) glDeleteFramebuffers(1, &level.Framebuffer);
            if (level.Texture) glDeleteTextures(1, &level.Texture);
        }
        t.BloomLevels.clear();
    }

    void PostProcess::DestroyTargets(Targets& t) {
        DestroyBloomLevels(t);
        if (t.LdrFramebuffer) glDeleteFramebuffers(1, &t.LdrFramebuffer);
        if (t.LdrColor) glDeleteTextures(1, &t.LdrColor);
        if (t.HdrFramebuffer) glDeleteFramebuffers(1, &t.HdrFramebuffer);
        if (t.HdrColor) glDeleteTextures(1, &t.HdrColor);
        if (t.HdrDepth) glDeleteTextures(1, &t.HdrDepth);
        t.LdrFramebuffer = t.LdrColor = t.HdrFramebuffer = t.HdrColor = t.HdrDepth = 0;
        t.Width = t.Height = 0;
    }

    bool PostProcess::CreateTargets(Targets& t, uint32_t width, uint32_t height) {
        DestroyTargets(t);

        t.HdrColor = MakeTexture(GL_RGBA16F, width, height, false);
        t.HdrDepth = MakeTexture(GL_DEPTH24_STENCIL8, width, height, false); // formato do Framebuffer do editor: permite copiar a profundidade
        t.HdrFramebuffer = MakeFramebuffer(t.HdrColor, t.HdrDepth);
        if (!t.HdrFramebuffer) {
            PRISM_CORE_ERROR("PostProcess: framebuffer HDR incompleto (", width, "x", height, "); o pos-processamento sera ignorado.");
            DestroyTargets(t);
            return false;
        }
        t.Width = width;
        t.Height = height;
        return true;
    }

    bool PostProcess::EnsureLdrTarget(Targets& t) {
        if (t.LdrFramebuffer)
            return true;
        t.LdrColor = MakeTexture(GL_RGBA8, t.Width, t.Height, true);
        t.LdrFramebuffer = MakeFramebuffer(t.LdrColor, 0);
        if (!t.LdrFramebuffer) {
            if (t.LdrColor) glDeleteTextures(1, &t.LdrColor);
            t.LdrColor = 0;
            return false;
        }
        return true;
    }

    bool PostProcess::EnsureBloomLevels(Targets& t) {
        if (!t.BloomLevels.empty())
            return true;

        uint32_t w = t.Width, h = t.Height;
        for (int i = 0; i < 6; ++i) {
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
            if (i > 0 && (w < 4 || h < 4))
                break;
            Level level;
            level.Width = w;
            level.Height = h;
            level.Texture = MakeTexture(GL_RGBA16F, w, h, true);
            level.Framebuffer = MakeFramebuffer(level.Texture, 0);
            if (!level.Framebuffer) {
                if (level.Texture) glDeleteTextures(1, &level.Texture);
                break;
            }
            t.BloomLevels.push_back(level);
        }
        if (t.BloomLevels.size() < 2) {
            DestroyBloomLevels(t);
            return false;
        }
        return true;
    }

    bool PostProcess::Begin(uint32_t width, uint32_t height) {
        auto usable = [](const Ref<Shader>& shader) { return shader && shader->IsValid(); };
        if (!usable(s_BloomDown) || !usable(s_BloomUp) || !usable(s_Composite) || !usable(s_Fxaa) || width == 0 || height == 0 || width > 8192 || height > 8192)
            return false;

        // Contexto reaproveitado (ponteiro GLFW igual de uma janela antiga):
        // os nomes dos FBOs nao valem neste contexto - esquece tudo.
        if (!m_Sets.empty() && m_Sets[0].HdrFramebuffer && !glIsFramebuffer(m_Sets[0].HdrFramebuffer)) {
            m_Sets.clear();
            m_Vao = 0;
        }

        if (!m_Vao)
            glCreateVertexArrays(1, &m_Vao);

        ++m_Tick;

        // Conjunto do tamanho pedido (um por tamanho, ate kMaxTargetSets).
        size_t found = m_Sets.size();
        for (size_t i = 0; i < m_Sets.size(); ++i) {
            if (m_Sets[i].Width == width && m_Sets[i].Height == height && m_Sets[i].HdrFramebuffer) {
                found = i;
                break;
            }
        }
        if (found == m_Sets.size()) {
            if (m_Sets.size() < kMaxTargetSets) {
                m_Sets.emplace_back();
            } else {
                // Substitui o menos usado recentemente (ex: o tamanho de um
                // painel que foi redimensionado e nao e mais pedido).
                found = 0;
                for (size_t i = 1; i < m_Sets.size(); ++i)
                    if (m_Sets[i].LastUsed < m_Sets[found].LastUsed)
                        found = i;
            }
            if (found == m_Sets.size())
                found = m_Sets.size() - 1;
            if (!CreateTargets(m_Sets[found], width, height))
                return false;
        }

        m_Current = found;
        m_Sets[found].LastUsed = m_Tick;

        glBindFramebuffer(GL_FRAMEBUFFER, m_Sets[found].HdrFramebuffer);
        glViewport(0, 0, (GLsizei)width, (GLsizei)height);
        return true;
    }

    // --- Quadro --------------------------------------------------------------

    void PostProcess::DrawTriangle() const {
        glBindVertexArray(m_Vao);
        glDrawArrays(GL_TRIANGLES, 0, 3); // "big triangle", ver shaders/fullscreen_quad.vert
        glBindVertexArray(0);
    }

    void PostProcess::RunBloom(const Targets& t, const PostProcessSettings& settings) {
        const size_t count = t.BloomLevels.size();

        // Descida: HDR -> nivel 0 (com limiar) -> ... -> nivel N-1.
        s_BloomDown->Bind();
        s_BloomDown->SetInt("u_Source", 0);
        s_BloomDown->SetFloat("u_Threshold", settings.BloomThreshold);
        for (size_t i = 0; i < count; ++i) {
            const Level& level = t.BloomLevels[i];
            glBindFramebuffer(GL_FRAMEBUFFER, level.Framebuffer);
            glViewport(0, 0, (GLsizei)level.Width, (GLsizei)level.Height);
            glBindTextureUnit(0, i == 0 ? t.HdrColor : t.BloomLevels[i - 1].Texture);
            s_BloomDown->SetInt("u_Prefilter", i == 0 ? 1 : 0);
            DrawTriangle();
        }
        s_BloomDown->Unbind();

        // Subida: cada nivel menor e SOMADO ao maior; o nivel 0 acumula tudo.
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        s_BloomUp->Bind();
        s_BloomUp->SetInt("u_Source", 0);
        for (size_t i = count - 1; i > 0; --i) {
            const Level& target = t.BloomLevels[i - 1];
            glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
            glViewport(0, 0, (GLsizei)target.Width, (GLsizei)target.Height);
            glBindTextureUnit(0, t.BloomLevels[i].Texture);
            DrawTriangle();
        }
        s_BloomUp->Unbind();
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // padrao do resto do Renderer (OpenGLContext::Init)
    }

    void PostProcess::End(const PostProcessSettings& settings, uint32_t targetFramebuffer, const int viewport[4]) {
        Targets& t = m_Sets[m_Current];

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        const bool bloom = settings.Bloom && settings.BloomIntensity > 0.0f && EnsureBloomLevels(t);
        if (bloom)
            RunBloom(t, settings);

        const bool fxaa = settings.FXAA && EnsureLdrTarget(t);

        // Composicao: direto no destino, ou no alvo LDR quando o FXAA vem depois.
        if (fxaa) {
            glBindFramebuffer(GL_FRAMEBUFFER, t.LdrFramebuffer);
            glViewport(0, 0, (GLsizei)t.Width, (GLsizei)t.Height);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        }

        s_Composite->Bind();
        glBindTextureUnit(0, t.HdrColor);
        glBindTextureUnit(1, bloom ? t.BloomLevels[0].Texture : t.HdrColor); // unidade 1 nunca fica vazia
        glBindTextureUnit(2, t.HdrDepth);
        s_Composite->SetInt("u_Scene", 0);
        s_Composite->SetInt("u_Bloom", 1);
        s_Composite->SetInt("u_Depth", 2);
        s_Composite->SetInt("u_BloomEnabled", bloom ? 1 : 0);
        s_Composite->SetFloat("u_BloomIntensity", settings.BloomIntensity);
        s_Composite->SetFloat("u_Exposure", settings.Exposure);
        s_Composite->SetFloat("u_Saturation", settings.Saturation);
        s_Composite->SetFloat("u_Contrast", settings.Contrast);
        s_Composite->SetInt("u_SkyPassThrough", settings.SkyPassThrough ? 1 : 0);
        DrawTriangle();
        s_Composite->Unbind();

        if (fxaa) {
            glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            s_Fxaa->Bind();
            glBindTextureUnit(0, t.LdrColor);
            s_Fxaa->SetInt("u_Source", 0);
            DrawTriangle();
            s_Fxaa->Unbind();
        }

        for (GLuint unit = 0; unit < 3; ++unit)
            glBindTextureUnit(unit, 0);

        // Profundidade da cena para o destino: gizmos/linhas desenhados depois
        // continuam ocluidos. (Blit ignora as mascaras de escrita; formatos
        // iguais no FBO do editor - D24S8.)
        glDisable(GL_SCISSOR_TEST);
        glBlitNamedFramebuffer(t.HdrFramebuffer, targetFramebuffer,
            0, 0, (GLint)t.Width, (GLint)t.Height,
            viewport[0], viewport[1], viewport[0] + (GLint)t.Width, viewport[1] + (GLint)t.Height,
            GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
    }

}
