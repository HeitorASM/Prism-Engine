#pragma once

// ============================================================================
// Input.h
// Sistema de input POR POLLING (nao por evento) - a API que scripts Lua e
// gameplay em C++ usam para perguntar "esta tecla esta apertada AGORA?" a
// qualquer momento de um OnUpdate(), em vez de reagir a eventos de
// teclado/mouse conforme eles chegam (ver Event.h/KeyEvent.h/MouseEvent.h -
// esses continuam existindo e sendo usados pelo EDITOR, ex: atalhos de
// Undo/Redo em EditorLayer::OnEvent). Polling e o modelo que faz sentido
// para gameplay: um script de movimento tipicamente quer "IsKeyDown(W)"
// dentro do proprio OnUpdate(deltaTime), nao um callback separado.
//
// DELIBERADAMENTE GLOBAL/ESTATICO (mesmo padrao de Renderer, ScriptEngine,
// PhysicsEngine) em vez de uma instancia por Scene - so existe UM teclado/
// mouse fisico por processo, nao um por Scene rodando. O que MUDA por
// contexto e QUAL GLFWwindow* esta sendo consultado (ver SetContext) -
// hoje isso e sempre a janela da PlayWindow (ver
// PrismEditor/Play/PlayWindow.cpp), nunca a janela do editor, porque
// scripts (que sao os unicos consumidores desta API - ver
// ScriptEngine::RegisterAPI) so rodam durante o modo Play.
//
// Nao vaza GLFW para quem usa a API (assinaturas so usam int/bool/float),
// mas os CODIGOS de tecla/botao usados como parametro SAO literalmente as
// macros GLFW_KEY_*/GLFW_MOUSE_BUTTON_* (ver Prism::Key/Prism::MouseButton
// abaixo) - decisao deliberada: reinventar um enum proprio 1:1 identico ao
// do GLFW so adicionaria uma camada de traducao sem nenhum ganho real
// (GLFW ja e a lib de janela/input escolhida em toda a engine, ver
// GlfwWindow.h/PlayWindow.cpp), e KeyEvent::GetKeyCode() (Core/KeyEvent.h)
// tambem expoe esses mesmos codigos crus.
// ============================================================================

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

struct GLFWwindow;

namespace Prism {

    // Subconjunto de codigos de tecla mais usados em gameplay (WASD, setas,
    // Espaco, Shift, Ctrl, Esc, 0-9, letras). Os valores SAO os GLFW_KEY_*
    // correspondentes (ver comentario no topo do arquivo) - qualquer outro
    // GLFW_KEY_* nao listado aqui ainda funciona normalmente passando o
    // int cru para IsKeyDown/IsKeyPressed (esta lista e so conveniencia
    // para C++ e para os nomes expostos em Lua via a tabela `Key` - ver
    // ScriptEngine::RegisterAPI - nao uma whitelist restritiva).
    enum class Key : int {
        Space = 32,
        Apostrophe = 39,
        Comma = 44, Minus = 45, Period = 46, Slash = 47,
        D0 = 48, D1 = 49, D2 = 50, D3 = 51, D4 = 52, D5 = 53, D6 = 54, D7 = 55, D8 = 56, D9 = 57,
        Semicolon = 59, Equal = 61,
        A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72, I = 73, J = 74,
        K = 75, L = 76, M = 77, N = 78, O = 79, P = 80, Q = 81, R = 82, S = 83, T = 84,
        U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,
        Escape = 256, Enter = 257, Tab = 258, Backspace = 259,
        Right = 262, Left = 263, Down = 264, Up = 265,
        LeftShift = 340, LeftControl = 341, LeftAlt = 342,
        RightShift = 344, RightControl = 345, RightAlt = 346,
    };

    // Valores = GLFW_MOUSE_BUTTON_* (ver comentario no topo do arquivo).
    enum class MouseButton : int {
        Left = 0,
        Right = 1,
        Middle = 2,
    };

