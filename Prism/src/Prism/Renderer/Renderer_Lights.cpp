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
// Renderer_Lights.cpp
// Coleta e envio das luzes: LightComponent -> GPULight, escolha por relevancia (LightSelection.h)
// e upload para o shader ativo. Mesma classe Renderer (Renderer.h); so mudou de arquivo.
// ============================================================================
namespace Prism {

    namespace {

    // Traduz TODA luz da cena (sem limite) na ordem do registro. Quem limita/escolhe sao
    // as CollectGPULights abaixo.
    std::vector<GPULight> TranslateAllLights(Scene& scene) {
        std::vector<GPULight> result;

        // Toda entidade com TransformComponent + LightComponent conta como
        // fonte de luz. Usa a transform de MUNDO (mesma logica que
        // DrawScene ja usa para meshes) - uma luz filha de um objeto pai
        // (ex: uma lanterna presa na mao de um personagem) acompanha a
        // posicao/rotacao do pai corretamente.
        auto view = scene.GetRegistry().view<TransformComponent, LightComponent>();
        for (auto entityHandle : view) {
            const auto& light = view.get<LightComponent>(entityHandle);
            if (!scene.IsEntityVisible(entityHandle))
                continue; // luz escondida nao ilumina
            glm::mat4 worldTransform = scene.GetWorldTransform(Entity(entityHandle, &scene));

            GPULight gpuLight;
            gpuLight.SourceEntityId = (uint32_t)entityHandle;
            gpuLight.Color = light.Color;
            gpuLight.Intensity = light.Intensity;
            gpuLight.Range = light.Range;
            gpuLight.CastShadows = light.CastShadows;

            // Posicao de mundo = coluna de translacao da matriz de
            // transform combinada (ultima coluna, xyz).
            gpuLight.Position = glm::vec3(worldTransform[3]);

            // Direcao "para frente" da entidade em espaco de mundo:
            // rotaciona o eixo -Z local (convencao da engine para "frente"
            // de uma entidade, mesma usada pela CameraComponent) pela
            // parte de rotacao/escala da matriz de mundo. Relevante para
            // Spot e Directional; Point ignora esse campo no shader.
            glm::vec3 forwardLocal = { 0.0f, 0.0f, -1.0f };
            gpuLight.Direction = glm::normalize(glm::mat3(worldTransform) * forwardLocal);

            // Traducao especifica por tipo - este e o UNICO lugar que
            // precisa de um novo "case" ao adicionar um LightType novo
            // (ex: Area) - ver comentario grande em Components.h sobre
            // extensibilidade. Point e o "default" (gpuLight.Type = 0)
            // ja fica correto sem entrar em nenhum branch abaixo.
            switch (light.Type) {
                case LightType::Point:
                    gpuLight.Type = 0; // LIGHT_TYPE_POINT no shader
                    break;
                case LightType::Spot:
                    gpuLight.Type = 1; // LIGHT_TYPE_SPOT no shader
                    // Pre-calcula os cossenos aqui (CPU, uma vez por
                    // frame por luz) em vez de no shader (por-fragmento,
                    // milhares de vezes por frame) - cos() e caro para
                    // repetir por pixel quando o angulo so muda quando o
                    // usuario edita a luz.
                    gpuLight.CosOuterAngle = glm::cos(glm::radians(light.SpotAngle));
                    gpuLight.CosInnerAngle = glm::cos(glm::radians(glm::min(light.InnerSpotAngle, light.SpotAngle)));
                    break;
                case LightType::Directional:
                    gpuLight.Type = 2; // LIGHT_TYPE_DIRECTIONAL no shader
                    break;
                // TODO(Area/IES): quando LightType ganhar esses valores
                // (ver Components.h), adicionar os cases aqui.
            }

            result.push_back(gpuLight);
        }

        return result;
    }

    // Historico de escolha por (contexto GL, cena): a histerese precisa saber o que ESTA camera
    // escolheu no quadro anterior. O editor desenha mais de uma vista (viewport + Game) e cada
    // uma tem cena/contexto proprios, entao uma nao embaralha a outra.
    struct LightHistoryEntry {
        GLFWwindow* Context = nullptr;
        const Scene* SceneKey = nullptr;
        std::vector<uint32_t> SelectedIds; // ordem crescente (exigido por LightSelection::Select)
        size_t LastTotal = 0;              // para avisar so quando o total de luzes mudar
    };

