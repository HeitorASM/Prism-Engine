#pragma once

// ============================================================================
// SceneDocument.h
// O "documento" do editor: ciclo de vida do mapa ativo (novo, carregar,
// salvar, salvar como) e a protecao contra perda de trabalho (alteracoes nao
// salvas ao trocar de mapa ou fechar o editor). Antes eram ~15 metodos e 8
// membros de EditorLayer. Ver docs/editor.md, "Alteracoes nao salvas".
//
// Opera sobre o EditorContext (ActiveScene, CurrentMapPath, ...). Os popups
// (Salvar Como, Alteracoes nao salvas) sao desenhados por RenderSaveAsPopup()
// e RenderUnsavedChangesPopup(), chamados todo frame pelo EditorLayer.
// ============================================================================

#include "EditorContext.h"
#include <cstdint>
#include <filesystem>
#include <functional>

namespace PrismEditor {

    class SceneDocument {
    public:
        explicit SceneDocument(EditorContext& context) : m_Ctx(context) {}

        // Carrega o mapa em 'path' na Scene ativa, substituindo o que
        // estiver aberto no momento (sem perguntar "salvar antes?" ainda).
        // Chamado tanto por LoadOrCreateScene()
        // (StartMap na abertura do editor) quanto pelo duplo-clique num
        // .prismmap no Content Browser. Retorna false se a leitura falhar -
        // a Scene ativa permanece intocada nesse caso (ver
        // SceneSerializer::Deserialize). Em caso de sucesso, atualiza
        // EditorContext::CurrentMapPath para 'path' - dai em diante "Salvar Mapa" grava
        // de volta neste mesmo arquivo.
        bool LoadScene(const std::filesystem::path& mapPath);

        // Cria uma Scene nova vazia (so o nome, sem entidades) e a torna a
        // Scene ativa. EditorContext::CurrentMapPath e limpo - a proxima vez que "Salvar
        // Mapa" for usado, se comporta como "Salvar Como" (ainda nao ha
        // arquivo associado a esta cena). Chamado pelo menu Arquivo > Novo Mapa.
        void NewMap();

        // Tenta carregar Project::GetConfig().StartMap; se nao existir
        // ainda (projeto novo, primeira vez abrindo o editor), cria uma
        // cena de exemplo em memoria em vez de falhar.
        void LoadOrCreateScene();

        // Salva a Scene ativa. Se EditorContext::CurrentMapPath ja aponta para um
        // arquivo (cena carregada do disco, ou ja salva antes nesta
        // sessao), grava nele direto. Caso contrario (cena nova, nunca
        // salva), se comporta como SaveActiveSceneAs() - abre o popup de
        // nome, ja que nao ha "onde" salvar ainda. Chamado pelo menu
        // Arquivo > Salvar Mapa / Ctrl+S.
        void SaveActiveScene();

        // Sempre abre o popup pedindo um nome (mesmo se a cena ja tiver um
        // arquivo associado) e salva num arquivo NOVO dentro de
        // Project::GetMapDirectory() - nao sobrescreve o mapa anterior.
        // EditorContext::CurrentMapPath passa a apontar para o arquivo recem-criado.
        // Chamado pelo menu Arquivo > Salvar Como.
        void SaveActiveSceneAs();

        // --- Alteracoes nao salvas ("dirty") ------------------------------
        //
        // Ha alteracoes nao salvas quando o estado ATUAL da cena difere do
        // estado de quando ela foi carregada/salva pela ultima vez. A
        // comparacao e por fingerprint da cena (ver
        // SceneSerializer::ComputeFingerprint) em vez de um flag marcado a
        // cada edicao: assim pega TODA alteracao, inclusive as dos campos de
        // Light/Collider/RigidBody/Camera que nao geram comando de undo, e
        // qualquer campo novo no futuro, sem ninguem precisar lembrar de
        // "marcar dirty". E desfazer/refazer ate o estado salvo volta a
        // contar como limpo.
        //
        // Custo: uma passada pela cena por chamada (~1 ms). So e chamado
        // quando o usuario tenta descartar a cena (fechar, novo mapa, abrir
        // outro mapa) - NUNCA por frame.
        bool HasUnsavedChanges() const;

        // Ponto de entrada unico para QUALQUER acao que descartaria a cena
        // atual (fechar o editor, Novo Mapa, abrir outro mapa). Se nao ha
        // alteracoes nao salvas, executa 'action' na hora. Se ha, guarda
        // 'action' e abre o popup "Salvar alteracoes?" - 'action' so roda
        // depois que o usuario escolher Salvar (e o save der certo) ou
        // Nao salvar; Cancelar a descarta.
        void RunAfterUnsavedCheck(std::function<void()> action);