    // Modo de exibicao/captura do cursor - espelha GLFW_CURSOR_NORMAL/
    // HIDDEN/DISABLED (ver glfwSetInputMode na doc do GLFW), exposto sem
    // vazar as macros GLFW cruas para quem usa a API (Normal/Hidden/
    // Disabled sao nomes mais claros para quem nunca mexeu com GLFW
    // diretamente - diferente de Key/MouseButton acima, que reusam os
    // codigos numericos do GLFW por conveniencia, aqui os TRES valores
    // possiveis sao poucos o suficiente para valer a pena um enum
    // proprio, mais legivel em Lua: Input.SetCursorMode(CursorMode.Locked)).
    enum class CursorMode {
        // Cursor normal, visivel, livre - comportamento padrao do SO.
        Normal,
        // Cursor visivel mas "preso" dentro da area da janela (nao pode
        // sair dela) - uso incomum em gameplay, exposto so por completude.
        Hidden,
        // Cursor INVISIVEL e "travado" no centro da janela (o SO nunca
        // deixa o cursor de verdade se mover) - modo padrao de camera
        // FPS/TPS. Com este modo, GetMouseDeltaX/Y (ver abaixo) reportam
        // movimento relativo ILIMITADO (o mouse pode "girar" para sempre,
        // nao esbarra na borda da tela), que e exatamente o que uma
        // camera de olhar ao redor precisa.
        Locked,
    };

    // Classe estatica - sem instancia, mesmo padrao de Renderer/
    // PhysicsEngine/ScriptEngine (ver comentario no topo do arquivo).
    class Input {
    public:
        // Define QUAL janela GLFW as consultas abaixo leem. Deve ser
        // chamado com a GLFWwindow* da PlayWindow ANTES do primeiro
        // OnUpdate() da Scene clonada rodar naquele frame (ver
        // PlayWindow::OnUpdate - chamado logo no inicio, antes de
        // m_PlayScene->OnUpdate()) - assim scripts que consultam Input
        // dentro do proprio OnUpdate() daquele frame ja veem o estado
        // correto. Passar nullptr (ex: ao fechar a PlayWindow, ver
        // PlayWindow::Close) faz toda consulta subsequente retornar
        // false/0 em vez de crashar - nunca ha um "estado de teclado
        // anterior" fantasma sobrevivendo entre uma sessao de Play e a
        // proxima. TAMBEM reresseta o modo do cursor para Normal (ver
        // .cpp) - uma PlayWindow nova nunca deveria herdar o cursor
        // travado de uma sessao de Play anterior.
        static void SetContext(GLFWwindow* window);

        // true enquanto a tecla estiver fisicamente pressionada (igual
        // segurar - GLFW_PRESS ou GLFW_REPEAT). Uso tipico: movimento
        // continuo (ex: "andar para frente enquanto W estiver segurado").
        static bool IsKeyDown(Key key) { return IsKeyDown((int)key); }
        static bool IsKeyDown(int keyCode);

        // true no MOMENTO exato em que a tecla passou de solta para
        // apertada (transicao, nao estado continuo) - uso tipico: pular,
        // atirar, confirmar um menu (acoes que devem acontecer UMA vez por
        // aperto, nao repetir a cada frame que a tecla continua segurada).
        // Implementado comparando o estado deste frame com o estado do
        // frame anterior (ver s_PreviousKeyState) - por isso, diferente de
        // IsKeyDown, o resultado so e valido dentro do mesmo frame em que
        // foi consultado; chamar duas vezes seguidas para a MESMA tecla no
        // mesmo frame ainda retorna o mesmo resultado (o estado anterior
        // so avanca em EndFrame(), nao a cada chamada).
        static bool IsKeyPressed(Key key) { return IsKeyPressed((int)key); }
        static bool IsKeyPressed(int keyCode);

        static bool IsMouseButtonDown(MouseButton button) { return IsMouseButtonDown((int)button); }
        static bool IsMouseButtonDown(int buttonCode);

        // Posicao do cursor em coordenadas de PIXEL da PlayWindow (origem
        // no canto superior esquerdo, mesma convencao do GLFW/da maioria
        // das APIs de janela). Em CursorMode::Locked (ver enum acima),
        // isto fica preso no centro da janela e NAO E UTIL para deteccao
        // de movimento - use GetMouseDeltaX/Y para isso (ver abaixo).
        static float GetMouseX();
        static float GetMouseY();

        // Quanto o mouse se moveu desde o frame anterior (pixels) - o que
        // uma camera FPS/TPS de "olhar ao redor" realmente quer, ao
        // contrario de GetMouseX/Y (posicao absoluta, inutil em
        // CursorMode::Locked, onde a posicao fica presa no centro).
        // Calculado internamente comparando GetMouseX/Y deste frame com o
        // do frame anterior (ver EndFrame) - funciona em QUALQUER
        // CursorMode, mas so faz sentido de verdade em Locked (em Normal/
        // Hidden, o delta para de crescer assim que o cursor de verdade
        // esbarra na borda da tela, o que distorce uma rotacao de camera
        // continua - por isso todo controller de camera FPS/TPS deve
        // chamar SetCursorMode(CursorMode::Locked) primeiro).
        // Valido so dentro do mesmo frame em que foi consultado, mesmo
        // contrato de IsKeyPressed (ver comentario la) - o "frame
        // anterior" usado no calculo so avanca em EndFrame().
        static float GetMouseDeltaX();
        static float GetMouseDeltaY();

