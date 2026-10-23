// glad sempre antes de qualquer header que toque OpenGL/GLFW.
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "ShadowSystem.h"
#include "../Core/Log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace Prism {

    namespace {
        constexpr uint32_t kCascadeResolution = 2048;
        constexpr uint32_t kSpotResolution = 1024;
        constexpr uint32_t kPointResolution = 1024;
        constexpr float kMaxShadowDistance = 120.0f; // alem disso nao ha sombra direcional
        constexpr float kCascadeLambda = 0.75f;       // 0 = divisao uniforme, 1 = logaritmica

        Ref<Shader> s_DepthShader;
        Ref<Shader> s_PointShader;

        uint32_t s_CascadeTexture = 0; // Texture2DArray, kShadowCascades camadas
        uint32_t s_SpotTexture = 0;    // Texture2DArray, kMaxSpotShadows camadas
        uint32_t s_PointTexture = 0;   // CubeMapArray, kMaxPointShadows luzes (6 camadas-face cada)

        std::vector<std::pair<GLFWwindow*, uint32_t>> s_Framebuffers; // um FBO por contexto GLFW
        ShadowFrame s_Frame;
        uint64_t s_Generation = 0;
        bool s_WarnedSpotLimit = false;
        bool s_WarnedPointLimit = false;
        bool s_WarnedIncomplete = false;

        uint32_t MakeDepthTexture(GLenum target, uint32_t resolution, uint32_t layers) {
            GLuint texture = 0;
            glCreateTextures(target, 1, &texture);
            glTextureStorage3D(texture, 1, GL_DEPTH_COMPONENT32F, (GLsizei)resolution, (GLsizei)resolution, (GLsizei)layers);
            // NEAREST: a comparacao e feita no shader (PCF manual), sem filtrar profundidade.
            glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            return texture;
        }

        uint32_t FramebufferForCurrentContext() {
            GLFWwindow* current = glfwGetCurrentContext();
            for (auto& [window, framebuffer] : s_Framebuffers) {
                if (window != current)
                    continue;
                if (glIsFramebuffer(framebuffer))
                    return framebuffer;
                glCreateFramebuffers(1, &framebuffer); // contexto reaproveitado: o nome antigo nao vale
                glNamedFramebufferDrawBuffer(framebuffer, GL_NONE);
                glNamedFramebufferReadBuffer(framebuffer, GL_NONE);
                return framebuffer;
            }
            GLuint framebuffer = 0;
            glCreateFramebuffers(1, &framebuffer);
            glNamedFramebufferDrawBuffer(framebuffer, GL_NONE); // so profundidade
            glNamedFramebufferReadBuffer(framebuffer, GL_NONE);
            s_Framebuffers.emplace_back(current, framebuffer);
            return framebuffer;
        }

        // Liga a camada 'layer' de 'texture' como alvo de profundidade e limpa.
        bool BeginLayer(uint32_t framebuffer, uint32_t texture, uint32_t layer, uint32_t resolution) {
            glNamedFramebufferTextureLayer(framebuffer, GL_DEPTH_ATTACHMENT, texture, 0, (GLint)layer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            if (glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                if (!s_WarnedIncomplete) {
                    s_WarnedIncomplete = true;
                    PRISM_CORE_ERROR("ShadowSystem: framebuffer de sombra incompleto; as sombras serao desligadas.");
                }
                return false;
            }
            glViewport(0, 0, (GLsizei)resolution, (GLsizei)resolution);
            glClear(GL_DEPTH_BUFFER_BIT);
            return true;
        }

        void DrawCasters(const std::vector<ShadowCaster>& casters, const Shader& shader) {
            for (const ShadowCaster& caster : casters) {
                if (!caster.Geometry)
                    continue;
                shader.SetMat4("u_Model", glm::value_ptr(caster.Model));
                caster.Geometry->BindForCurrentContext();
                glDrawElements(GL_TRIANGLES, (GLsizei)caster.Geometry->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
            }
            glBindVertexArray(0);
        }

        glm::vec3 SafeUp(const glm::vec3& direction) {
            return std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // Cascatas da luz direcional 'light' (ver ShadowSystem.h). false = falhou.
        bool RenderCascades(ShadowFrame& frame, const GPULight& light, const std::vector<ShadowCaster>& casters,
                            const float* view, const float* projection, float sceneRadius, uint32_t framebuffer) {
            const glm::mat4 V = glm::make_mat4(view);
            const glm::mat4 P = glm::make_mat4(projection);
            const glm::mat4 invViewProjection = glm::inverse(P * V);

            // near/far do frustum a partir da matriz de projecao (perspectiva ou ortografica).
            const bool ortho = std::abs(P[3][3] - 1.0f) < 1e-4f;
            float nearPlane, farPlane;
            if (ortho) {
                nearPlane = (P[3][2] + 1.0f) / P[2][2];
                farPlane = (P[3][2] - 1.0f) / P[2][2];
            } else {
                nearPlane = P[3][2] / (P[2][2] - 1.0f);
                farPlane = P[3][2] / (P[2][2] + 1.0f);
            }
            if (!std::isfinite(nearPlane) || !std::isfinite(farPlane))
                return false;
            nearPlane = std::max(nearPlane, 0.01f);
            if (!(farPlane > nearPlane))
                farPlane = nearPlane + 100.0f;
            const float shadowFar = std::min(farPlane, nearPlane + kMaxShadowDistance);

            glm::vec3 nearCorners[4], farCorners[4];
            const glm::vec2 xy[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
            for (int i = 0; i < 4; ++i) {
                glm::vec4 n = invViewProjection * glm::vec4(xy[i], -1.0f, 1.0f);
                glm::vec4 f = invViewProjection * glm::vec4(xy[i], 1.0f, 1.0f);
                nearCorners[i] = glm::vec3(n) / n.w;
                farCorners[i] = glm::vec3(f) / f.w;
            }

            // Direcao em que a luz VIAJA.
            const glm::vec3 direction = glm::normalize(light.Direction);
            const float extra = glm::clamp(sceneRadius, 10.0f, 500.0f); // casters atras da cascata ainda projetam sombra nela

            float previousSplit = nearPlane;
            for (int c = 0; c < kShadowCascades; ++c) {
                const float ratio = (float)(c + 1) / (float)kShadowCascades;
                const float logarithmic = nearPlane * std::pow(shadowFar / nearPlane, ratio);
                const float uniform = nearPlane + (shadowFar - nearPlane) * ratio;
                const float split = glm::mix(uniform, logarithmic, kCascadeLambda);

                const float tNear = (previousSplit - nearPlane) / (farPlane - nearPlane);
                const float tFar = (split - nearPlane) / (farPlane - nearPlane);
                glm::vec3 corners[8];
                for (int i = 0; i < 4; ++i) {
                    corners[i] = nearCorners[i] + (farCorners[i] - nearCorners[i]) * tNear;
                    corners[i + 4] = nearCorners[i] + (farCorners[i] - nearCorners[i]) * tFar;
                }
                glm::vec3 center(0.0f);
                for (const glm::vec3& corner : corners)
                    center += corner;
                center /= 8.0f;
                float radius = 0.0f;
                for (const glm::vec3& corner : corners)
                    radius = std::max(radius, glm::length(corner - center));
                radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 0.5f); // arredonda: o tamanho nao "respira" a cada quadro

                const glm::vec3 eye = center - direction * (radius + extra);
                const glm::mat4 lightView = glm::lookAt(eye, center, SafeUp(direction));
                const float zNear = 0.01f;
                const float zFar = radius * 2.0f + extra + 1.0f;
                glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, zNear, zFar);

                // Alinha a grade de texels: a sombra nao "nada" quando a camera se move.
                const glm::vec4 origin = lightProjection * lightView * glm::vec4(center, 1.0f);
                const float texelNdc = 2.0f / (float)kCascadeResolution;
                const float dx = std::round(origin.x / texelNdc) * texelNdc - origin.x;
                const float dy = std::round(origin.y / texelNdc) * texelNdc - origin.y;
                lightProjection = glm::translate(glm::mat4(1.0f), glm::vec3(dx, dy, 0.0f)) * lightProjection;

                frame.CascadeMatrix[c] = lightProjection * lightView;
                frame.CascadeSplit[c] = split;
                frame.CascadeTexel[c] = 2.0f * radius / (float)kCascadeResolution;
                frame.CascadeRange[c] = zFar - zNear;
                previousSplit = split;
            }
            frame.CascadeCount = kShadowCascades;

            if (!s_CascadeTexture)
                s_CascadeTexture = MakeDepthTexture(GL_TEXTURE_2D_ARRAY, kCascadeResolution, kShadowCascades);

            s_DepthShader->Bind();
            for (int c = 0; c < kShadowCascades; ++c) {
                if (!BeginLayer(framebuffer, s_CascadeTexture, (uint32_t)c, kCascadeResolution)) {
                    s_DepthShader->Unbind();
                    frame.CascadeCount = 0;
                    return false;
                }
                s_DepthShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(frame.CascadeMatrix[c]));
                DrawCasters(casters, *s_DepthShader);
            }
            s_DepthShader->Unbind();
            return true;
        }

        bool RenderSpot(ShadowFrame& frame, int slot, const GPULight& light, const std::vector<ShadowCaster>& casters, uint32_t framebuffer) {
            const float outerAngle = std::acos(glm::clamp(light.CosOuterAngle, -1.0f, 1.0f));
            const float fov = glm::clamp(2.0f * outerAngle + glm::radians(2.0f), glm::radians(10.0f), glm::radians(170.0f));
            const float nearPlane = 0.1f;
            const float farPlane = std::max(light.Range > 0.0f ? light.Range : 100.0f, nearPlane + 1.0f);
            const glm::vec3 direction = glm::normalize(light.Direction);

            const glm::mat4 lightView = glm::lookAt(light.Position, light.Position + direction, SafeUp(direction));
            frame.SpotMatrix[slot] = glm::perspective(fov, 1.0f, nearPlane, farPlane) * lightView;
            frame.SpotTexelAt1[slot] = 2.0f * std::tan(fov * 0.5f) / (float)kSpotResolution;
            frame.SpotNear[slot] = nearPlane;
            frame.SpotFar[slot] = farPlane;

            if (!s_SpotTexture)
                s_SpotTexture = MakeDepthTexture(GL_TEXTURE_2D_ARRAY, kSpotResolution, kMaxSpotShadows);

            if (!BeginLayer(framebuffer, s_SpotTexture, (uint32_t)slot, kSpotResolution))
                return false;
            s_DepthShader->Bind();
            s_DepthShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(frame.SpotMatrix[slot]));
            DrawCasters(casters, *s_DepthShader);
            s_DepthShader->Unbind();
            return true;
        }

        bool RenderPoint(ShadowFrame& frame, int slot, const GPULight& light, const std::vector<ShadowCaster>& casters, uint32_t framebuffer) {
            const float nearPlane = 0.05f;
            const float farPlane = std::max(light.Range > 0.0f ? light.Range : 50.0f, nearPlane + 1.0f);
            frame.PointFar[slot] = farPlane;

            if (!s_PointTexture)
                s_PointTexture = MakeDepthTexture(GL_TEXTURE_CUBE_MAP_ARRAY, kPointResolution, kMaxPointShadows * 6);

            // Ordem das faces do cubemap: +X, -X, +Y, -Y, +Z, -Z (com os "ups" da convencao do GL).
            const glm::vec3 directions[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            const glm::vec3 ups[6] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
            const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

            s_PointShader->Bind();
            s_PointShader->SetFloat3("u_LightPos", light.Position.x, light.Position.y, light.Position.z);
            s_PointShader->SetFloat("u_Far", farPlane);
            for (int face = 0; face < 6; ++face) {
                if (!BeginLayer(framebuffer, s_PointTexture, (uint32_t)(slot * 6 + face), kPointResolution)) {
                    s_PointShader->Unbind();
                    return false;
                }
                const glm::mat4 matrix = projection * glm::lookAt(light.Position, light.Position + directions[face], ups[face]);
                s_PointShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(matrix));
                DrawCasters(casters, *s_PointShader);
            }
            s_PointShader->Unbind();
            return true;
        }
    }

    bool ShadowSystem::Init(const std::filesystem::path& shaderDir) {
        s_DepthShader = Shader::CreateFromFiles("ShadowDepth", shaderDir / "shadow_depth.vert", shaderDir / "shadow_depth.frag");
        s_PointShader = Shader::CreateFromFiles("ShadowPoint", shaderDir / "shadow_point.vert", shaderDir / "shadow_point.frag");
        return s_DepthShader && s_PointShader && s_DepthShader->IsValid() && s_PointShader->IsValid();
    }

    void ShadowSystem::Shutdown() {
        s_DepthShader.reset();
        s_PointShader.reset();
        for (auto& [window, framebuffer] : s_Framebuffers)
            glDeleteFramebuffers(1, &framebuffer);
        s_Framebuffers.clear();
        if (s_CascadeTexture) { glDeleteTextures(1, &s_CascadeTexture); s_CascadeTexture = 0; }
        if (s_SpotTexture) { glDeleteTextures(1, &s_SpotTexture); s_SpotTexture = 0; }
        if (s_PointTexture) { glDeleteTextures(1, &s_PointTexture); s_PointTexture = 0; }
        s_Frame = ShadowFrame{};
    }

    bool ShadowSystem::ReloadChangedShaders() {
        bool any = false;
        if (s_DepthShader) any |= s_DepthShader->ReloadIfChanged();
        if (s_PointShader) any |= s_PointShader->ReloadIfChanged();
        return any;
    }

    const ShadowFrame& ShadowSystem::Render(const std::vector<ShadowCaster>& casters, std::vector<GPULight>& lights,
                                            const float* view, const float* projection) {
        s_Frame = ShadowFrame{};
        s_Frame.Generation = ++s_Generation;
        for (GPULight& light : lights)
            light.ShadowSlot = -1;

        // Camera: para onde ela olha (eixo -Z da view), usado para escolher a cascata.
        const glm::mat4 V = glm::make_mat4(view);
        s_Frame.CameraForward = -glm::vec3(V[0][2], V[1][2], V[2][2]);

        if (!s_DepthShader || !s_PointShader || !s_DepthShader->IsValid() || !s_PointShader->IsValid() || casters.empty())
            return s_Frame;

        // Quem tem sombra neste quadro (limite por tipo; o resto fica sem, com aviso unico).
        int directionalIndex = -1;
        std::vector<int> spotLights, pointLights;
        for (int i = 0; i < (int)lights.size(); ++i) {
            const GPULight& light = lights[i];
            if (!light.CastShadows)
                continue;
            if (light.Type == 2) {          // Directional
                if (directionalIndex < 0)
                    directionalIndex = i;
            } else if (light.Type == 1) {   // Spot
                if ((int)spotLights.size() < kMaxSpotShadows) spotLights.push_back(i);
                else if (!s_WarnedSpotLimit) { s_WarnedSpotLimit = true; PRISM_CORE_WARN("Sombras: no maximo ", kMaxSpotShadows, " luzes Spot projetam sombra por vez; as demais ficam sem."); }
            } else if (light.Type == 0) {   // Point
                if ((int)pointLights.size() < kMaxPointShadows) pointLights.push_back(i);
                else if (!s_WarnedPointLimit) { s_WarnedPointLimit = true; PRISM_CORE_WARN("Sombras: no maximo ", kMaxPointShadows, " luzes Point projetam sombra por vez; as demais ficam sem."); }
            }
        }
        if (directionalIndex < 0 && spotLights.empty() && pointLights.empty())
            return s_Frame;

        // Raio da cena (so pelas posicoes dos casters): folga para os casters
        // que ficam atras da cascata. Mesmo criterio simples de sempre.
        glm::vec3 sceneMin(std::numeric_limits<float>::max()), sceneMax(std::numeric_limits<float>::lowest());
        for (const ShadowCaster& caster : casters) {
            const glm::vec3 position(caster.Model[3]);
            sceneMin = glm::min(sceneMin, position);
            sceneMax = glm::max(sceneMax, position);
        }
        const float sceneRadius = glm::length(sceneMax - sceneMin) * 0.5f + 2.0f;

        // Estado do GL: guarda e restaura (o chamador continua o quadro dele).
        GLint previousFramebuffer = 0, previousViewport[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
        const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);

        const uint32_t framebuffer = FramebufferForCurrentContext();

        if (directionalIndex >= 0) {
            if (RenderCascades(s_Frame, lights[directionalIndex], casters, view, projection, sceneRadius, framebuffer))
                lights[directionalIndex].ShadowSlot = 0;
        }
        for (int slot = 0; slot < (int)spotLights.size(); ++slot) {
            if (RenderSpot(s_Frame, slot, lights[spotLights[slot]], casters, framebuffer)) {
                lights[spotLights[slot]].ShadowSlot = slot;
                s_Frame.SpotCount = slot + 1;
            }
        }
        for (int slot = 0; slot < (int)pointLights.size(); ++slot) {
            if (RenderPoint(s_Frame, slot, lights[pointLights[slot]], casters, framebuffer)) {
                lights[pointLights[slot]].ShadowSlot = slot;
                s_Frame.PointCount = slot + 1;
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (blendWasEnabled) glEnable(GL_BLEND);
        if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
        return s_Frame;
    }

    void ShadowSystem::Upload(const ShadowFrame& frame, const Shader& shader) {
        shader.SetInt("u_CascadeShadow", (int)kUnitCascadeShadow);
        shader.SetInt("u_SpotShadow", (int)kUnitSpotShadow);
        shader.SetInt("u_PointShadow", (int)kUnitPointShadow);

        shader.SetInt("u_CascadeCount", frame.CascadeCount);
        for (int c = 0; c < kShadowCascades && c < frame.CascadeCount; ++c) {
            const std::string index = "[" + std::to_string(c) + "]";
            shader.SetMat4("u_CascadeMatrix" + index, glm::value_ptr(frame.CascadeMatrix[c]));
            shader.SetFloat("u_CascadeSplit" + index, frame.CascadeSplit[c]);
            shader.SetFloat("u_CascadeTexel" + index, frame.CascadeTexel[c]);
            shader.SetFloat("u_CascadeRange" + index, frame.CascadeRange[c]);
        }
        shader.SetFloat3("u_CameraForward", frame.CameraForward.x, frame.CameraForward.y, frame.CameraForward.z);

        shader.SetInt("u_SpotCount", frame.SpotCount);
        for (int s = 0; s < frame.SpotCount; ++s) {
            const std::string index = "[" + std::to_string(s) + "]";
            shader.SetMat4("u_SpotMatrix" + index, glm::value_ptr(frame.SpotMatrix[s]));
            shader.SetFloat("u_SpotTexelAt1" + index, frame.SpotTexelAt1[s]);
            shader.SetFloat("u_SpotNear" + index, frame.SpotNear[s]);
            shader.SetFloat("u_SpotFar" + index, frame.SpotFar[s]);
        }

        shader.SetInt("u_PointCount", frame.PointCount);
        for (int p = 0; p < frame.PointCount; ++p)
            shader.SetFloat("u_PointFar[" + std::to_string(p) + "]", frame.PointFar[p]);

        glBindTextureUnit(kUnitCascadeShadow, s_CascadeTexture);
        glBindTextureUnit(kUnitSpotShadow, s_SpotTexture);
        glBindTextureUnit(kUnitPointShadow, s_PointTexture);
    }

    void ShadowSystem::UploadNone(const Shader& shader) {
        shader.SetInt("u_CascadeShadow", (int)kUnitCascadeShadow);
        shader.SetInt("u_SpotShadow", (int)kUnitSpotShadow);
        shader.SetInt("u_PointShadow", (int)kUnitPointShadow);
        shader.SetInt("u_CascadeCount", 0);
        shader.SetInt("u_SpotCount", 0);
        shader.SetInt("u_PointCount", 0);
    }

}
