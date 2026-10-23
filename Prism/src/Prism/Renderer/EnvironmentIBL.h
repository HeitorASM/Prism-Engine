#pragma once

// ============================================================================
// EnvironmentIBL.h
// Iluminacao baseada em imagem (IBL) a partir do panorama do ambiente:
//   1) panorama equirretangular -> cubemap de ambiente 256^2 (com mips)
//   2) convolucao difusa        -> cubemap de irradiancia 32^2
//   3) convolucao GGX           -> cubemap especular pre-filtrado 128^2, 5 mips
// Tudo e "assado" uma vez por panorama (no primeiro quadro que o usa) com
// shaders de fragmento; rotacao e intensidade sao aplicadas ao amostrar, entao
// mexer nelas no editor e gratis. Texturas sao compartilhadas entre contextos
// GLFW; os FBOs do bake sao temporarios.
// ============================================================================

#include "Shader.h"
#include "SkyTexture.h"
#include "../Core/Base.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Prism {

    constexpr uint32_t kUnitIrradiance = 8;
    constexpr uint32_t kUnitPrefilter = 9;
    constexpr int kPrefilterLevels = 5;

    class EnvironmentIBL {
    public:
        static bool Init(const std::filesystem::path& shaderDir);
        static void Shutdown();
        // true se algum shader foi recompilado (o mapa e refeito no proximo Update).
        static bool ReloadChangedShaders();

        // Garante o bake de 'panorama' (identificado por 'key'). false = indisponivel
        // (sem shaders / falha); o chamador usa o gradiente analitico.
        static bool Update(const SkyTexture& panorama, const std::string& key);

        // Esquece o mapa atual (fim de projeto / panorama removido).
        static void Invalidate();

        // Liga os mapas nos units kUnitIrradiance/kUnitPrefilter e envia os uniforms
        // de IBL ao shader ja bindado. 'active' false = so os samplers (sem IBL).
        static void Upload(const Shader& shader, bool active, float scale, float rotationRadians);
    };

}
