#include <glad/gl.h>
#include <GLFW/glfw3.h> 
#include "Renderer.h"
#include "PrimitiveMeshFactory.h"
#include "../Core/Log.h"
#include "../Core/Profiler.h"
#include "GpuTimer.h"
#include "DrawBatching.h"
#include "LightSelection.h"
#include "MaterialState.h"
#include "../Core/Paths.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Particles/ParticleSystem.h"
#include "../UI/UISystem.h"
#include "DrawContext.h"
#include "../Assets/ModelLoader.h"
#include "../Animation/AnimationSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp> 
#include <algorithm> 
#include <cstring>
#include <map>
#include <tuple>
#include <cmath>
#include <string>    
#include <limits>   
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <vector>
#include "RendererInternal.h"

// ============================================================================
// Renderer_Overlay.cpp
// Partes do Renderer que desenham POR CIMA/ALEM da cena: linhas de gizmo, UI (tela e mundo),
// segmentos do DrawContext e particulas. Mesma classe Renderer (Renderer.h); so mudou de arquivo.
// ============================================================================
namespace Prism {

    void Renderer::DrawLines(const float* points, uint32_t pointCount, const float* viewProjection, const float* color) {
        if (!s_LineShader || pointCount == 0) return;

        // ATENCAO: s_LineVAO e criado uma unica vez em Init() (contexto do
        // editor) e, como qualquer VAO, NAO e valido em outro contexto
        // OpenGL mesmo com share list (ver Mesh.h/
        // Mesh::BindForCurrentContext). DrawLines() so e chamado pelo
        // EditorLayer (gizmos), nunca pela PlayWindow - se gizmos de debug
        // forem desenhados tambem na PlayWindow, esta funcao vai precisar
        // do mesmo tratamento de "VAO por contexto" que Mesh:: ja tem.
        glBindVertexArray(s_LineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, s_LineVBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pointCount * 3 * sizeof(float)), points, GL_DYNAMIC_DRAW);

        s_LineShader->Bind();
        s_LineShader->SetMat4("u_ViewProjection", viewProjection);
        s_LineShader->SetFloat3("u_BaseColor", color[0], color[1], color[2]);

        glDrawArrays(GL_LINES, 0, (GLsizei)pointCount);