        // Desenha o popup modal "Salvar alteracoes?" (Salvar / Nao salvar /
        // Cancelar). Chamado a cada frame de RenderDockspace(), mesmo
        // fechado (mesmo motivo de todo popup modal aqui).
        void RenderUnsavedChangesPopup();

        // Desenha o popup modal de nome usado por SaveActiveSceneAs() -
        // chamado a cada frame de RenderDockspace() (ImGui::OpenPopup
        // exige isso mesmo quando o popup esta fechado, ver EditorLayer.cpp).
        void RenderSaveAsPopup();

        // Gancho do botao X da janela (Application::SetCloseRequestHandler):
        // true = pode fechar; false = recusa e, se ha alteracoes, pede para
        // RenderUnsavedChangesPopup() abrir o aviso no proximo frame.
        bool OnCloseRequested();

        // Libera o proximo pedido de fechar (usado por "Fechar Projeto", que
        // fecha pelo Application::Close() depois de resolver o aviso).
        void SetAllowWindowClose(bool allow);

        // IDs dos popups modais (o EditorLayer precisa deles para nao disparar
        // atalhos enquanto um popup esta aberto).
        // ID do popup modal de "Salvar Como" - compartilhado entre
        // RenderSaveAsPopup() (que o abre/desenha) e o atalho Ctrl+S/Ctrl+Shift+S
        // em RenderDockspace() (que precisa saber se ja esta aberto, para nao
        // tentar abrir de novo por cima de si mesmo).
        static constexpr const char* kSaveAsPopupId = "Salvar Mapa Como";
        static constexpr const char* kUnsavedChangesPopupId = "Alteracoes nao salvas";

    private:
        // Igual a SaveActiveScene(), mas retorna se a cena FICOU salva
        // (true) ou se o save foi adiado/falhou (false). Cena sem arquivo
        // abre o popup Salvar Como e retorna false: o save so acontece num
        // frame futuro, quando o usuario confirmar o nome.
        bool TrySaveActiveScene();

        // Memoriza o estado atual da cena como "o que esta salvo em disco".
        // Chamado apos carregar uma cena, criar uma nova e apos cada save
        // bem sucedido.
        void MarkSceneClean();

        EditorContext& m_Ctx;

        // =====================================================================
        // ATENCAO: os membros do "dirty flag" (alteracoes nao salvas) ficam NO
        // FIM da classe DE PROPOSITO - nao mova para o meio. Herdado de quando
        // eram membros do EditorLayer: este header e incluido por mais de um
        // .cpp (via EditorLayer.h), e mudar o layout no meio da classe, com
        // algum .obj ainda compilado com o layout antigo (build incremental),
        // causou corrupcao de heap (crash 0xC0000374 no driver, via ImGui).
        // No fim, o layout de tudo que ja existia continua identico. Se mexer
        // aqui, faca um rebuild completo.
        // =====================================================================

        // Fingerprint da cena no ultimo carregamento/save (ver
        // HasUnsavedChanges). 0 = ainda nao memorizado.
        uint64_t m_SavedSceneFingerprint = 0;

        // Estado do popup "Salvar alteracoes?" (ver RunAfterUnsavedCheck).
        // m_PendingDiscardAction e o que o usuario estava tentando fazer;
        // fica guardado ate ele decidir.
        bool m_ShowUnsavedChangesPopup = false;

        std::function<void()> m_PendingDiscardAction;

        // true quando o usuario escolheu "Salvar" numa cena SEM arquivo: o
        // save real acontece no popup Salvar Como (outro frame), e a acao
        // pendente so deve rodar se esse save de fato ocorrer. Se o usuario
        // cancelar o nome, a acao e descartada - fechar o editor depois de
        // um "Salvar" cancelado perderia o trabalho justamente quando o
        // aviso deveria protege-lo.
        bool m_RunPendingActionAfterSaveAs = false;

        // Fechar a janela (botao X) chega pelo callback do GLFW, no meio de
        // glfwPollEvents - cedo demais para abrir um popup ImGui. O gancho
        // do Application so registra o pedido aqui, e OnImGuiRender o
        // trata no proximo frame.
        bool m_CloseWindowRequested = false;

        // true enquanto o editor confirma que PODE fechar (depois do
        // usuario ter decidido no popup). Faz o gancho do Application
        // deixar o proximo pedido de fechamento passar sem perguntar de
        // novo.
        bool m_AllowWindowClose = false;

        // Estado do popup modal de "Salvar Como" (ver RenderSaveAsPopup).
        bool m_ShowSaveAsPopup = false;

        char m_SaveAsNameBuffer[128] = "";
    };

}
