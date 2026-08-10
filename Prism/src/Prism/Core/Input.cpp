// glad ANTES de GLFW - nao porque Input.cpp usa OpenGL diretamente (nao
// usa), mas porque GLFW/glfw3.h faz #include <GL/gl.h> por padrao quando
// nao detecta que um loader de OpenGL customizado (glad, neste caso) ja
// foi incluido antes dele - sem isso, a compilacao falha com "GL/gl.h:
// No such file or directory" neste ambiente (sem o OpenGL de sistema
// instalado, so o glad vendorizado) - mesma regra ja seguida por todo
// outro arquivo da engine que inclui GLFW (ver GlfwWindow.cpp,
// PlayWindow.cpp, comentario identico em OpenGLContext.cpp).
#include <glad/gl.h>
#include "Input.h"
#include <GLFW/glfw3.h>

namespace Prism {

    GLFWwindow* Input::s_Window = nullptr;
    std::unordered_map<int, bool> Input::s_PreviousKeyState;
    std::unordered_set<int> Input::s_TrackedKeys;
    float Input::s_PreviousMouseX = 0.0f;
    float Input::s_PreviousMouseY = 0.0f;
    bool Input::s_FirstFrameSinceContext = true;
    CursorMode Input::s_CursorMode = CursorMode::Normal;

    void Input::SetContext(GLFWwindow* window) {
        s_Window = window;

        // Sempre volta para Normal ao trocar de contexto (nova PlayWindow
        // abrindo, ou nullptr ao fechar) - ver comentario grande no .h.
        // Chama glfwSetInputMode diretamente aqui (nao via
        // SetCursorMode(), ver abaixo) porque SetCursorMode faz o MESMO
        // early-out "if (!s_Window) return" que impediria isto de
        // funcionar no caso window==nullptr (nao ha glfwSetInputMode
        // valido para chamar numa janela nula - o unico "reset" possivel
        // nesse caso e no proprio estado local s_CursorMode).
        s_CursorMode = CursorMode::Normal;
        if (window)
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        // Forca o proximo EndFrame() a nao calcular delta de mouse contra
        // um s_PreviousMouseX/Y desatualizado (de uma sessao de Play
        // anterior, ou so zerado) - ver comentario grande em
        // s_FirstFrameSinceContext, Input.h.
        s_FirstFrameSinceContext = true;
    }

