#pragma once

// ============================================================================
// RuntimeLayer.h
// A unica Layer do PrismRuntime: abre o projeto, carrega o mapa inicial, roda
// scripts/fisica e desenha o jogo na janela pela camera principal
// (Prism::GameView - o mesmo desenho da aba Game e da janela de Play do editor).
// ============================================================================

#include <Prism.h>

namespace PrismRuntime {

    class RuntimeLayer : public Prism::Layer {
    public:
        RuntimeLayer() : Layer("RuntimeLayer") {}

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;

        // Para scripts/fisica e solta a cena. Idempotente. Chamado pela
        // Application antes de desligar a engine (ver RuntimeApp.cpp).
        void StopGame();

    private:
        // Falha de carga: loga o motivo e fecha a aplicacao.
        void Fail(const std::string& reason);
        void ToggleFullscreen();

        Prism::Ref<Prism::Scene> m_Scene;
        bool m_Failed = false;

        // Estado para sair do tela cheia (posicao/tamanho da janela antes).
        bool m_Fullscreen = false;
        int m_WindowedX = 100, m_WindowedY = 100, m_WindowedW = 1280, m_WindowedH = 720;
        bool m_F11WasDown = false;
    };

}
