// glad SEMPRE primeiro - mesma regra de ouro do resto da engine (ver
// comentario extenso em Prism/src/Prism/Renderer/OpenGLContext.cpp).
#include <glad/gl.h>
#include "PlayWindow.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>

namespace PrismEditor {

    PlayWindow::~PlayWindow() {
        Close();
    }

    bool PlayWindow::Open(Prism::Ref<Prism::Scene> editorScene, GLFWwindow* sharedContextWindow) {
        if (m_Window) {
            PRISM_CORE_ERROR("PlayWindow::Open chamado com uma janela ja aberta - use Close() primeiro ou IsOpen() para checar.");
            return false;
        }

        // --- Clona a Scene (nao roda a instancia de edicao diretamente) ---
        // Reaproveita o SceneSerializer/formato .prismmap: serializa a
        // Scene de edicao para um arquivo temporario e desserializa de
        // volta numa Scene NOVA - mesmo mecanismo que o antigo snapshot/
        // restore de Play-dentro-da-viewport usava (ver historico), so que
        // aqui o resultado e usado como uma COPIA independente, nunca
        // reescrito de volta na Scene de edicao. E o jeito mais simples de
        // "clonar" corretamente todos os components (incluindo os que tem
        // logica de serializacao propria) sem duplicar esse conhecimento
        // numa segunda funcao de clonagem separada.
        std::error_code ec;
        std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
        if (ec) {
            PRISM_CORE_ERROR("PlayWindow::Open: nao foi possivel acessar a pasta temporaria do sistema: ", ec.message());
            return false;
        }
        std::filesystem::path clonePath = tempDir / ("prism_play_window_clone_" + std::to_string(reinterpret_cast<uintptr_t>(editorScene.get())) + ".prismmap");

        Prism::SceneSerializer writeSerializer(editorScene);
        if (!writeSerializer.Serialize(clonePath)) {
            PRISM_CORE_ERROR("PlayWindow::Open: falha ao clonar a Scene (escrita): ", clonePath.string());
            return false;
        }

        Prism::SceneSerializer readSerializer(Prism::Scene::Create());
        if (!readSerializer.Deserialize(clonePath)) {
            PRISM_CORE_ERROR("PlayWindow::Open: falha ao clonar a Scene (leitura): ", clonePath.string());
            std::filesystem::remove(clonePath, ec);
            return false;
        }
        m_PlayScene = readSerializer.GetScene();

        std::filesystem::remove(clonePath, ec); // nao critico se falhar - arquivo temporario, SO limpa eventualmente

        // --- Cria a janela do SO, com contexto OpenGL COMPARTILHADO ---
        // O ultimo parametro de glfwCreateWindow (sharedContextWindow) e o
        // que faz toda geometria/shaders ja carregados pelo Renderer no
        // contexto do editor (VAOs/VBOs/programs, ver Renderer::Init())
        // ficarem validos tambem nesta janela nova, sem recarregar nada -
        // ver comentario extenso no topo do .h sobre essa decisao.
        //
        // GLFW ja foi inicializado (glfwInit) pela GlfwWindow do editor -
        // nao chamamos glfwInit() de novo aqui, e nao usamos
        // glfwWindowHint(GLFW_CONTEXT_VERSION_*) de novo tambem: hints sao
        // globais e a janela do editor ja configurou 4.5 Core Profile,
        // que continua valendo para esta chamada.
        // Hints explicitos ANTES de glfwCreateWindow - hints sao globais e
        // "grudam" na proxima chamada de glfwCreateWindow, entao mesmo que
        // nada mais no processo tenha mudado GLFW_VISIBLE/GLFW_FOCUSED,
        // deixamos isso explicito aqui em vez de depender do default (o
        // default do GLFW ja e GLFW_TRUE para ambos, mas alguma chamada
        // futura em outro lugar do codigo poderia mudar isso globalmente
        // sem a gente perceber - melhor garantir aqui, no ponto de uso).
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);

        // A PlayWindow deve ser uma janela SOLTA comum (like a debug window
        // da Godot) - nunca fullscreen/maximizada. GLFW_MAXIMIZED default
        // e GLFW_FALSE, mas alguns window managers (principalmente no
        // Windows, quando a ultima janela criada no processo estava
        // maximizada) fazem uma nova janela "herdar" esse estado do lado
        // do SO em vez de respeitar o hint - forcamos explicitamente aqui
        // e chamamos glfwRestoreWindow() logo apos criar, como garantia
        // dupla. GLFW_DECORATED garante borda/titlebar/botoes de
        // fechar-minimizar (sem isso a janela pareceria "fullscreen" por
        // nao ter chrome nenhum, mesmo sem ser maximizada de verdade).
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow((int)m_Width, (int)m_Height, "Prism - Play", nullptr, sharedContextWindow);
        if (!m_Window) {
            PRISM_CORE_ERROR("PlayWindow::Open: glfwCreateWindow falhou.");
            m_PlayScene = nullptr;
            return false;
        }

