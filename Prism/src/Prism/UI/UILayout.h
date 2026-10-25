#pragma once

// ============================================================================
// UILayout.h
// Posiciona os elementos de um Ui::Document (preenche Node::Bounds, Lines e
// ScaledFontSize). So std: o que depende de fonte/texturas entra pela interface
// Measure, que o UISystem implementa com o atlas de fonte e o cache de texturas
// (e os testes implementam com larguras fixas).
//
// Modelo (parecido com flexbox, bem menor):
//  - Todo elemento tem margem (fora) e padding (dentro). Valores em pixels de
//    PROJETO: multiplicados pela escala do documento (ver ComputeScale).
//  - Panel com layout="stack" (padrao): cada filho se ancora dentro do pai
//    (anchor + margin), sobrepostos. layout="row"/"column": filhos em linha/coluna,
//    com gap, align (eixo transversal) e justify (eixo principal).
//  - width/height: auto (conteudo), px, %, fill/Nfr (fatia do espaco que sobra).
//  - Elementos invisiveis (ou com ancestral invisivel) nao ocupam espaco.
// ============================================================================

#include "UIDocument.h"

namespace Prism::Ui {

    // Altura de uma linha de texto = tamanho da fonte * este fator (igual ao desenho do UISystem).
    constexpr float kLineHeightFactor = 1.2f;

    class Measure {
    public:
        virtual ~Measure() = default;
        // Largura em pixels de 'utf8' numa linha so, com a fonte de 'fontPx' pixels de altura.
        virtual float TextWidth(const std::string& utf8, float fontPx) const = 0;
        // Tamanho natural da textura em pixels, ou {0,0} se nao carregou.
        virtual Size ImageSize(const std::string& path) const { (void)path; return {}; }
    };

    // Fator de escala pelo modo do documento (limitado a 0.1..10).
    float ComputeScale(const Document& document, float screenWidth, float screenHeight);

    // Layout completo da tela 'screenWidth' x 'screenHeight' (pixels).
    void LayoutDocument(Document& document, float screenWidth, float screenHeight, const Measure& measure);

    // Quebra 'text' em linhas: '\n' sempre quebra; com 'wrap' e maxWidth > 0 quebra tambem
    // por palavras (e por caractere, se uma palavra sozinha nao cabe).
    void BreakLines(const std::string& text, float fontPx, bool wrap, float maxWidth, const Measure& measure,
                    std::vector<std::string>& outLines);

}
