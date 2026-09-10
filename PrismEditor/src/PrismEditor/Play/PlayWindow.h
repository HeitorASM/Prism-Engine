#pragma once

// ============================================================================
// PlayWindow.h
// Janela SEPARADA do sistema operacional (proprio GLFWwindow, nao um painel
// ImGui dockable) que roda o jogo de verdade durante o modo Play - mesmo
// padrao usado por Godot, Unity e Source (uma janela distinta da do editor).
//
// Duas decisoes de design centrais, ambas motivadas por bugs que a
// abordagem antiga (Play dentro da propria viewport do editor, com
// snapshot/restore - ver README) exigia:
//
// 1) A Scene rodada aqui e uma COPIA (Scene::Clone(), em memoria - ver
//    Scene.h/.cpp e PlayWindow::Open) da Scene de edicao, NUNCA a mesma
//    instancia. Isso elimina de vez a necessidade de snapshot/
//    restore e do popup "salvar antes de rodar?": a Scene do editor jamais
//    e tocada por scripts/fisica, entao nao ha nada para desfazer ao
//    fechar a PlayWindow.
//
// 2) O contexto OpenGL desta janela e CRIADO COM SHARE (ultimo parametro de
//    glfwCreateWindow apontando para a janela do editor) - isso significa
//    que toda a geometria/shaders ja carregados pelo Renderer (VAOs/VBOs
//    criados no contexto do editor, ver Renderer::Init()) sao validos
//    tambem aqui, SEM precisar recarregar nada. O preco dessa escolha:
//    "object sharing" nao inclui estado global do contexto (bind atual,
//    viewport, depth test, etc - ver
//    https://www.glfw.org/docs/latest/context_guide.html) - por isso
//    RenderFrame() reconfigura esse estado (glEnable, glViewport) toda vez
//    que troca de contexto via glfwMakeContextCurrent, nao assume que
//    "ja esta configurado" do frame anterior no outro contexto.
// ============================================================================

#include <Prism.h>
#include <glm/glm.hpp>
#include <cstdint>

struct GLFWwindow;

namespace PrismEditor {

    class PlayWindow {
    public:
        PlayWindow() = default;
        ~PlayWindow();

        // Nao copiavel/movivel - dona de um GLFWwindow* e de uma Scene
        // clonada, ambos com identidade unica (destrutor cuida de
        // desalocar os dois). Se precisar "mover" o conceito de Play para
        // outro lugar no futuro, prefira Close() + um novo Open().
        PlayWindow(const PlayWindow&) = delete;
        PlayWindow& operator=(const PlayWindow&) = delete;

        // Clona 'editorScene' (serializa para um arquivo temporario e
        // desserializa de volta - ver .cpp, mesmo mecanismo que o
        // snapshot/restore antigo usava, reaproveitado aqui para "clonar"
        // em vez de "salvar e restaurar") e abre a janela do SO. Chama
        // Scene::OnScriptsStart() na COPIA logo em seguida - fisica/scripts
        // comecam a rodar imediatamente. sharedContextWindow deve ser a
        // GLFWwindow* da janela do editor (ver comentario no topo do
        // arquivo sobre contexto compartilhado). Nao faz nada (retorna
        // false) se ja houver uma PlayWindow aberta nesta instancia - use
        // IsOpen() para checar antes, ou Close() + Open() para reiniciar.
        // Recebe um Ref<Scene> (nao Scene&) de proposito - Scene::Clone()
        // e um metodo de instancia de Scene, que so faz sentido chamado
        // a partir de um Ref<Scene> ja existente (ver uso em Open()).
        bool Open(Prism::Ref<Prism::Scene> editorScene, GLFWwindow* sharedContextWindow);

        // Para scripts/fisica da Scene clonada (Scene::OnScriptsStop()) e
        // destroi a janela do SO e a Scene clonada. Idempotente - chamar
        // sem uma janela aberta nao faz nada. A Scene de EDICAO nunca foi
        // tocada, entao nao ha nada para restaurar (diferente do
        // snapshot/restore antigo).
        void Close();

        bool IsOpen() const { return m_Window != nullptr; }

        // Chamado uma vez por frame pelo dono da PlayWindow (EditorLayer)
        // enquanto IsOpen(). Faz OnUpdate da Scene clonada (scripts +
        // fisica), desenha o frame nesta janela (troca o contexto OpenGL
        // ativo para o desta janela via glfwMakeContextCurrent, desenha, e
        // troca de volta para a janela do editor ao final - ver .cpp) e
        // processa os proprios eventos GLFW desta janela. Se o usuario
        // clicou no X da janela, ou apertou Esc (ver .cpp), fecha a
        // PlayWindow internamente (Close()) e retorna - IsOpen() passa a
        // ser false a partir do proximo frame.
        void OnUpdate(float deltaTime, GLFWwindow* editorContextToRestore);

    private:
        GLFWwindow* m_Window = nullptr;
        Prism::Ref<Prism::Scene> m_PlayScene;

        // Tamanho inicial da janela de Play - fixo por enquanto (sem
        // configuracao de resolucao na UI ainda, ver README "Proximos
        // passos"). Atualizado se o usuario redimensionar a janela do SO
        // manualmente (ver GlfwWindowSizeCallback no .cpp).
        uint32_t m_Width = 1280;
        uint32_t m_Height = 720;

        static void GlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void GlfwCloseCallback(GLFWwindow* window);
        static void GlfwWindowSizeCallback(GLFWwindow* window, int width, int height);

        // true assim que o usuario pede para fechar (X da janela ou Esc) -
        // consumido no INICIO do proximo OnUpdate() (nao dentro do proprio
        // callback GLFW, que roda em glfwPollEvents() e nao deveria
        // destruir a janela/contexto que esta processando no meio do
        // proprio evento).
        bool m_CloseRequested = false;

        // Garante que o aviso de "sem camera Primary" (ver OnUpdate) so
        // loga uma vez por sessao de Play, nao a cada frame.
        bool m_LoggedNoCameraWarning = false;
    };

}
