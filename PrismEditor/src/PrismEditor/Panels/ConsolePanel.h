#pragma once

// ============================================================================
// ConsolePanel.h
// Painel de Console de verdade do editor - substitui o texto fixo
// "Saida de log aparecera aqui" que existia antes. Le de Prism::LogBuffer
// (ver Prism/src/Prism/Core/LogBuffer.h), que recebe uma copia de toda
// mensagem passada para PRISM_INFO/PRISM_CORE_ERROR/etc desde que a
// Application foi criada.
//
// Features: cor por nivel (Trace/Info/Warn/Error/Critical), filtro de texto,
// toggles para esconder Trace/Info (verbosidade), auto-scroll (desativado
// automaticamente se o usuario rolar para cima, reativado ao rolar ate o
// fim), botao Limpar, e contagem de quantas mensagens estao sendo
// filtradas vs total.
// ============================================================================

#include <Prism.h>

namespace PrismEditor {

    class ConsolePanel {
    public:
        void OnImGuiRender();

    private:
        char m_FilterBuffer[256] = "";

        bool m_ShowTrace = false; // Trace e verboso demais para ficar visivel por padrao
        bool m_ShowInfo = true;
        bool m_ShowWarn = true;
        bool m_ShowError = true;

        bool m_AutoScroll = true;
    };

}