        glBindVertexArray(0);
        s_LineShader->Unbind();
    }

    void Renderer::DrawUIQuads(const UIQuad* quads, uint32_t count, float screenWidth, float screenHeight,
                               const Texture2D* texture, uint32_t fontTexture) {
        if (!s_UIShader || s_UISSBO == 0 || count == 0 || screenWidth <= 0.0f || screenHeight <= 0.0f)
            return;

        bool cullWasEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        bool depthWasEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(GetFullscreenQuadVAOForCurrentContext()); // VAO vazio: o quad nasce no shader
        s_UIShader->Bind();
        s_UIShader->SetFloat2("u_ScreenSize", screenWidth, screenHeight);
        if (texture && texture->IsValid())
            texture->Bind(0);
        if (fontTexture != 0)
            glBindTextureUnit(1, fontTexture);

        glNamedBufferData(s_UISSBO, (GLsizeiptr)(count * sizeof(UIQuad)), quads, GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_UISSBO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);

        glBindVertexArray(0);
        s_UIShader->Unbind();
        glDisable(GL_BLEND);
        if (depthWasEnabled)
            glEnable(GL_DEPTH_TEST);
        if (cullWasEnabled)
            glEnable(GL_CULL_FACE);
    }

    void Renderer::DrawUIQuadsWorld(const UIQuad* quads, uint32_t count, const float* model, const float* viewProjection,
                                    float canvasWidth, float canvasHeight, float pixelsPerMeter,
                                    const Texture2D* texture, uint32_t fontTexture, bool depthTest) {
        if (!s_UIWorldShader || s_UISSBO == 0 || count == 0 || canvasWidth <= 0.0f || canvasHeight <= 0.0f || pixelsPerMeter <= 0.0f)
            return;

        const bool cullWasEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        const bool depthWasEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); // visivel dos dois lados (de tras fica espelhado)

        glBindVertexArray(GetFullscreenQuadVAOForCurrentContext()); // VAO vazio: o quad nasce no shader
        s_UIWorldShader->Bind();
        s_UIWorldShader->SetMat4("u_Model", model);
        s_UIWorldShader->SetMat4("u_ViewProjection", viewProjection);
        s_UIWorldShader->SetFloat2("u_CanvasSize", canvasWidth, canvasHeight);
        s_UIWorldShader->SetFloat("u_PixelsPerMeter", pixelsPerMeter);
        s_UIWorldShader->SetInt("u_OutputMode", s_OutputHDR ? 1 : 0);
        if (texture && texture->IsValid())
            texture->Bind(0);
        if (fontTexture != 0)
            glBindTextureUnit(1, fontTexture);

        glNamedBufferData(s_UISSBO, (GLsizeiptr)(count * sizeof(UIQuad)), quads, GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_UISSBO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);

        glBindVertexArray(0);
        s_UIWorldShader->Unbind();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (cullWasEnabled) glEnable(GL_CULL_FACE);
    }

    void Renderer::DrawLineSegments(const LineSegment* segments, uint32_t count, const float* viewProjection, bool depthTest) {
        if (!s_DrawLineShader || s_DrawLineSSBO == 0 || count == 0)
            return;

        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] <= 0 || viewport[3] <= 0)
            return;

        const bool cullWasEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        const bool depthWasEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(GetFullscreenQuadVAOForCurrentContext());
        s_DrawLineShader->Bind();
        s_DrawLineShader->SetMat4("u_ViewProjection", viewProjection);
        s_DrawLineShader->SetFloat2("u_ViewportSize", (float)viewport[2], (float)viewport[3]);
        s_DrawLineShader->SetInt("u_OutputMode", s_OutputHDR ? 1 : 0);

        glNamedBufferData(s_DrawLineSSBO, (GLsizeiptr)(count * sizeof(LineSegment)), segments, GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_DrawLineSSBO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);

        glBindVertexArray(0);
        s_DrawLineShader->Unbind();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (cullWasEnabled) glEnable(GL_CULL_FACE);
    }

    void Renderer::DrawParticles(Scene& scene, const float* view, const float* projection) {
        if (!s_ParticleShader || s_ParticleSSBO == 0)
            return;

        auto emitters = scene.GetRegistry().view<ParticleEmitterComponent>();
        if (emitters.begin() == emitters.end())
            return;

        glm::mat4 viewMatrix = glm::make_mat4(view);
        glm::mat4 viewProjection = glm::make_mat4(projection) * viewMatrix;
        // Eixos da camera no mundo = linhas da matriz de view (rotacao ortonormal).
        glm::vec3 cameraRight(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
        glm::vec3 cameraUp(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
        glm::vec3 cameraPosition(s_CameraWorldPos[0], s_CameraWorldPos[1], s_CameraWorldPos[2]);

        struct GPUParticle {
            glm::vec4 PositionSize;
            glm::vec4 Color;
        };
        std::vector<GPUParticle> gpuParticles;
        std::vector<std::pair<float, uint32_t>> order; // (distancia^2, indice) para ordenar o blend normal

        bool cullWasEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        bool shaderBound = false;

        for (auto handle : emitters) {
            const std::vector<Particle>* particles = ParticleSystem::GetParticles(scene, handle);
            if (!particles || particles->empty())
                continue;
            const auto& emitter = emitters.get<ParticleEmitterComponent>(handle);

            if (!shaderBound) {
                glEnable(GL_BLEND);
                glDepthMask(GL_FALSE);
                glDisable(GL_CULL_FACE);
                glBindVertexArray(GetFullscreenQuadVAOForCurrentContext()); // VAO vazio: o quad nasce no shader
                s_ParticleShader->Bind();
                s_ParticleShader->SetInt("u_OutputMode", s_OutputHDR ? 1 : 0);
                s_ParticleShader->SetMat4("u_ViewProjection", glm::value_ptr(viewProjection));
                s_ParticleShader->SetFloat3("u_CameraRight", cameraRight.x, cameraRight.y, cameraRight.z);
                s_ParticleShader->SetFloat3("u_CameraUp", cameraUp.x, cameraUp.y, cameraUp.z);
                s_ParticleShader->SetInt("u_Texture", 0);
                shaderBound = true;
            }

            // Ordem de desenho: aditivo nao depende dela; alpha normal vai de tras para frente.
            order.clear();
            order.reserve(particles->size());
            for (uint32_t i = 0; i < (uint32_t)particles->size(); i++) {
                glm::vec3 delta = (*particles)[i].Position - cameraPosition;
                order.emplace_back(glm::dot(delta, delta), i);
            }
            if (!emitter.Additive)
                std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

            gpuParticles.clear();
            gpuParticles.reserve(order.size());
            for (const auto& entry : order) {
                const Particle& particle = (*particles)[entry.second];
                float t = std::min(std::max(particle.Age / particle.Lifetime, 0.0f), 1.0f);
                float size = emitter.SizeStart + (emitter.SizeEnd - emitter.SizeStart) * t;
                glm::vec4 color = glm::mix(emitter.ColorStart, emitter.ColorEnd, t);
                gpuParticles.push_back({ glm::vec4(particle.Position, size), color });
            }

            glNamedBufferData(s_ParticleSSBO, (GLsizeiptr)(gpuParticles.size() * sizeof(GPUParticle)), gpuParticles.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_ParticleSSBO);

            if (emitter.Additive)
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            else
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            Texture2D* texture = GetOrLoadTexture(emitter.TexturePath, false);
            bool useTexture = texture && texture->IsValid();
            if (useTexture)
                texture->Bind(0);
            s_ParticleShader->SetInt("u_UseTexture", useTexture ? 1 : 0);

            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)gpuParticles.size());
        }

        if (shaderBound) {
            glBindVertexArray(0);
            s_ParticleShader->Unbind();
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (cullWasEnabled)
                glEnable(GL_CULL_FACE);
        }
    }

}
