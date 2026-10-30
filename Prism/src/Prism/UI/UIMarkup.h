#pragma once

// ============================================================================
// UIMarkup.h
// Le o texto de um .prismui (um XML simples) e monta o Ui::Document.
//
//   <UI scale="height" reference="1920x1080">
//     <Panel id="hud" anchor="top-right" margin="16" layout="column" gap="8" padding="12" color="#00000099">
//       <Text id="score" size="24" color="#FFCC00">Moedas: 0</Text>
//       <Button id="pause" onClick="OnPause" width="160">Pausar</Button>
//     </Panel>
//   </UI>
//
// Erros de XML (tag sem fechar, aspas faltando...) fazem o parse FALHAR (Ok == false),
// com linha e coluna. Problemas de conteudo (atributo desconhecido, valor invalido,
// elemento que nao existe) viram AVISOS: o documento ainda carrega, com o valor padrao no
// lugar, e a mensagem diz o que estava errado (e sugere o nome certo quando e um typo).
//
// So std; sem dependencia da engine. Nao depende do locale (decimal e sempre '.').
// ============================================================================

#include "UIDocument.h"

#include <string>
#include <vector>

namespace Prism::Ui {

    struct ParseResult {
        Document Doc;
        std::vector<Diagnostic> Diagnostics;
        bool Ok = false; // false: ha ao menos um Error; Doc fica vazio

        bool HasErrors() const {
            for (const Diagnostic& d : Diagnostics)
                if (d.Level == Diagnostic::Severity::Error)
                    return true;
            return false;
        }
    };

    ParseResult ParseDocument(const std::string& text);

    // "arquivo.prismui:12:5: erro: mensagem" (formato que o painel Saida de Build/Console entende).
    std::string FormatDiagnostic(const std::string& fileName, const Diagnostic& diagnostic);

    // Ajudantes expostos para teste/uso fora do parser.
    bool ParseColor(const std::string& text, Color& out);          // #RGB #RGBA #RRGGBB #RRGGBBAA ou nome
    bool ParseLength(const std::string& text, Length& out);        // auto | fill | 2fr | 50% | 120 | 120px
    bool ParseInsets(const std::string& text, Insets& out);        // 1, 2 (vert horiz) ou 4 (cima dir baixo esq) numeros
    bool ParseFloatStrict(const std::string& text, float& out);    // so '.', sem locale; false se sobrar texto

}
