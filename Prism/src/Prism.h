#pragma once

// ============================================================================
// Prism.h
// Header unico para quem CONSOME a engine (o Editor, e futuramente jogos em
// modo Runtime). Nao inclua isto de dentro da propria engine - la dentro,
// inclua sempre o header especifico que voce precisa.
// ============================================================================

#include "Prism/Core/Base.h"
#include "Prism/Core/Log.h"
#include "Prism/Core/LogBuffer.h"
#include "Prism/Core/Application.h"
#include "Prism/Core/Window.h"
#include "Prism/Core/Command.h"
#include "Prism/Core/CommandHistory.h"
#include "Prism/Layer/Layer.h"
#include "Prism/Project/Project.h"
#include "Prism/Renderer/Framebuffer.h"
#include "Prism/Renderer/Renderer.h"
#include "Prism/Renderer/Shader.h"
#include "Prism/Scene/Scene.h"
#include "Prism/Scene/Entity.h"
#include "Prism/Scene/Components.h"
#include "Prism/Scene/SceneSerializer.h"

// --- Entry Point ---
// NAO inclua EntryPoint.h aqui. Ele contem um main() e deve ser incluido
// exatamente uma vez, so no .cpp que efetivamente inicia o executavel.
