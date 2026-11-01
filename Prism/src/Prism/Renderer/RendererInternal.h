#pragma once

// ============================================================================
// RendererInternal.h
// Estado compartilhado ENTRE os arquivos de implementacao do Renderer (Renderer.cpp,
// Renderer_Lights.cpp, Renderer_Overlay.cpp). Nao e API publica: so inclua dentro de
// Prism/src/Prism/Renderer/*.cpp.
// ============================================================================

#include "Shader.h"

namespace Prism::RendererDetail {

    // Shader em uso pelo estado de quadro/material/objeto: o padrao ou o de um material com
    // ShaderPath. Sempre aponta para um shader valido depois de Renderer::Init().
    extern Shader* ActiveShader;

}
