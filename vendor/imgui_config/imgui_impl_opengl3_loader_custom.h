// ============================================================================
// imgui_impl_opengl3_loader_custom.h
// Quando IMGUI_IMPL_OPENGL_LOADER_CUSTOM esta definido, o backend
// imgui_impl_opengl3.cpp espera que ESTE arquivo exista e inclua o loader
// OpenGL que o projeto estiver usando - no nosso caso, o glad2 vendorizado.
//
// Isso evita que o ImGui traga seu proprio loader embutido (que colidiria
// com o glad, causando o mesmo tipo de erro "OpenGL header already
// included" que ja resolvemos no resto da engine).
// ============================================================================
#include <glad/gl.h>
