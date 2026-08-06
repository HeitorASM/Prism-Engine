#pragma once

// ============================================================================
// Command.h
// Interface base do padrao Command, usada pelo sistema de Undo/Redo (ver
// CommandHistory.h). Cada acao reversivel do editor (mover uma entidade,
// criar, excluir, editar uma cor, etc) vira uma classe pequena que sabe
// como se aplicar (Execute) e como se desfazer (Undo).
//
// Isto fica no CORE da engine (nao dentro do Editor) de proposito: qualquer
// ferramenta futura que edite estado (o editor de brushes/BSP, o editor de
// materiais) vai precisar do mesmo mecanismo de undo/redo - nao faz sentido
// reimplementar isso fora da engine so porque hoje quem usa e o EditorLayer.
// As classes de Command CONCRETAS (TransformCommand, CreateEntityCommand,
// etc), essas sim ficam no lado do Editor, porque conhecem tipos como
// Prism::Entity/Scene especificos do fluxo de edicao.
// ============================================================================

#include "Base.h"
#include <string>

namespace Prism {

    class Command {
    public:
        virtual ~Command() = default;

        // Aplica a acao. Chamado uma vez quando o comando e criado/empurrado
        // na CommandHistory, e novamente sempre que um Redo() o traz de volta.
        virtual void Execute() = 0;

        // Reverte exatamente o que Execute() fez. Deve deixar o estado
        // identico a como estava ANTES do Execute() correspondente.
        virtual void Undo() = 0;

        // Nome curto para exibir em UI (ex: "Mover Entidade", "Criar Cubo") -
        // usado por paineis de historico de undo, se/quando existirem.
        virtual std::string GetName() const = 0;
    };

}
