#pragma once

// ============================================================================
// ProfilerPanel.h
// Painel "Desempenho": tempo de quadro (grafico dos ultimos quadros), zonas de
// CPU e de GPU (ultimo/media/maximo), contadores de desenho e de culling.
// Os dados vem de Prism::Profiler (Core/Profiler.h). O painel LIGA o Profiler
// enquanto esta aberto e desliga ao fechar, para nao custar nada no resto do tempo.
// ============================================================================

namespace PrismEditor {

    class ProfilerPanel {
    public:
        void OnImGuiRender();
        void Toggle() { m_Open = !m_Open; }
        bool IsOpen() const { return m_Open; }
        void SetOpen(bool open) { m_Open = open; }

    private:
        bool m_Open = false;
        bool m_Paused = false;
        bool m_WasEnabledByUs = false;
    };

}
