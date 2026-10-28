// Gancho de superficie PADRAO: nao faz nada.
//
// Um material com 'ShaderPath' troca ESTE arquivo pelo .glsl do projeto
// (ver Renderer.cpp, ShaderIncludeOverrides). O arquivo do projeto deve
// definir exatamente:
//
//     void prism_surface(inout PrismSurface s, in PrismSurfaceInput i)
//
// PrismSurface / PrismSurfaceInput sao declarados em basic.frag antes do include.
void prism_surface(inout PrismSurface s, in PrismSurfaceInput i) {
}