    bool Input::IsKeyDown(int keyCode) {
        // Sem contexto definido (fora do modo Play - ver SetContext) ou
        // codigo fora do intervalo valido de teclado do GLFW: reporta
        // "nao apertada" em vez de crashar. Isso deixa scripts seguros
        // mesmo se chamados num momento inesperado (ex: um OnUpdate que
        // ainda dispara um frame depois do Close() de uma PlayWindow).
        //
        // ATENCAO: keyCode < 0 || keyCode > GLFW_KEY_LAST NAO garante que
        // keyCode seja um GLFW_KEY_* valido - o intervalo tem "buracos"
        // (varios inteiros no meio nao correspondem a tecla nenhuma, ver
        // comentario grande em Input.h sobre s_PreviousKeyState). Esta
        // funcao so e chamada com codigos que o PROPRIO CHAMADOR forneceu
        // (Key::* do C++, ou Key.* da tabela Lua - ver
        // ScriptEngine::RegisterAPI), nunca por um loop interno desta
        // classe - por isso um GLFW_INVALID_ENUM aqui so pode acontecer
        // se o script passar um numero cru invalido de proposito (ex:
        // Input.IsKeyDown(99999)), o que e responsabilidade de quem
        // escreveu o script, nao um bug desta classe.
        if (!s_Window || keyCode < 0 || keyCode > GLFW_KEY_LAST)
            return false;
        int state = glfwGetKey(s_Window, keyCode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsKeyPressed(int keyCode) {
        if (!s_Window || keyCode < 0 || keyCode > GLFW_KEY_LAST)
            return false;
        bool down = IsKeyDown(keyCode);

        // SO LEITURA aqui - nunca escreve em s_PreviousKeyState (quem
        // escreve e exclusivamente EndFrame(), ver la). Critico para
        // chamar IsKeyPressed varias vezes para a MESMA tecla dentro do
        // MESMO frame continuar retornando o mesmo resultado (ver
        // contrato documentado em Input.h) - se esta funcao escrevesse o
        // estado atual a cada chamada, uma segunda chamada no mesmo frame
        // já veria wasDown=down (porque a primeira chamada acabou de
        // gravar isso), fazendo IsKeyPressed "esquecer" a transicao
        // depois da primeira consulta do frame.
        //
        // Uma tecla nunca antes registrada no mapa (nem por IsKeyPressed
        // nem por EndFrame) e tratada como wasDown=false - a tecla so
        // passa a ser rastreada por EndFrame() a partir do PROXIMO frame,
        // apos esta primeira chamada de IsKeyDown() acima ja ter
        // acontecido com um keyCode valido (garantido pelo range check
        // logo no inicio da funcao).
        s_TrackedKeys.insert(keyCode);
        auto it = s_PreviousKeyState.find(keyCode);
        bool wasDown = (it != s_PreviousKeyState.end()) && it->second;

        return down && !wasDown;
    }

    bool Input::IsMouseButtonDown(int buttonCode) {
        if (!s_Window || buttonCode < 0 || buttonCode > GLFW_MOUSE_BUTTON_LAST)
            return false;
        return glfwGetMouseButton(s_Window, buttonCode) == GLFW_PRESS;
    }

    float Input::GetMouseX() {
        if (!s_Window) return 0.0f;
        double x, y;
        glfwGetCursorPos(s_Window, &x, &y);
        return (float)x;
    }

    float Input::GetMouseY() {
        if (!s_Window) return 0.0f;
        double x, y;
        glfwGetCursorPos(s_Window, &x, &y);
        return (float)y;
    }

    float Input::GetMouseDeltaX() {
        if (!s_Window) return 0.0f;
        // Primeiro frame desde SetContext/SetCursorMode (ver comentario
        // grande em s_FirstFrameSinceContext, Input.h) - s_PreviousMouseX
        // ainda nao reflete "o frame anterior de verdade" (EndFrame()
        // ainda nao rodou desde a troca de contexto/modo), entao reporta
        // delta=0 explicitamente em vez de calcular contra um valor
        // desatualizado (0.0 inicial, ou a posicao de ANTES da troca de
        // CursorMode) - o que produziria um salto espurio gigante neste
        // frame.
        if (s_FirstFrameSinceContext) return 0.0f;
        return GetMouseX() - s_PreviousMouseX;
    }

    float Input::GetMouseDeltaY() {
        if (!s_Window) return 0.0f;
        if (s_FirstFrameSinceContext) return 0.0f;
        return GetMouseY() - s_PreviousMouseY;
    }

    void Input::SetCursorMode(CursorMode mode) {
        if (!s_Window)
            return;

        s_CursorMode = mode;
        int glfwValue = GLFW_CURSOR_NORMAL;
        switch (mode) {
            case CursorMode::Normal:   glfwValue = GLFW_CURSOR_NORMAL;   break;
            case CursorMode::Hidden:   glfwValue = GLFW_CURSOR_HIDDEN;   break;
            case CursorMode::Locked:   glfwValue = GLFW_CURSOR_DISABLED; break;
        }
        glfwSetInputMode(s_Window, GLFW_CURSOR, glfwValue);

        // Entrando ou saindo de Locked muda drasticamente o que
        // GetMouseX/Y reportam (GLFW recentraliza/libera o cursor
        // internamente ao trocar o modo) - sem resincronizar
        // s_PreviousMouseX/Y AGORA, o proximo GetMouseDeltaX/Y deste
        // MESMO frame calcularia contra uma posicao "de antes da troca de
        // modo", produzindo um salto espurio de delta exatamente no frame
        // em que o script ligou/desligou o cursor travado. Suprime esse
        // salto tratando este frame como se fosse "o primeiro" (mesmo
        // mecanismo de s_FirstFrameSinceContext, ver SetContext).
        s_FirstFrameSinceContext = true;
    }

    void Input::EndFrame() {
        // Sem contexto (ex: PlayWindow acabou de fechar, SetContext(nullptr)
        // ja foi chamado) - zera todo o estado anterior em vez de deixar
        // "grudado" no ultimo frame de uma sessao de Play anterior, que
        // poderia causar um falso IsKeyPressed=false espurio (achando que
        // uma tecla "ja estava apertada" de uma sessao completamente
        // diferente) se uma nova PlayWindow abrir depois.
        if (!s_Window) {
            s_PreviousKeyState.clear();
            s_TrackedKeys.clear();
            return;
        }

        // Atualiza APENAS as teclas em s_TrackedKeys - ou seja, que
        // IsKeyPressed ja consultou pelo menos uma vez desde que o
        // contexto foi definido (ver comentario grande em Input.h) - NAO
        // varre um intervalo fixo de codigos, o que e exatamente o que
        // causava GLFW_INVALID_ENUM/"Invalid key N" para codigos sem
        // tecla correspondente (bug anterior desta funcao). Toda entrada
        // aqui ja passou por uma chamada valida de glfwGetKey em algum
        // IsKeyDown() anterior (dentro de IsKeyPressed, que e quem
        // adiciona a s_TrackedKeys), entao chamar de novo e sempre seguro.
        for (int keyCode : s_TrackedKeys)
            s_PreviousKeyState[keyCode] = IsKeyDown(keyCode);

        // --- Avanca o "frame anterior" do mouse (para GetMouseDeltaX/Y) ---
        // s_FirstFrameSinceContext so influencia o GetMouseDeltaX/Y deste
        // frame atual (ja calculado ANTES deste EndFrame rodar, ver
        // GetMouseDeltaX/Y - eles leem s_PreviousMouseX/Y do frame
        // anterior, que so e sobrescrito AQUI, no final do frame) - por
        // isso nao ha ramificacao aqui: em ambos os casos (primeiro frame
        // ou nao) a acao e a mesma, so avancar s_PreviousMouseX/Y para a
        // posicao atual. A flag em si so precisa ser derrubada depois de
        // "consumida" por um frame.
        s_PreviousMouseX = GetMouseX();
        s_PreviousMouseY = GetMouseY();
        s_FirstFrameSinceContext = false;
    }

}
