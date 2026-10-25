#include <glad/gl.h>
#include "RuntimeLayer.h"
#include <Prism/Core/Paths.h>
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <string>

namespace PrismRuntime {

    namespace fs = std::filesystem;

    // Aceita o .prismproj ou uma pasta que o contenha. Vazio se nao achar.
    static fs::path FindProjectFile(const fs::path& given) {
        std::error_code ec;
        if (fs::is_regular_file(given, ec))
            return given;
        if (fs::is_directory(given, ec)) {
            for (const auto& entry : fs::directory_iterator(given, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".prismproj")
                    return entry.path();
            }
        }
        return {};
    }

    void RuntimeLayer::Fail(const std::string& reason) {
        PRISM_ERROR("PrismRuntime: ", reason);
        m_Failed = true;
        Prism::Application::Get().Close();
    }

    void RuntimeLayer::OnAttach() {
        // Sem imgui.ini: o jogo nao tem layout de editor para lembrar.
        ImGui::GetIO().IniFilename = nullptr;

        // --- Qual projeto? -------------------------------------------------
        // 1o argumento (pasta ou .prismproj); sem argumento, procura ao lado
        // do executavel e em "Game/" (layout de um jogo distribuido).
        fs::path projectFile;
        bool distributed = false; // layout de jogo exportado: projeto em Game/ ao lado do executavel
        const auto& args = Prism::GetLaunchArguments();
        if (!args.empty()) {
            projectFile = FindProjectFile(fs::path(args[0]));
            if (projectFile.empty()) {
                Fail("nao encontrei um projeto (.prismproj) em '" + args[0] + "'. Uso: PrismRuntime <pasta-do-projeto | arquivo.prismproj>");
                return;
            }
        }
        else {
            fs::path exeDir = Prism::GetExecutableDirectory();
            projectFile = FindProjectFile(exeDir / "Game");
            if (projectFile.empty())
                projectFile = FindProjectFile(exeDir);
            if (projectFile.empty()) {
                Fail("nenhum projeto informado. Uso: PrismRuntime <pasta-do-projeto | arquivo.prismproj>");
                return;
            }
            distributed = true;
        }

        // Jogo exportado (Arquivo > Exportar Jogo no editor): nada recompila shaders, scripts nem o
        // modulo C++ no disco do jogador - desliga o hot reload (que consultaria o disco varias vezes
        // por segundo a toa) e abre o modulo direto de Binaries/, sem copiar para Cache/ (a pasta de
        // instalacao pode ser somente leitura). Com um projeto passado na linha de comando (desenvolvimento)
        // tudo continua como antes.
        if (distributed) {
            Prism::Renderer::SetShaderHotReload(false);
            Prism::ScriptEngine::SetHotReloadEnabled(false);
            Prism::NativeScripting::SetHotReloadEnabled(false);
            Prism::NativeScripting::SetDirectLoad(true);
        }

        Prism::Ref<Prism::Project> project = Prism::Project::Load(projectFile);
        if (!project) {
            Fail("falha ao carregar o projeto '" + projectFile.string() + "'.");
            return;
        }

        // Titulo da janela com o nome do projeto.
        GLFWwindow* window = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();
        glfwSetWindowTitle(window, project->GetConfig().Name.c_str());

        // --- Mapa inicial --------------------------------------------------
        const fs::path& startMap = project->GetConfig().StartMap;
        if (startMap.empty()) {
            Fail("o projeto '" + project->GetConfig().Name + "' nao tem mapa inicial. Abra o projeto no editor e salve um mapa.");
            return;
        }
        fs::path mapPath = project->GetMapDirectory() / startMap;
        Prism::SceneSerializer serializer(Prism::Scene::Create());
        if (!serializer.Deserialize(mapPath)) {
            Fail("falha ao carregar o mapa '" + mapPath.string() + "'.");
            return;
        }
        m_Scene = serializer.GetScene();
        m_Scene->SetSourceMap(startMap.generic_string()); // Scene.Reload() recarrega este mapa

        // Scripts C++ (Binaries/NativeScripts.*): sem modulo compilado nao e erro.
        Prism::NativeScripting::LoadProjectModule(*project);

        // Input ANTES de OnScriptsStart (ver PlayWindow::Open): scripts podem
        // usar Input no OnCreate.
        Prism::Input::SetContext(window);
        Prism::UISystem::ResetInput();

        m_Scene->OnScriptsStart();
        PRISM_INFO("PrismRuntime: '", project->GetConfig().Name, "' rodando (mapa ", startMap.generic_string(), ").");
    }

    void RuntimeLayer::OnDetach() {
        StopGame();
    }

    void RuntimeLayer::StopGame() {
        if (!m_Scene)
            return;
        m_Scene->OnScriptsStop();
        Prism::Input::SetContext(nullptr);
        m_Scene = nullptr;
    }

    void RuntimeLayer::ToggleFullscreen() {
        GLFWwindow* window = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();
        if (!m_Fullscreen) {
            glfwGetWindowPos(window, &m_WindowedX, &m_WindowedY);
            glfwGetWindowSize(window, &m_WindowedW, &m_WindowedH);
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
            if (!mode)
                return;
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            m_Fullscreen = true;
        }
        else {
            glfwSetWindowMonitor(window, nullptr, m_WindowedX, m_WindowedY, m_WindowedW, m_WindowedH, 0);
            m_Fullscreen = false;
        }
    }

    void RuntimeLayer::OnUpdate(float deltaTime) {
        if (m_Failed || !m_Scene)
            return;

        GLFWwindow* window = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();

        // F11 alterna tela cheia (so na transicao solta -> apertada).
        bool f11Down = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
        if (f11Down && !m_F11WasDown)
            ToggleFullscreen();
        m_F11WasDown = f11Down;

        // Limita o passo: uma pausa longa (arrastar a janela, breakpoint) nao
        // pode virar um unico passo gigante de fisica/scripts.
        deltaTime = std::min(deltaTime, 0.1f);

        m_Scene->OnUpdate(deltaTime);
        Prism::Input::EndFrame(); // depois de toda a logica do quadro (ver Input::EndFrame)

        // Scene.Load / Scene.Reload pedidos por scripts: troca a cena depois do quadro.
        Prism::Scene::ApplyPendingLoad(m_Scene);

        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
            return;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        Prism::Renderer::SetViewport((uint32_t)framebufferWidth, (uint32_t)framebufferHeight);

        // A UI in-game usa o tamanho da JANELA (mesma unidade do mouse em Input).
        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        Prism::GameView::Render(*m_Scene, (float)windowWidth, (float)windowHeight, true);
    }

}
