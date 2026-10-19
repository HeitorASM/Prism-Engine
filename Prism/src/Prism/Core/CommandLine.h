#pragma once

// ============================================================================
// CommandLine.h
// Argumentos com que o processo foi iniciado. O main() generico
// (EntryPoint.h) os guarda antes de chamar Prism::CreateApplication(), para
// que o lado da aplicacao (ex: o PrismRuntime, que recebe o caminho do
// projeto) os leia sem precisar de um main() proprio.
// ============================================================================

#include <string>
#include <vector>

namespace Prism {

    // Chamado uma unica vez pelo main() de EntryPoint.h.
    void SetLaunchArguments(int argc, char** argv);

    // argv sem o nome do executavel (argv[0]). Vazio se nao houver argumentos.
    // (Nome diferente de "GetCommandLine" de proposito: <windows.h> define
    // essa palavra como macro.)
    const std::vector<std::string>& GetLaunchArguments();

}
