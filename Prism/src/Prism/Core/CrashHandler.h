#pragma once

// ============================================================================
// CrashHandler.h
// Mostra uma janela de erro NATIVA e AMIGAVEL (MessageBox do Win32) antes
// do processo morrer, para qualquer crash catastrofico que EntryPoint.h
// sozinho NAO consegue pegar: excecoes C++ (std::exception e afins) ja
// sao capturadas la (try/catch em torno de app->Run()) - mas isso nao
// cobre coisas como:
//   - Qualquer assert que chama abort()/__debugbreak() diretamente (ver
//     PRISM_ASSERT, Base.h, e JPH_ASSERT do proprio Jolt Physics -
//     ambos, em builds Debug, encerram o processo sem lancar excecao
//     nenhuma - nada para o try/catch de EntryPoint.h pegar).
//   - Acesso a memoria invalida (null pointer, out-of-bounds em array
//     cru, etc) - EXCEPTION_ACCESS_VIOLATION do Windows, que tambem nao
//     e uma std::exception.
//   - Divisao por zero de inteiros, stack overflow, e outras excecoes
//     estruturadas (SEH) do proprio sistema operacional.
//
// Sem este handler, qualquer um desses casos resulta no diagnostico
// generico e assustador do proprio Windows ("PrismEditor.exe parou de
// funcionar", ou pior, o processo simplesmente desaparece sem nenhuma
// mensagem se rodado fora de um debugger) - nao e o tipo de experiencia
// que qualquer usuario do editor deveria ter, mesmo sabendo que a causa
// raiz e um bug da engine (ex: um Collider degenerado que ainda nao
// tinha o clamp de tamanho minimo, ver PhysicsEngine::CreateBodyForEntity).
//
// WINDOWS-ONLY: usa SetUnhandledExceptionFilter, uma API do Win32 (mesma
// limitacao de PRISM_DEBUGBREAK em Base.h, que so define __debugbreak()
// para MSVC/Windows). Guardado por PRISM_PLATFORM_WINDOWS (definida pelo
// CMake, ver Prism/CMakeLists.txt); em qualquer outra plataforma este
// arquivo vira um no-op silencioso. Um handler equivalente para Linux/macOS
// (via sigaction para SIGSEGV/SIGABRT) ainda nao existe.
// ============================================================================

namespace Prism {

    // Instala o handler global - chamar UMA VEZ, o mais cedo possivel no
    // main() (ver EntryPoint.h), antes de qualquer outra inicializacao
    // que possa crashar. Nao-destrutivo/idempotente o suficiente para
    // chamar mais de uma vez sem problema (SetUnhandledExceptionFilter
    // apenas substitui o filtro anterior pelo mesmo), mas o uso normal e
    // chamar so uma vez.
    //
    // IMPORTANTE ao testar: SetUnhandledExceptionFilter (a API usada
    // aqui) e IGNORADO PELO WINDOWS quando um debugger esta anexado ao
    // processo (ex: rodando com F5 no Visual Studio) - isso e
    // comportamento PADRAO do Windows, nao um bug deste handler: o
    // debugger sempre tem prioridade e intercepta a excecao primeiro
    // (e o comportamento CORRETO durante desenvolvimento - voce quer que
    // o VS pare exatamente na linha do assert, nao que uma caixa de
    // mensagem apareca por cima escondendo isso). Para ver a tela de
    // erro amigavel de verdade, rode o .exe compilado DIRETAMENTE (duplo
    // clique, ou Ctrl+F5 "Start Without Debugging" no Visual Studio), nao
    // com F5.
    void InstallCrashHandler();

}
