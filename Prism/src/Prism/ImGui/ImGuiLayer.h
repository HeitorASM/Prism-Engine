#pragma once
#include "../Layer/Layer.h"

namespace Prism {

    // Layer especial: sempre um Overlay (fica por cima de todas as outras).
    // Cuida do ciclo Begin/End do ImGui a cada frame. As Layers do Editor
    // (paineis de hierarquia, viewport, etc.) apenas CHAMAM ImGui::Begin("...")
    // dentro do proprio OnImGuiRender - esta classe so garante que o contexto
    // ImGui exista e esteja pronto antes, e seja "flushado" para tela depois.
    class ImGuiLayer : public Layer {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnEvent(Event& e) override;

        void Begin();
        void End();

        void SetBlockEvents(bool block) { m_BlockEvents = block; }
        void SetDarkThemeColors();

    private:
        bool m_BlockEvents = true;
        float m_Time = 0.0f;
    };

}
