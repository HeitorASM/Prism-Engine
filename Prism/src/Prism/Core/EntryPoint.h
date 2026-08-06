#pragma once

// ============================================================================
// EntryPoint.h
// Define o main() real do executavel. O Editor (e futuramente o Runtime)
// so precisa implementar Prism::CreateApplication() em algum .cpp seu -
// nao escreve main() nenhum. Isso garante que toda aplicacao construida
// sobre a engine inicializa exatamente da mesma forma.
// ============================================================================

#include "Application.h"
#include "Log.h"

#include <exception>
#include <cstdlib>

int main(int argc, char** argv) {
    // Sem isso, qualquer excecao C++ nao capturada em algum lugar da engine
    // (std::filesystem::filesystem_error, std::out_of_range, etc.) sobe ate
    // aqui, o runtime chama std::terminate() -> abort(), e tudo que aparece
    // pro usuario e uma caixa generica "Debug Error! abort() has been
    // called", sem nenhuma pista de qual excecao foi ou onde ela ocorreu.
    // Capturando aqui, pelo menos logamos a mensagem real da excecao antes
    // de encerrar - fundamental para debugar isso no futuro.
    try {
        auto app = Prism::CreateApplication();
        app->Run();
        delete app;
        return 0;
    }
    catch (const std::exception& e) {
        PRISM_CORE_ERROR("Excecao nao tratada, encerrando: ", e.what());
        return EXIT_FAILURE;
    }
    catch (...) {
        PRISM_CORE_ERROR("Excecao desconhecida (nao derivada de std::exception), encerrando.");
        return EXIT_FAILURE;
    }
}