    LightHistoryEntry& LightHistoryFor(const Scene* scene) {
        static std::vector<LightHistoryEntry> history;
        GLFWwindow* context = glfwGetCurrentContext();
        for (LightHistoryEntry& entry : history)
            if (entry.Context == context && entry.SceneKey == scene)
                return entry;
        if (history.size() >= 8)
            history.erase(history.begin()); // cenas antigas (trocas de mapa, Play) nao se acumulam
        history.push_back(LightHistoryEntry{ context, scene, {}, 0 });
        return history.back();
    }

    } // namespace

    std::vector<GPULight> Renderer::CollectGPULights(Scene& scene) {
        std::vector<GPULight> result = TranslateAllLights(scene);
        if (result.size() > MAX_LIGHTS)
            result.resize(MAX_LIGHTS);
        return result;
    }

    std::vector<GPULight> Renderer::CollectGPULights(Scene& scene, const float* cameraWorldPos, const Culling::Frustum& frustum) {
        std::vector<GPULight> all = TranslateAllLights(scene);

        LightHistoryEntry& history = LightHistoryFor(&scene);

        std::vector<LightSelection::Candidate> candidates;
        candidates.reserve(all.size());
        for (const GPULight& light : all) {
            LightSelection::Candidate candidate;
            candidate.Id = light.SourceEntityId;
            candidate.Type = light.Type;
            candidate.Position[0] = light.Position.x;
            candidate.Position[1] = light.Position.y;
            candidate.Position[2] = light.Position.z;
            candidate.Range = light.Range;
            candidate.Intensity = light.Intensity * std::max(light.Color.x, std::max(light.Color.y, light.Color.z));
            candidates.push_back(candidate);
        }

        const std::vector<uint32_t> chosen = LightSelection::Select(candidates, frustum, cameraWorldPos, MAX_LIGHTS, history.SelectedIds);

        std::vector<GPULight> result;
        result.reserve(chosen.size());
        history.SelectedIds.clear();
        for (uint32_t index : chosen) {
            result.push_back(all[index]);
            history.SelectedIds.push_back(all[index].SourceEntityId);
        }
        std::sort(history.SelectedIds.begin(), history.SelectedIds.end());

        if (all.size() > MAX_LIGHTS && all.size() != history.LastTotal)
            PRISM_CORE_INFO("Iluminacao: a cena tem ", all.size(), " luzes; a cada quadro as ", (size_t)MAX_LIGHTS,
                            " mais relevantes para a camera sao usadas (limite do shader).");
        history.LastTotal = all.size();

        return result;
    }

    void Renderer::UploadLights(const std::vector<GPULight>& lights) {
        if (!RendererDetail::ActiveShader) return;

        int count = (int)std::min<size_t>(lights.size(), MAX_LIGHTS);
        RendererDetail::ActiveShader->SetInt("u_LightCount", count);

        for (int i = 0; i < count; i++) {
            const GPULight& light = lights[i];
            std::string prefix = "u_Lights[" + std::to_string(i) + "].";

            RendererDetail::ActiveShader->SetInt(prefix + "Type", light.Type);
            RendererDetail::ActiveShader->SetFloat3(prefix + "Position", light.Position.x, light.Position.y, light.Position.z);
            RendererDetail::ActiveShader->SetFloat3(prefix + "Direction", light.Direction.x, light.Direction.y, light.Direction.z);
            RendererDetail::ActiveShader->SetFloat3(prefix + "Color", light.Color.x, light.Color.y, light.Color.z);
            RendererDetail::ActiveShader->SetFloat(prefix + "Intensity", light.Intensity);
            RendererDetail::ActiveShader->SetFloat(prefix + "Range", light.Range);
            RendererDetail::ActiveShader->SetFloat(prefix + "CosOuterAngle", light.CosOuterAngle);
            RendererDetail::ActiveShader->SetFloat(prefix + "CosInnerAngle", light.CosInnerAngle);
            RendererDetail::ActiveShader->SetInt(prefix + "ShadowSlot", light.CastShadows ? light.ShadowSlot : -1);
        }
    }

}