        glfwSetWindowUserPointer(m_Window, this);
        glfwSetKeyCallback(m_Window, GlfwKeyCallback);
        glfwSetWindowCloseCallback(m_Window, GlfwCloseCallback);
        glfwSetWindowSizeCallback(m_Window, GlfwWindowSizeCallback);

        // Garantia dupla contra o problema de "nasce maximizada" descrito
        // acima - restaura para o tamanho/posicao normais (nao-maximizada,
        // nao-iconificada) mesmo que o SO tenha ignorado os hints.
        glfwRestoreWindow(m_Window);
        glfwSetWindowSize(m_Window, (int)m_Width, (int)m_Height);

        // Forca visibilidade/foco/frente explicitamente - glfwCreateWindow
        // ja deveria fazer isso sozinho (hints acima), mas alguns drivers/
        // window managers (e o proprio Visual Studio anexado como debugger,
        // que as vezes rouba o foco de volta para si assim que a janela e
        // criada) deixam a janela nova criada porem sem foco e atras da
        // janela do editor - o usuario ve "nada abriu" quando na verdade a
        // janela esta la, so escondida atras de outra. Show+Focus explicito
        // corrige isso de forma redundante mas segura (idempotente se a
        // janela ja estava visivel/focada).
        glfwShowWindow(m_Window);
        glfwFocusWindow(m_Window);

        // Precisamos que o contexto desta janela esteja current para
        // configurar o estado inicial dela (glEnable etc - "object
        // sharing" nao inclui estado global de contexto, ver
        // https://www.glfw.org/docs/latest/context_guide.html e
        // comentario no topo do .h). NAO chamamos gladLoadGL() de novo -
        // os ponteiros de funcao carregados pelo primeiro contexto (o do
        // editor) continuam validos aqui, ja que ambos os contextos usam
        // a mesma API/versao (confirmado pelo proprio exemplo oficial
        // glfw/examples/sharing.c: "the contexts are created with the
        // same APIs so the function pointers should be re-usable").
        glfwMakeContextCurrent(m_Window);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Devolve o contexto ativo para a janela do editor - Open() e
        // chamado de dentro do loop do editor, que espera continuar
        // desenhando la logo em seguida.
        glfwMakeContextCurrent(sharedContextWindow);

        m_CloseRequested = false;
        m_LoggedNoCameraWarning = false;

        // Fisica/scripts comecam a rodar imediatamente - Play "de verdade"
        // comeca no instante em que a janela abre, sem um segundo botao
        // separado dentro dela.
        m_PlayScene->OnScriptsStart();