        // Define como o cursor do SO se comporta dentro desta janela (ver
        // CursorMode acima) - tipicamente chamado uma vez em OnCreate() de
        // um script de camera FPS/TPS (CursorMode::Locked) e revertido
        // (CursorMode::Normal) quando o jogador abre um menu de pausa/
        // inventario, ou em OnDestroy(). Nao faz nada (retorna
        // silenciosamente) se SetContext ainda nao apontar para uma janela
        // valida (fora do modo Play).
        static void SetCursorMode(CursorMode mode);
        static CursorMode GetCursorMode() { return s_CursorMode; }

        // Chamado uma vez por frame, DEPOIS que toda a logica de
        // gameplay/scripts daquele frame ja rodou (ver PlayWindow::OnUpdate
        // - chamado apos m_PlayScene->OnUpdate()) - avanca o "estado do
        // frame anterior" usado por IsKeyPressed E por
        // GetMouseDeltaX/Y (posicao do mouse deste frame vira "a posicao
        // anterior" para o calculo de delta do PROXIMO frame). Sem isso,
        // IsKeyPressed ficaria preso reportando true enquanto a tecla
        // continuar segurada, e GetMouseDeltaX/Y sempre reportaria a
        // distancia desde ALGUM ponto fixo no passado em vez de "desde o
        // frame anterior".
        static void EndFrame();

    private:
        static GLFWwindow* s_Window;

        // Estado de CADA tecla ja consultada ao menos uma vez, no frame
        // ANTERIOR - usado so por IsKeyPressed para detectar a transicao
        // solta->apertada. Um mapa (nao um array denso 0..GLFW_KEY_LAST)
        // DE PROPOSITO: o espaco de codigos GLFW_KEY_* NAO e denso (ha
        // "buracos" - varios valores inteiros no meio do intervalo nao
        // correspondem a nenhuma tecla real, ver GLFW_KEY_UNKNOWN=-1 e a
        // lista de defines em glfw3.h) - glfwGetKey() com um codigo que
        // nao e um GLFW_KEY_* valido dispara um GLFW_INVALID_ENUM (erro
        // logado, sem crash, mas spamma o console) - um array percorrendo
        // "todo inteiro de 0 a 349" inevitavelmente bate em codigos
        // invalidos no meio do caminho. O mapa evita isso: so guarda
        // (e portanto so consulta em EndFrame) os codigos que
        // IsKeyDown/IsKeyPressed ja usaram pelo menos uma vez - sempre
        // codigos que o proprio chamador (script/C++) forneceu, e que ja
        // passaram por uma chamada bem-sucedida de glfwGetKey antes.
        static std::unordered_map<int, bool> s_PreviousKeyState;

        // Conjunto de codigos de tecla que IsKeyPressed ja consultou pelo
        // menos uma vez desde o ultimo SetContext(nullptr) - EndFrame()
        // usa isto (nao s_PreviousKeyState.keys(), que so e populado POR
        // EndFrame - ver .cpp) para saber quais teclas re-sincronizar,
        // sem nunca chamar glfwGetKey com um codigo que nao veio de uma
        // chamada real de IsKeyPressed.
        static std::unordered_set<int> s_TrackedKeys;

        // Posicao do mouse no frame ANTERIOR (pixels) - usado so para
        // calcular GetMouseDeltaX/Y (ver comentario la). Atualizado em
        // EndFrame(), mesmo padrao temporal de s_PreviousKeyState.
        static float s_PreviousMouseX;
        static float s_PreviousMouseY;

        // true ate a PRIMEIRA vez que EndFrame() roda depois de um
        // SetContext(window) valido - evita um "salto" de delta espurio
        // no primeiro frame de uma sessao de Play (sem isso,
        // s_PreviousMouseX/Y comecariam em 0,0 e o primeiro
        // GetMouseDeltaX/Y reportaria um salto gigante ate a posicao real
        // do cursor, em vez de 0 como deveria ser no primeiro frame).
        static bool s_FirstFrameSinceContext;

        static CursorMode s_CursorMode;
    };

}
