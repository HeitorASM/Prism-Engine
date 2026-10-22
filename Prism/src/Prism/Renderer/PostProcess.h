#pragma once

// ============================================================================
// PostProcess.h
// Pos-processamento da cena. O Renderer::DrawScene desenha a cena num buffer
// HDR (RGBA16F, linear) em vez de direto no destino; no fim, End() faz:
//
//   bloom (descida/subida de 13/9 amostras)  ->  composicao (exposicao + ACES,
//   saturacao, contraste)  ->  FXAA (opcional)  ->  destino original
//
// e copia a profundidade para o destino, para o que for desenhado DEPOIS
// (gizmos, linhas) continuar sendo ocluido pela cena.
//
// COM TUDO DESLIGADO a imagem e a mesma de antes: a composicao usa o mesmo
// ACES + sRGB que o basic.frag aplicava, e o ceu LDR / cor chapada pula o tone
// mapping (ver post_composite.frag), saindo exatamente como foi escolhido.
//
// FBOs NAO sao compartilhados entre contextos GLFW (editor x janela de Play),
// entao ha UMA instancia por contexto (ForCurrentContext), como os VAOs do
// fullscreen quad no Renderer. Os shaders (programas) sao compartilhados.
// ============================================================================

#include "Shader.h"
#include "../Core/Base.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Prism {

    struct PostProcessSettings {
        bool Bloom = false;
        float BloomThreshold = 1.0f;  // brilho (linear) a partir do qual entra no bloom
        float BloomIntensity = 0.15f; // quanto do bloom e somado a imagem
        bool FXAA = true;
        float Exposure = 1.0f;
        float Saturation = 1.0f;
        float Contrast = 1.0f;
        bool SkyPassThrough = true;   // ceu LDR sai sem exposicao/ACES (falso para panorama HDR)
    };

    class PostProcess {
    public:
        // Carrega os shaders (uma vez, com um contexto atual) / libera tudo.
        static bool Init(const std::filesystem::path& shaderDir);
        static void Shutdown();
        static void ReloadChangedShaders();

        // Instancia do contexto GLFW atual (criada sob demanda).
        static PostProcess& ForCurrentContext();

        // Garante os alvos com o tamanho pedido, liga o FBO HDR e ajusta o
        // viewport (0,0,w,h). false = nao foi possivel (o chamador desenha direto).
        bool Begin(uint32_t width, uint32_t height);

        // Termina o quadro: pos-processa o HDR em 'targetFramebuffer' (no
        // retangulo 'viewport' = x, y, w, h) e copia a profundidade.
        void End(const PostProcessSettings& settings, uint32_t targetFramebuffer, const int viewport[4]);

        ~PostProcess();
        PostProcess(const PostProcess&) = delete;
        PostProcess& operator=(const PostProcess&) = delete;

    private:
        PostProcess() = default;

        struct Level {
            uint32_t Texture = 0;
            uint32_t Framebuffer = 0;
            uint32_t Width = 0, Height = 0;
        };

        // Alvos de UM tamanho. O editor desenha varias vistas por quadro (viewport,
        // aba Game, pre-visualizacao da camera) com tamanhos diferentes: manter um
        // conjunto por tamanho evita recriar texturas/FBOs a cada DrawScene.
        struct Targets {
            uint32_t Width = 0, Height = 0;
            uint32_t HdrFramebuffer = 0, HdrColor = 0, HdrDepth = 0;
            uint32_t LdrFramebuffer = 0, LdrColor = 0; // so com FXAA
            std::vector<Level> BloomLevels;
            uint64_t LastUsed = 0;
        };

        static void DestroyTargets(Targets& targets);
        static void DestroyBloomLevels(Targets& targets);
        bool CreateTargets(Targets& targets, uint32_t width, uint32_t height);
        bool EnsureBloomLevels(Targets& targets);
        bool EnsureLdrTarget(Targets& targets);
        void RunBloom(const Targets& targets, const PostProcessSettings& settings);
        void DrawTriangle() const;

    private:
        static constexpr size_t kMaxTargetSets = 4;
        std::vector<Targets> m_Sets;
        size_t m_Current = 0;   // indice em m_Sets do conjunto em uso neste quadro
        uint64_t m_Tick = 0;
        uint32_t m_Vao = 0;
    };

}
