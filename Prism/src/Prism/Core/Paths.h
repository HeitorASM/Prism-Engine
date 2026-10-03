#pragma once

// ============================================================================
// Paths.h
// Caminhos do sistema que a engine precisa descobrir em tempo de execucao.
// ============================================================================

#include <filesystem>

namespace Prism {

    // Pasta do executavel em execucao (nao o diretorio de trabalho, que muda
    // conforme como o programa foi aberto). Vazio se a plataforma nao for
    // suportada ou a consulta falhar.
    std::filesystem::path GetExecutableDirectory();

}
