#pragma once

// ============================================================================
// CommandHistory.h
// Pilha de undo/redo classica: dois stacks de Command (ver Command.h). Novo
// comando -> Execute() e empurrado no stack de undo, e o stack de redo e
// limpo (a convencao universal em editores: fazer uma acao nova depois de
// desfazer descarta o "futuro" que foi desfeito, exatamente como
// Photoshop/Word/qualquer editor de texto se comporta).
//
// Dono de todos os Commands empurrados aqui (unique_ptr) - quando um
// comando sai definitivamente do historico (limite de tamanho atingido, ou
// um Execute() novo limpa o redo stack), ele e destruido normalmente.
// ============================================================================

#include "Base.h"
#include "Command.h"
#include <vector>
#include <memory>
#include <string>

namespace Prism {

    class CommandHistory {
    public:
        // maxSize evita crescimento sem limite numa sessao de edicao longa -
        // ao estourar, o comando mais antigo do stack de undo e descartado.
        // 200 e um numero arbitrario, generoso o bastante para uma sessao de
        // edicao normal sem consumir memoria demais guardando historico.
        explicit CommandHistory(size_t maxSize = 200);

        // Executa o comando imediatamente (chama Execute()) e o registra no
        // historico. O CommandHistory passa a ser dono do ponteiro.
        void Execute(Scope<Command> command);

        void Undo();
        void Redo();

        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }

        // Nome do proximo comando que Undo()/Redo() afetaria - util para
        // mostrar "Desfazer 'Mover Entidade'" em vez de so "Desfazer" no
        // menu (ver EditorLayer::RenderMenuBar).
        std::string PeekUndoName() const;
        std::string PeekRedoName() const;

        void Clear();

    private:
        std::vector<Scope<Command>> m_UndoStack;
        std::vector<Scope<Command>> m_RedoStack;
        size_t m_MaxSize;
    };

}
