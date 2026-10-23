#pragma once

// ============================================================================
// ShadowSystem.h
// Mapas de sombra da cena, por quadro (Renderer::DrawScene chama Render()):
//
//   Directional  -> CASCADED SHADOW MAPS: a parte visivel do frustum da camera
//                   e dividida em kShadowCascades faixas (divisao logaritmica/
//                   uniforme mista); cada faixa tem um ortho "encaixado" numa
//                   esfera (estavel ao girar a camera) com os texels alinhados a
//                   grade (sem tremor ao mover). Camada de um Texture2DArray.
//   Spot         -> projecao perspectiva do cone, uma camada de um Texture2DArray.
//   Point        -> cubemap de DISTANCIA (6 faces por luz, CubeMapArray).
//
// So as luzes com CastShadows entram, ate o limite de cada tipo (kMax*): as
// que passarem do limite ficam sem sombra (aviso unico no log). Texturas sao
// criadas sob demanda (cena sem sombra nao gasta VRAM) e podem ser vistas por
// qualquer contexto GLFW; so o FBO e por contexto (FBOs nao sao compartilhados).
// ============================================================================

#include "GPULight.h"
#include "Mesh.h"
#include "Shader.h"
#include "../Core/Base.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Prism {

    constexpr int kShadowCascades = 4;
    constexpr int kMaxSpotShadows = 4;
    constexpr int kMaxPointShadows = 2;

    // Texture units usados pelo basic.frag para os mapas de sombra (0 e 1 sao
    // reservados para AO; 2-4 para as texturas do Material).
    constexpr uint32_t kUnitCascadeShadow = 5;
    constexpr uint32_t kUnitSpotShadow = 6;
    constexpr uint32_t kUnitPointShadow = 7;

    // Uma entidade que projeta sombra (mesh + transform de mundo).
    struct ShadowCaster {
        Mesh* Geometry = nullptr;
        glm::mat4 Model{ 1.0f };
    };

    // Resultado de um quadro: o que o basic.frag precisa para amostrar.
    struct ShadowFrame {
        uint64_t Generation = 0; // muda a cada Render() (o Renderer so reenvia os uniforms quando muda)

        int CascadeCount = 0;    // 0 = sem sombra direcional
        glm::mat4 CascadeMatrix[kShadowCascades];
        float CascadeSplit[kShadowCascades] = {};   // distancia (ao longo do olhar da camera) onde cada cascata termina
        float CascadeTexel[kShadowCascades] = {};   // tamanho de um texel em unidades de mundo
        float CascadeRange[kShadowCascades] = {};   // profundidade do ortho (far - near), para converter o bias
        glm::vec3 CameraForward{ 0.0f, 0.0f, -1.0f };

        int SpotCount = 0;
        glm::mat4 SpotMatrix[kMaxSpotShadows];
        float SpotTexelAt1[kMaxSpotShadows] = {};   // tamanho de um texel a distancia 1 da luz
        float SpotNear[kMaxSpotShadows] = {};
        float SpotFar[kMaxSpotShadows] = {};

        int PointCount = 0;
        float PointFar[kMaxPointShadows] = {};

        bool Any() const { return CascadeCount > 0 || SpotCount > 0 || PointCount > 0; }
    };

    class ShadowSystem {
    public:
        static bool Init(const std::filesystem::path& shaderDir);
        static void Shutdown();
        // true se algum shader foi recompilado (os uniforms precisam ser reenviados).
        static bool ReloadChangedShaders();

        // Desenha os mapas do quadro e preenche 'lights[i].ShadowSlot'. Salva e
        // restaura framebuffer/viewport. O resultado vale ate a proxima chamada.
        static const ShadowFrame& Render(const std::vector<ShadowCaster>& casters, std::vector<GPULight>& lights,
                                         const float* view, const float* projection);

        // Envia os uniforms de sombra ao shader (ja bindado) e liga as texturas.
        static void Upload(const ShadowFrame& frame, const Shader& shader);

        // Estado "sem sombras" (CascadeCount/SpotCount/PointCount = 0).
        static void UploadNone(const Shader& shader);
    };

}
