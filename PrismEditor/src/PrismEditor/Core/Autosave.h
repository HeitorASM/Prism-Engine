#pragma once

// ============================================================================
// Autosave.h
// Autosave do mapa com recuperacao depois de um crash.
//
//   <Projeto>/Cache/Autosave/autosave.prismmap   a cena (mesmo formato do .prismmap)
//   <Projeto>/Cache/Autosave/autosave.info       texto: mapa de origem, nome e hora
//
// Funcionamento:
//  - Tick() roda todo frame: a cada kIntervalSeconds, se o mapa tem alteracoes nao salvas,
//    grava o autosave (atomico, sem sujar o Console). Se o mapa esta limpo, apaga o autosave.
//  - Salvar o mapa (SceneDocument) e sair do editor normalmente (OnDetach / Fechar Projeto)
//    apagam o autosave: se ele ainda existe na PROXIMA abertura, o editor nao foi fechado
//    direito (crash, queda de energia, kill) e PeekRecovery() + RenderRecoveryPopup() oferecem
//    Recuperar ou Descartar.
//  - Recuperar abre o autosave associado ao mapa ORIGINAL, ja marcado como "com alteracoes nao
//    salvas": Ctrl+S grava de volta no mapa de verdade (com o backup de sempre). O autosave nunca
//    sobrescreve o mapa por conta propria.
//
// Estado (tempo acumulado, recuperacao pendente) e interno ao Autosave.cpp: nenhum membro novo em
// classes ja existentes (ver o aviso de layout em SceneDocument.h).
// ============================================================================

#include "EditorContext.h"
#include "SceneDocument.h"

namespace PrismEditor::Autosave {

    constexpr float kIntervalSeconds = 120.0f;
    constexpr const char* kRecoveryPopupId = "Recuperar mapa";

    // Chamar UMA vez ao abrir o projeto, ANTES de carregar o mapa inicial: olha se sobrou um
    // autosave da sessao anterior e, se sim, marca a recuperacao como pendente.
    void PeekRecovery();

    // Todo frame (EditorLayer::OnUpdate). Nao faz nada enquanto a recuperacao esta pendente,
    // durante o Play ou antes do intervalo.
    void Tick(EditorContext& ctx, const SceneDocument& document, float deltaTime);

    // Apaga o autosave (mapa salvo, saida normal do editor, ou "Descartar").
    void Discard();

    // Popup modal "Recuperar mapa". Chamar todo frame (como os outros popups modais do editor).
    void RenderRecoveryPopup(EditorContext& ctx, SceneDocument& document);

    // true enquanto a pergunta de recuperacao esta pendente/aberta.
    bool IsRecoveryPending();

}