        PRISM_CORE_INFO("PlayWindow: aberta (", m_Width, "x", m_Height, "), Scene clonada com scripts/fisica rodando.");
        return true;
    }

    void PlayWindow::Close() {
        if (!m_Window)
            return; // idempotente

        if (m_PlayScene)
            m_PlayScene->OnScriptsStop();

        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
        m_PlayScene = nullptr;

        PRISM_CORE_INFO("PlayWindow: fechada.");
    }

    void PlayWindow::GlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        // Esc fecha a PlayWindow - convencao comum ("sair do modo Play")
        // em engines com uma janela de jogo separada. So marca a
        // intencao (m_CloseRequested) - a destruicao de verdade acontece
        // no INICIO do proximo OnUpdate(), nunca dentro deste callback
        // (que roda de dentro de glfwPollEvents(), no meio do processamento
        // de eventos desta mesma janela - destruir a janela aqui seria
        // arriscado).
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            PlayWindow* self = (PlayWindow*)glfwGetWindowUserPointer(window);
            if (self) self->m_CloseRequested = true;
        }
    }

    void PlayWindow::GlfwCloseCallback(GLFWwindow* window) {
        // Usuario clicou no X da janela - mesma logica de adiar a
        // destruicao para o proximo OnUpdate(), ver comentario acima.
        PlayWindow* self = (PlayWindow*)glfwGetWindowUserPointer(window);
        if (self) self->m_CloseRequested = true;
    }

    void PlayWindow::GlfwWindowSizeCallback(GLFWwindow* window, int width, int height) {
        PlayWindow* self = (PlayWindow*)glfwGetWindowUserPointer(window);
        if (self && width > 0 && height > 0) {
            self->m_Width = (uint32_t)width;
            self->m_Height = (uint32_t)height;
        }
    }

    void PlayWindow::OnUpdate(float deltaTime, GLFWwindow* editorContextToRestore) {
        if (!m_Window)
            return;

        if (m_CloseRequested) {
            Close();
            return;
        }

        // --- Simulacao (scripts + fisica) da Scene CLONADA -----------------
        m_PlayScene->OnUpdate(deltaTime);

        // --- Desenho ---------------------------------------------------
        // Troca o contexto ativo para o desta janela antes de qualquer
        // chamada GL - "current context" e global por thread no GLFW, nao
        // por janela (ver comentario no topo do .h) - TODA chamada GL
        // entre este ponto e o glfwMakeContextCurrent(editorContextToRestore)
        // no final afeta esta janela, nunca a do editor.
        glfwMakeContextCurrent(m_Window);

        int framebufferWidth, framebufferHeight;
        glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            Prism::Renderer::SetViewport((uint32_t)framebufferWidth, (uint32_t)framebufferHeight);
        }

        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        // Acha a entidade com CameraComponent::Primary=true na Scene
        // CLONADA (nao na Scene de edicao) - mesma busca que
        // EditorLayer::RenderCameraPreview ja fazia, agora usada aqui para
        // a janela de Play de verdade em vez de so uma preview.
        Prism::Entity primaryCameraEntity;
        auto cameraView = m_PlayScene->GetRegistry().view<Prism::TransformComponent, Prism::CameraComponent>();
        for (auto entityHandle : cameraView) {
            auto& camera = cameraView.get<Prism::CameraComponent>(entityHandle);
            if (camera.Primary) {
                primaryCameraEntity = Prism::Entity(entityHandle, m_PlayScene.get());
                break;
            }
        }

        if (primaryCameraEntity) {
            float aspect = framebufferHeight > 0 ? (float)framebufferWidth / (float)framebufferHeight : 1.0f;
            auto& camera = primaryCameraEntity.GetComponent<Prism::CameraComponent>();

            glm::mat4 worldTransform = m_PlayScene->GetWorldTransform(primaryCameraEntity);
            glm::mat4 view = glm::inverse(worldTransform);
            glm::mat4 projection = camera.GetProjection(aspect);
            glm::mat4 viewProjection = projection * view;
            glm::vec3 worldPos = glm::vec3(worldTransform[3]);

            Prism::Renderer::DrawScene(*m_PlayScene, glm::value_ptr(viewProjection), glm::value_ptr(worldPos));
        } else if (!m_LoggedNoCameraWarning) {
            // Log UMA vez so (nao a cada frame) avisando por que a tela
            // fica preta - ajuda a diagnosticar sem inundar o console.
            // Motivos tipicos: nenhuma entidade tem CameraComponent na
            // cena, ou existe uma mas com Primary=false (o default do
            // component e Primary=true - ver Components.h - entao isso so
            // acontece se o usuario desmarcou manualmente no Properties
            // panel, ou se ha mais de uma camera e outra "ganhou").
            uint32_t cameraCount = 0;
            for (auto entityHandle : cameraView) { (void)entityHandle; ++cameraCount; }
            PRISM_CORE_WARN("PlayWindow: nenhuma entidade com CameraComponent::Primary=true encontrada na Scene (",
                cameraCount, " CameraComponent(s) no total) - a tela ficara preta ate existir uma. "
                "Confira o Properties panel da entidade camera.");
            m_LoggedNoCameraWarning = true;
        }
        // Sem CameraComponent::Primary na cena: so o Clear() acima aparece
        // (tela solida) - mesma limitacao que RenderCameraPreview ja tinha
        // (ver EditorLayer.cpp), nao e novo desta janela.

        glfwSwapBuffers(m_Window);

        // NAO chamamos glfwPollEvents() aqui - glfwPollEvents() e GLOBAL
        // para todas as janelas GLFW do processo (nao por janela), e a
        // janela do editor (GlfwWindow::OnUpdate(), chamada uma vez por
        // frame pelo loop principal da Application) ja chama isso, o que
        // processa eventos desta PlayWindow tambem (incluindo os
        // callbacks registrados acima - GlfwKeyCallback/CloseCallback/
        // WindowSizeCallback). Chamar de novo aqui seria redundante (nao
        // incorreto, so inutil - a fila de eventos pendentes ja estaria
        // vazia na segunda chamada do mesmo frame).

        // Restaura o contexto da janela do editor - o chamador (EditorLayer,
        // dentro do loop principal da Application) espera que o contexto
        // ativo continue sendo o dele, ja que vai desenhar a viewport/UI do
        // editor logo em seguida no mesmo frame.
        glfwMakeContextCurrent(editorContextToRestore);
    }

}
