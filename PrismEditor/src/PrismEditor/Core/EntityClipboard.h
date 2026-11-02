#pragma once

// ============================================================================
// EntityClipboard.h
// Area de transferencia do editor para entidades (Copiar / Recortar / Colar).
// Guarda uma COPIA FIEL da subarvore copiada numa Scene propria, entao:
//   - continua valida se a entidade original for editada ou excluida depois;
//   - sobrevive a troca de mapa (copia num mapa, cola em outro);
//   - copia a subarvore inteira (filhos, components, vinculo de prefab).
// Vive so na memoria do editor (nao usa a area de transferencia do sistema).
// ============================================================================

#include <Prism.h>

namespace PrismEditor::EntityClipboard {

    // Substitui o conteudo pela subarvore de 'entity'. Entidade invalida = nao faz nada.
    void Copy(Prism::Entity entity);

    // Ha algo para colar?
    bool HasContent();

    // Raiz da copia guardada (invalida se vazio). PasteEntityCommand faz a propria copia a partir dela.
    Prism::Entity Root();

    // Nome da entidade copiada, para a UI ("Colar 'Cubo'"). Vazio se nao ha conteudo.
    std::string Label();

    void Clear();

}
