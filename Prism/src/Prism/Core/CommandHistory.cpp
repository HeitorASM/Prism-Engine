#include "CommandHistory.h"

namespace Prism {

    CommandHistory::CommandHistory(size_t maxSize) : m_MaxSize(maxSize) {}

    void CommandHistory::Execute(Scope<Command> command) {
        command->Execute();

        // Fazer uma acao nova depois de um Undo() descarta o "futuro" -
        // convencao universal de editores (Photoshop, Word, etc).
        m_RedoStack.clear();

        m_UndoStack.push_back(std::move(command));

        if (m_UndoStack.size() > m_MaxSize) {
            // Descarta o comando mais antigo (inicio do vector). erase no
            // inicio de um vector e O(n), mas isso so acontece uma vez a
            // cada m_MaxSize execucoes - nao e um caminho quente.
            m_UndoStack.erase(m_UndoStack.begin());
        }
    }

    void CommandHistory::Undo() {
        if (m_UndoStack.empty())
            return;

        Scope<Command> command = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();

        command->Undo();

        m_RedoStack.push_back(std::move(command));
    }

    void CommandHistory::Redo() {
        if (m_RedoStack.empty())
            return;

        Scope<Command> command = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();

        command->Execute();

        m_UndoStack.push_back(std::move(command));
    }

    std::string CommandHistory::PeekUndoName() const {
        return m_UndoStack.empty() ? "" : m_UndoStack.back()->GetName();
    }

    std::string CommandHistory::PeekRedoName() const {
        return m_RedoStack.empty() ? "" : m_RedoStack.back()->GetName();
    }

    void CommandHistory::Clear() {
        m_UndoStack.clear();
        m_RedoStack.clear();
    }

}
