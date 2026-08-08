#include "EditorLayer.h"
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace PrismEditor {

    // ID do popup modal de "Salvar Como" - compartilhado entre
    // RenderSaveAsPopup() (que o abre/desenha) e o atalho Ctrl+S/Ctrl+Shift+S
    // em RenderDockspace() (que precisa saber se ja esta aberto, para nao
    // tentar abrir de novo por cima de si mesmo).
    static constexpr const char* kSaveAsPopupId = "Salvar Mapa Como";

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    void EditorLayer::OnAttach() {
        PRISM_INFO("EditorLayer anexada. Projeto ativo: ",
            Prism::Project::GetActive()->GetConfig().Name);

        Prism::FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_ViewportFramebuffer = Prism::Framebuffer::Create(fbSpec);

        m_ContentBrowser.ResetToProjectRoot();
        m_ContentBrowser.SetOnMapDoubleClicked([this](const std::filesystem::path& mapPath) {
            LoadScene(mapPath);
        });

        LoadOrCreateScene();
    }

    bool EditorLayer::LoadScene(const std::filesystem::path& mapPath) {
        Prism::SceneSerializer serializer(Prism::Scene::Create());
        if (!serializer.Deserialize(mapPath)) {
            PRISM_ERROR("Falha ao carregar o mapa: ", mapPath.string());
            return false;
        }

        m_ActiveScene = serializer.GetScene();
        m_CurrentMapPath = mapPath;
        m_SelectedEntity = {};
        m_CommandHistory.Clear();
        return true;
    }

    void EditorLayer::NewMap() {
        // TODO: quando existir rastreamento de "alteracoes nao salvas"
        // (dirty flag), perguntar aqui antes de descartar a cena atual -
        // por ora, Novo Mapa descarta sem aviso, igual acontecia ao
        // carregar outro mapa pelo Content Browser (ver nota no README).
        m_ActiveScene = Prism::Scene::Create("Nova Cena");
        m_CurrentMapPath.clear(); // sem arquivo associado ainda - "Salvar Mapa" vai se comportar como "Salvar Como"
        m_SelectedEntity = {};
        m_CommandHistory.Clear();

        PRISM_INFO("Novo mapa criado (ainda nao salvo).");
    }

    void EditorLayer::LoadOrCreateScene() {
        auto project = Prism::Project::GetActive();
        const auto& startMap = project->GetConfig().StartMap;

        // Historico de undo/redo e por definicao amarrado a UMA Scene em
        // memoria - trocar a Scene (carregando um mapa do disco) sem
        // limpar o historico deixaria Undo() tentando desfazer acoes sobre
        // entidades que nao existem mais na nova Scene. LoadScene() ja faz
        // isso; aqui so garantimos o mesmo comportamento no caminho de
        // "criar cena de exemplo" abaixo.
        m_CommandHistory.Clear();

        if (!startMap.empty()) {
            std::filesystem::path mapPath = project->GetMapDirectory() / startMap;
            if (std::filesystem::exists(mapPath)) {
                if (LoadScene(mapPath))
                    return;
                PRISM_WARN("Falha ao carregar '", mapPath.string(), "' - criando cena de exemplo em memoria.");
            }
        }

        // Projeto novo (ou StartMap ainda nao definido/nao encontrado):
        // cena de exemplo em memoria, igual antes de existir persistencia.
        // "Salvar Mapa"/"Salvar Como" (ver abaixo) e o que grava isso no
        // disco - m_CurrentMapPath fica vazio ate la.
        m_ActiveScene = Prism::Scene::Create("Cena de exemplo");
        m_CurrentMapPath.clear();

        Prism::Entity cube = m_ActiveScene->CreateEntity("Cubo");
        cube.GetComponent<Prism::TransformComponent>().Translation = { -1.2f, 0.0f, 0.0f };
        cube.AddComponent<Prism::MeshRendererComponent>();

        Prism::Entity cube2 = m_ActiveScene->CreateEntity("Cubo (filho conceitual)");
        auto& t2 = cube2.GetComponent<Prism::TransformComponent>();
        t2.Translation = { 1.4f, 0.3f, 0.0f };
        t2.Scale = { 0.6f, 0.6f, 0.6f };
        auto& mesh2 = cube2.AddComponent<Prism::MeshRendererComponent>();
        mesh2.Color = { 0.3f, 0.6f, 0.9f };

        m_SelectedEntity = cube;
    }

    // Helper interno (nao declarado no .h) - escreve m_ActiveScene em
    // 'mapPath' de fato, sem se importar com "e primeiro save?" ou popups.
    // SaveActiveScene()/SaveActiveSceneAs() decidem QUAL caminho usar;
    // este helper so faz a escrita e retorna se deu certo.
    static bool WriteSceneFile(Prism::Ref<Prism::Scene> scene, const std::filesystem::path& mapPath) {
        std::error_code ec;
        std::filesystem::create_directories(mapPath.parent_path(), ec);

        Prism::SceneSerializer serializer(scene);
        if (!serializer.Serialize(mapPath)) {
            PRISM_ERROR("Falha ao salvar o mapa em: ", mapPath.string());
            return false;
        }

        PRISM_INFO("Mapa salvo: ", mapPath.string());
        return true;
    }

    void EditorLayer::SaveActiveScene() {
        if (m_CurrentMapPath.empty()) {
            // Cena sem arquivo associado ainda (nova, ou criada por
            // NewMap()) - nao ha "onde" sobrescrever, entao pedimos um
            // nome, exatamente como Salvar Como faria.
            SaveActiveSceneAs();
            return;
        }

        WriteSceneFile(m_ActiveScene, m_CurrentMapPath);
    }

    void EditorLayer::SaveActiveSceneAs() {
        // So abre o popup - a escrita de fato acontece em
        // RenderSaveAsPopup() quando o usuario confirma o nome, porque
        // ImGui::OpenPopup precisa ser chamado durante o ciclo normal de
        // render (ver RenderDockspace(), que chama RenderSaveAsPopup() a
        // cada frame independente do popup estar aberto ou nao).
        std::string suggested = m_ActiveScene->GetName();
        std::snprintf(m_SaveAsNameBuffer, sizeof(m_SaveAsNameBuffer), "%s", suggested.c_str());
        m_ShowSaveAsPopup = true;
    }

    void EditorLayer::RenderSaveAsPopup() {
        if (m_ShowSaveAsPopup) {
            ImGui::OpenPopup(kSaveAsPopupId);
            m_ShowSaveAsPopup = false; // OpenPopup so precisa ser chamado uma vez, no frame em que o popup deve abrir
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kSaveAsPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Nome do mapa:");
            ImGui::SetNextItemWidth(-1);

            bool confirmedByEnter = ImGui::InputText("##SaveAsName", m_SaveAsNameBuffer, sizeof(m_SaveAsNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            auto project = Prism::Project::GetActive();
            std::string name = m_SaveAsNameBuffer;

            // Sanitizacao minima: caracteres proibidos em nomes de arquivo
            // no Windows (e problematicos em qualquer SO) viram underscore.
            // Sem isso, um nome como "Meu Mapa: Final?" geraria um
            // std::filesystem::path invalido e Serialize() falharia com um
            // erro criptico em vez de simplesmente funcionar com um nome
            // sensato.
            static const std::string kForbiddenChars = "/\\:*?\"<>|";
            for (auto& c : name)
                if (kForbiddenChars.find(c) != std::string::npos)
                    c = '_';

            bool nameEmpty = name.empty();

            // Preview do caminho final - ajuda o usuario a perceber ANTES
            // de confirmar se vai sobrescrever um mapa existente (mesmo
            // nome de um arquivo .prismmap ja presente na pasta Maps/).
            std::filesystem::path previewPath = project->GetMapDirectory() / (name + ".prismmap");
            bool wouldOverwrite = !nameEmpty && std::filesystem::exists(previewPath);

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o mapa.");
            } else if (wouldOverwrite) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Ja existe um mapa com este nome - sera sobrescrito.");
            } else {
                ImGui::TextDisabled("%s", previewPath.filename().string().c_str());
            }

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Salvar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty;

            if (confirmed) {
                if (WriteSceneFile(m_ActiveScene, previewPath)) {
                    m_CurrentMapPath = previewPath;
                    m_ActiveScene->SetName(name);

                    // Novo mapa salvo vira o StartMap do projeto - assim a
                    // proxima vez que o editor abrir, reabre este mapa
                    // (mesmo comportamento que ja existia para o primeiro
                    // save de um projeto, agora tambem valido para
                    // qualquer Salvar Como subsequente).
                    std::filesystem::path relativeToMapDir = std::filesystem::relative(previewPath, project->GetMapDirectory());
                    Prism::Project::SetStartMap(relativeToMapDir);
                }
                ImGui::CloseCurrentPopup();
            } else if (cancelled) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void EditorLayer::OnDetach() {}

    void EditorLayer::OnUpdate(float deltaTime) {
        // Redimensiona o framebuffer se o painel Viewport mudou de tamanho
        // desde o ultimo frame (arrastar a janela, dockar/desdockar, etc).
        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        if (m_ViewportSize[0] > 0.0f && m_ViewportSize[1] > 0.0f &&
            (spec.Width != (uint32_t)m_ViewportSize[0] || spec.Height != (uint32_t)m_ViewportSize[1])) {
            m_ViewportFramebuffer->Resize((uint32_t)m_ViewportSize[0], (uint32_t)m_ViewportSize[1]);
        }

        m_ActiveScene->OnUpdate(deltaTime);

        RenderScene(deltaTime);

        // Resize do framebuffer de preview segue o mesmo padrao do
        // m_ViewportFramebuffer acima - so que usando o tamanho do painel
        // Camera (m_CameraPreviewSize), setado por RenderCameraPreviewPanel().
        if (m_CameraPreviewFramebuffer) {
            const auto& previewSpec = m_CameraPreviewFramebuffer->GetSpecification();
            if (m_CameraPreviewSize[0] > 0.0f && m_CameraPreviewSize[1] > 0.0f &&
                (previewSpec.Width != (uint32_t)m_CameraPreviewSize[0] || previewSpec.Height != (uint32_t)m_CameraPreviewSize[1])) {
                m_CameraPreviewFramebuffer->Resize((uint32_t)m_CameraPreviewSize[0], (uint32_t)m_CameraPreviewSize[1]);
            }
        }
        RenderCameraPreview(deltaTime);
    }

    void EditorLayer::RenderScene(float deltaTime) {
        m_ViewportFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        float aspect = spec.Height > 0 ? (float)spec.Width / (float)spec.Height : 1.0f;

        // A viewport principal do editor usa SEMPRE a camera de orbita
        // livre - nunca a CameraComponent::Primary da cena, mesmo que
        // exista uma. Ver o comentario em m_CameraYaw (EditorLayer.h) e em
        // RenderCameraPreviewPanel() para o motivo: substituir a viewport
        // principal pela camera de jogo te deixa "preso" dentro de
        // qualquer mesh onde a camera esteja posicionada (ex: dentro da
        // capsula de colisao de um character), sem visao de trabalho para
        // corrigir isso. A camera de jogo tem sua propria preview separada
        // (RenderCameraPreview/m_CameraPreviewFramebuffer).
        float yawRad = glm::radians(m_CameraYaw);
        float pitchRad = glm::radians(m_CameraPitch);
        glm::vec3 cameraPos;
        cameraPos.x = m_CameraDistance * cosf(pitchRad) * cosf(yawRad);
        cameraPos.y = m_CameraDistance * sinf(pitchRad);
        cameraPos.z = m_CameraDistance * cosf(pitchRad) * sinf(yawRad);

        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        glm::mat4 viewProjection = projection * view;

        // Necessario ANTES de qualquer DrawMesh() deste framebuffer - ver
        // comentario em Renderer::SetCameraPosition (Renderer.h) sobre o
        // teste de "face interna transparente".
        Prism::Renderer::SetCameraPosition(glm::value_ptr(cameraPos));

        RenderSceneEntities(viewProjection);
        RenderCameraGizmos(viewProjection);
        RenderSelectedColliderGizmo(viewProjection);

        m_ViewportFramebuffer->Unbind();
    }

    void EditorLayer::RenderCameraPreview(float deltaTime) {
        // Sem framebuffer ainda (painel Camera nunca foi aberto) - nada a
        // fazer. Criado sob demanda em RenderCameraPreviewPanel().
        if (!m_CameraPreviewFramebuffer)
            return;

        m_CameraPreviewFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        // Acha a entidade com CameraComponent::Primary=true - mesma busca
        // que RenderScene() fazia antes desta mudanca (agora vive so aqui,
        // ja que so a preview usa a camera de jogo).
        Prism::Entity primaryCameraEntity;
        auto cameraView = m_ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::CameraComponent>();
        for (auto entityHandle : cameraView) {
            auto [transform, camera] = cameraView.get<Prism::TransformComponent, Prism::CameraComponent>(entityHandle);
            if (camera.Primary) {
                primaryCameraEntity = Prism::Entity(entityHandle, m_ActiveScene.get());
                break; // so a primeira Primary encontrada conta - SetPrimaryCamera() ja garante que so existe uma
            }
        }

        if (primaryCameraEntity) {
            const auto& spec = m_CameraPreviewFramebuffer->GetSpecification();
            float aspect = spec.Height > 0 ? (float)spec.Width / (float)spec.Height : 1.0f;

            auto& camera = primaryCameraEntity.GetComponent<Prism::CameraComponent>();

            // View da camera de jogo: inversa da matriz de MUNDO da
            // entidade (ancestrais inclusos, via GetWorldTransform - ver
            // parenting em Scene.h; ex: uma camera filha de um Character
            // acompanha o pai). GetWorldTransform ja inclui Scale, que nao
            // faz sentido para uma camera, mas cameras tipicamente ficam
            // com Scale=1 em toda a cadeia de ancestrais (o preset de
            // criacao garante isso na propria entidade), entao nao e um
            // problema na pratica.
            glm::mat4 worldTransform = m_ActiveScene->GetWorldTransform(primaryCameraEntity);
            glm::mat4 view = glm::inverse(worldTransform);
            glm::mat4 projection = camera.GetProjection(aspect);
            glm::mat4 viewProjection = projection * view;

            // Necessario ANTES de qualquer DrawMesh() deste framebuffer -
            // ver comentario em Renderer::SetCameraPosition (Renderer.h).
            // E EXATAMENTE este teste que resolve o problema original de
            // uma CameraComponent posicionada dentro de outro mesh (ex: a
            // capsula de colisao de um Character): a face interna do mesh
            // que a envolve fica transparente na preview, entao a camera
            // enxerga o resto da cena em vez de uma parede solida.
            glm::vec3 worldPos = glm::vec3(worldTransform[3]);
            Prism::Renderer::SetCameraPosition(glm::value_ptr(worldPos));

            RenderSceneEntities(viewProjection);
            // Nao chama RenderCameraGizmos aqui de proposito - a propria
            // camera nao deve desenhar o frustum dela mesma dentro da sua
            // propria preview (ficaria com a geometria do gizmo colada na
            // tela toda, ja que a camera esta dentro do proprio frustum).
        }
        // Sem Primary: framebuffer fica so com o Clear acima (fundo escuro
        // solido) - RenderCameraPreviewPanel() mostra uma mensagem de texto
        // em cima dessa imagem vazia, explicando que nenhuma camera foi
        // marcada como Primary ainda.

        m_CameraPreviewFramebuffer->Unbind();
    }

    void EditorLayer::RenderSceneEntities(const glm::mat4& viewProjection) {
        // Desenha TODA entidade da cena que tenha Transform + MeshRenderer -
        // isto e o "sistema de renderizacao" no sentido ECS: uma funcao que
        // itera sobre o conjunto de components relevantes, sem saber nada
        // sobre quantas entidades existem ou o que cada uma "e" alem disso.
        // Compartilhada por RenderScene() (viewport principal) e
        // RenderCameraPreview() (preview da camera de jogo) - so muda a
        // matriz view/projection recebida.
        auto view_ = m_ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::MeshRendererComponent>();
        for (auto entityHandle : view_) {
            auto& meshRenderer = view_.get<Prism::MeshRendererComponent>(entityHandle);

            // GetWorldTransform (nao TransformComponent::GetTransform() sozinho)
            // combina o transform local desta entidade com o de todos os
            // ancestrais (ver RelationshipComponent/parenting em Scene.h) -
            // e assim que mover um pai move os filhos junto na viewport.
            glm::mat4 model = m_ActiveScene->GetWorldTransform(Prism::Entity(entityHandle, m_ActiveScene.get()));
            glm::vec3 color = meshRenderer.Color;

            // DrawMesh() ja sabe desenhar qualquer PrimitiveMesh (Cube,
            // Sphere, Capsule, Cylinder, Plane - ver Renderer.h/.cpp) - nao
            // precisamos mais de um switch aqui, so repassar o tipo.
            Prism::Renderer::DrawMesh(meshRenderer.Mesh, glm::value_ptr(viewProjection), glm::value_ptr(model), glm::value_ptr(color));
        }
    }

    void EditorLayer::RenderCameraGizmos(const glm::mat4& viewProjection) {
        // Desenha um frustum simples (piramide com base retangular, apice
        // na posicao da camera) para toda entidade com CameraComponent -
        // sem isso, uma camera seria invisivel na viewport (ela nao tem
        // MeshRendererComponent). So Perspective desenha um frustum de
        // verdade (leque abrindo do apice); Orthographic desenha uma caixa
        // (os planos near/far tem o mesmo tamanho, sem convergencia).
        auto view = m_ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::CameraComponent>();
        for (auto entityHandle : view) {
            auto& camera = view.get<Prism::CameraComponent>(entityHandle);

            // Tamanho fixo de exibicao (nao o Far real da camera, que pode
            // ser gigante e tornar o gizmo inutilizavel visualmente) - so
            // near/far "curtos" para dar a nocao de direcao/abertura.
            constexpr float kGizmoNear = 0.15f;
            constexpr float kGizmoFar = 0.6f;

            float nearHalfHeight, nearHalfWidth, farHalfHeight, farHalfWidth;
            // Aspect fixo 16:9 para o gizmo, independente do aspect real do
            // viewport de destino - o gizmo e so uma indicacao visual de
            // "aqui existe uma camera olhando nesta direcao", nao uma
            // preview exata do frustum (isso ficaria caro/complexo de
            // manter em sincronia com o Framebuffer real).
            constexpr float kGizmoAspect = 16.0f / 9.0f;

            if (camera.ProjectionType == Prism::CameraProjectionType::Orthographic) {
                float halfHeight = camera.OrthoSize * 0.5f * 0.15f; // escalado para o mesmo tamanho visual do frustum perspective
                nearHalfHeight = farHalfHeight = halfHeight;
                nearHalfWidth = farHalfWidth = halfHeight * kGizmoAspect;
            } else {
                float tanHalfFov = tanf(glm::radians(camera.FOV) * 0.5f);
                nearHalfHeight = kGizmoNear * tanHalfFov;
                nearHalfWidth = nearHalfHeight * kGizmoAspect;
                farHalfHeight = kGizmoFar * tanHalfFov;
                farHalfWidth = farHalfHeight * kGizmoAspect;
            }

            glm::mat4 model = m_ActiveScene->GetWorldTransform(Prism::Entity(entityHandle, m_ActiveScene.get()));
            // Camera olha para -Z local (convencao padrao de camera em
            // OpenGL/glm, igual view = glm::inverse(model) em RenderScene()
            // assume implicitamente).
            auto toWorld = [&](float x, float y, float z) {
                return glm::vec3(model * glm::vec4(x, y, -z, 1.0f));
            };

            glm::vec3 apex = toWorld(0, 0, 0);
            glm::vec3 nearTL = toWorld(-nearHalfWidth,  nearHalfHeight, kGizmoNear);
            glm::vec3 nearTR = toWorld( nearHalfWidth,  nearHalfHeight, kGizmoNear);
            glm::vec3 nearBL = toWorld(-nearHalfWidth, -nearHalfHeight, kGizmoNear);
            glm::vec3 nearBR = toWorld( nearHalfWidth, -nearHalfHeight, kGizmoNear);
            glm::vec3 farTL  = toWorld(-farHalfWidth,   farHalfHeight,  kGizmoFar);
            glm::vec3 farTR  = toWorld( farHalfWidth,   farHalfHeight,  kGizmoFar);
            glm::vec3 farBL  = toWorld(-farHalfWidth,  -farHalfHeight,  kGizmoFar);
            glm::vec3 farBR  = toWorld( farHalfWidth,  -farHalfHeight,  kGizmoFar);

            // 8 segmentos: retangulo near, retangulo far, 4 arestas
            // conectando near->far (para Perspective isso converge para o
            // apice se near for pequeno o bastante - aqui so desenhamos o
            // retangulo near normalmente, ja que kGizmoNear > 0).
            std::vector<glm::vec3> points = {
                nearTL, nearTR,  nearTR, nearBR,  nearBR, nearBL,  nearBL, nearTL, // retangulo near
                farTL, farTR,    farTR, farBR,    farBR, farBL,    farBL, farTL,   // retangulo far
                nearTL, farTL,   nearTR, farTR,   nearBR, farBR,   nearBL, farBL,  // arestas conectando
                apex, nearTL,    apex, nearTR,    apex, nearBR,    apex, nearBL,   // apice ao retangulo near (indica a origem/posicao da camera)
            };

            // Primary usa uma cor diferente (ciano) das demais (cinza) -
            // ajuda a identificar de relance qual camera o modo Play vai
            // usar quando ha mais de uma na cena.
            glm::vec3 color = camera.Primary ? glm::vec3(0.25f, 0.85f, 0.95f) : glm::vec3(0.6f, 0.6f, 0.6f);

            Prism::Renderer::DrawLines(glm::value_ptr(points[0]), (uint32_t)points.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
        }
    }

    void EditorLayer::RenderSelectedColliderGizmo(const glm::mat4& viewProjection) {
        // So desenha se a entidade selecionada tiver Transform + Collider -
        // sem selecao (m_SelectedEntity invalida) ou sem ColliderComponent,
        // nao ha nada a fazer.
        if (!m_SelectedEntity || !m_SelectedEntity.HasComponent<Prism::ColliderComponent>())
            return;

        auto& collider = m_SelectedEntity.GetComponent<Prism::ColliderComponent>();

        // Gizmo do collider usa a matriz de MUNDO completa da entidade
        // (ancestrais inclusos, via GetWorldTransform - ver parenting em
        // Scene.h) - mesmo approach usado por RenderCameraGizmos acima. O
        // TAMANHO "base" da forma de colisao vem de collider.Size; se a
        // entidade (ou algum ancestral) tiver Scale != 1, o gizmo escala
        // junto (reflete o que a fisica de verdade faria depois, quando
        // Box3D estiver integrado - Box3D tambem aplica a escala do corpo
        // aos shapes).
        glm::mat4 worldTransform = m_ActiveScene->GetWorldTransform(m_SelectedEntity);
        auto toWorld = [&](const glm::vec3& local) {
            return glm::vec3(worldTransform * glm::vec4(local, 1.0f));
        };

        // Amarelo: convencao comum de "gizmo de colisao selecionado" (Unity
        // usa verde-claro, Unreal usa laranja/vermelho, Godot usa um roxo
        // claro - amarelo aqui so para ficar bem distinto do ciano/cinza ja
        // usados pelas cameras, ver RenderCameraGizmos acima).
        glm::vec3 color(0.95f, 0.85f, 0.2f);

        // Gera os pontos (pares consecutivos = segmentos, ver DrawLines) de
        // um circulo de raio 'radius' no plano perpendicular a 'axis' (0=X,
        // 1=Y, 2=Z), centrado em 'center' (espaco local, antes de toWorld),
        // com 'segments' segmentos - usado tanto para Sphere (3 circulos
        // ortogonais) quanto para as tampas da Capsule.
        auto appendCircle = [&](std::vector<glm::vec3>& points, glm::vec3 center, float radius, int axis, int segments) {
            glm::vec3 prev;
            for (int i = 0; i <= segments; i++) {
                float t = (float)i / (float)segments * 2.0f * 3.14159265f;
                float c = radius * cosf(t);
                float s = radius * sinf(t);
                glm::vec3 p = center;
                if (axis == 0)      p += glm::vec3(0.0f, c, s); // circulo no plano YZ (perpendicular a X)
                else if (axis == 1) p += glm::vec3(c, 0.0f, s); // circulo no plano XZ (perpendicular a Y)
                else                p += glm::vec3(c, s, 0.0f); // circulo no plano XY (perpendicular a Z)

                if (i > 0) { points.push_back(prev); points.push_back(p); }
                prev = p;
            }
        };

        // Gera um arco de 180 graus (meio-circulo) de raio 'radius',
        // centrado em 'center', comecando na direcao 'startAxis' e
        // terminando na direcao 'endAxis' (dois eixos ortogonais entre si -
        // ex: startAxis=(1,0,0), endAxis=(0,1,0) desenha o quarto de volta
        // de +X ate +Y, e o proximo quarto de +Y ate -X, completando meia
        // volta). Usado para as calotas hemisfericas da Capsule (2 arcos
        // por calota = uma "cruz" de meridianos, dando a nocao de cupula
        // sem precisar de uma malha completa).
        auto appendArc = [&](std::vector<glm::vec3>& points, glm::vec3 center, float radius, glm::vec3 startAxis, glm::vec3 endAxis, int segments) {
            glm::vec3 prev;
            for (int i = 0; i <= segments; i++) {
                float t = (float)i / (float)segments * 3.14159265f; // 0 .. PI (meia volta)
                glm::vec3 p = center + radius * (startAxis * cosf(t) + endAxis * sinf(t));
                if (i > 0) { points.push_back(prev); points.push_back(p); }
                prev = p;
            }
        };

        std::vector<glm::vec3> localPoints;
        constexpr int kCircleSegments = 24;
        constexpr int kArcSegments = 12;

        switch (collider.Shape) {
            case Prism::ColliderShape::Box: {
                // Size e ja meio-extensao (half-extents) - ver comentario em
                // ColliderComponent (Components.h).
                glm::vec3 e = collider.Size;
                glm::vec3 c[8] = {
                    { -e.x,-e.y,-e.z }, {  e.x,-e.y,-e.z }, {  e.x, e.y,-e.z }, { -e.x, e.y,-e.z }, // face -Z
                    { -e.x,-e.y, e.z }, {  e.x,-e.y, e.z }, {  e.x, e.y, e.z }, { -e.x, e.y, e.z }, // face +Z
                };
                int edges[12][2] = {
                    {0,1},{1,2},{2,3},{3,0}, // face -Z
                    {4,5},{5,6},{6,7},{7,4}, // face +Z
                    {0,4},{1,5},{2,6},{3,7}, // arestas conectando as duas faces
                };
                for (auto& e2 : edges) { localPoints.push_back(c[e2[0]]); localPoints.push_back(c[e2[1]]); }
                break;
            }
            case Prism::ColliderShape::Sphere: {
                float r = collider.Size.x; // so Size.x e usado como raio, ver ColliderComponent
                appendCircle(localPoints, glm::vec3(0.0f), r, 0, kCircleSegments);
                appendCircle(localPoints, glm::vec3(0.0f), r, 1, kCircleSegments);
                appendCircle(localPoints, glm::vec3(0.0f), r, 2, kCircleSegments);
                break;
            }
            case Prism::ColliderShape::Capsule: {
                // Size.x = raio, Size.y = altura TOTAL da capsula, incluindo
                // as duas calotas hemisfericas (Size.z ignorado - ver
                // ColliderComponent). O "cilindro" do meio vai de
                // -halfCylinderHeight a +halfCylinderHeight; cada calota e
                // uma hemisfera de raio 'radius' colada em cada ponta,
                // desenhada com 2 arcos de meridiano (planos XY e ZY) + o
                // equador (reaproveitando appendCircle) - suficiente para
                // ler "isto e uma capsula, nao um cilindro" de relance, sem
                // precisar de uma malha completa de esfera.
                float radius = collider.Size.x;
                float halfCylinderHeight = std::max(collider.Size.y * 0.5f - radius, 0.0f); // metade da parte cilindrica, descontando as 2 calotas de raio 'radius'

                // Equador do cilindro (topo e base da parte reta).
                appendCircle(localPoints, glm::vec3(0.0f, halfCylinderHeight, 0.0f), radius, 1, kCircleSegments);
                appendCircle(localPoints, glm::vec3(0.0f, -halfCylinderHeight, 0.0f), radius, 1, kCircleSegments);

                // Calota de cima: hemisferio acima de y=halfCylinderHeight,
                // desenhado como 2 meridianos de 180 graus (de +X a +Y, e
                // de +Z a +Y) - a metade "de cima" do arco (de 0 a PI/2 ja
                // cobre o quarto que importa, mas usar o arco completo de
                // +X/+Z ate -X/-Z passando por +Y da a cupula inteira numa
                // linha so por meridiano).
                glm::vec3 topCenter(0.0f, halfCylinderHeight, 0.0f);
                appendArc(localPoints, topCenter, radius, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), kArcSegments);
                appendArc(localPoints, topCenter, radius, glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), kArcSegments);
                appendArc(localPoints, topCenter, radius, glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), kArcSegments);
                appendArc(localPoints, topCenter, radius, glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), kArcSegments);

                // Calota de baixo: espelhada (aponta para -Y em vez de +Y).
                glm::vec3 bottomCenter(0.0f, -halfCylinderHeight, 0.0f);
                appendArc(localPoints, bottomCenter, radius, glm::vec3(1, 0, 0), glm::vec3(0, -1, 0), kArcSegments);
                appendArc(localPoints, bottomCenter, radius, glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0), kArcSegments);
                appendArc(localPoints, bottomCenter, radius, glm::vec3(0, 0, 1), glm::vec3(0, -1, 0), kArcSegments);
                appendArc(localPoints, bottomCenter, radius, glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), kArcSegments);

                // 4 linhas verticais ao redor do cilindro (nas direcoes
                // +X/-X/+Z/-Z) conectando o equador de cima ao de baixo -
                // sem essas, as duas calotas + equadores pareceriam 2
                // esferas soltas em vez de uma capsula conectada.
                glm::vec3 dirs[4] = { {radius,0,0}, {-radius,0,0}, {0,0,radius}, {0,0,-radius} };
                for (auto& d : dirs) {
                    localPoints.push_back(topCenter + d);
                    localPoints.push_back(bottomCenter + d);
                }
                break;
            }
        }

        std::vector<glm::vec3> worldPoints;
        worldPoints.reserve(localPoints.size());
        for (auto& p : localPoints)
            worldPoints.push_back(toWorld(p));

        if (!worldPoints.empty())
            Prism::Renderer::DrawLines(glm::value_ptr(worldPoints[0]), (uint32_t)worldPoints.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
    }

    void EditorLayer::SetPrimaryCamera(Prism::Entity newPrimary) {
        auto view = m_ActiveScene->GetRegistry().view<Prism::CameraComponent>();
        for (auto entityHandle : view) {
            Prism::Entity entity(entityHandle, m_ActiveScene.get());
            entity.GetComponent<Prism::CameraComponent>().Primary = (entity == newPrimary);
        }
    }

    void EditorLayer::OnEvent(Prism::Event& event) {
        // O controle de camera propriamente dito (arrastar com botao
        // direito) e tratado em RenderViewportPanel() usando o estado de
        // mouse do proprio ImGui, porque so faz sentido reagir quando o
        // mouse esta sobre o painel Viewport - o que o ImGui ja sabe dizer
        // (IsWindowHovered) sem precisarmos de um sistema de Input por
        // polling na engine ainda (isso fica para quando o modo "jogar
        // dentro do editor" precisar de WASD de verdade).
    }

    void EditorLayer::OnImGuiRender() {
        RenderDockspace();
    }

    void EditorLayer::RenderDockspace() {
        static bool dockspaceOpen = true;
        static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("PrismEditorDockspace", &dockspaceOpen, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspaceId = ImGui::GetID("PrismDockspace");
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
        }

        // Atalhos globais de Undo/Redo/Salvar. Ignorados enquanto o ImGui
        // esta capturando texto (ex: editando o campo "Nome" na Properties
        // panel, ou o proprio campo de nome do popup Salvar Como) para nao
        // brigar com o undo nativo de InputText - Ctrl+Z ali deve desfazer
        // a digitacao, nao uma acao do CommandHistory.
        if (!io.WantTextInput && !ImGui::IsPopupOpen(kSaveAsPopupId)) {
            bool ctrl = io.KeyCtrl;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_CommandHistory.Undo();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                m_CommandHistory.Redo();
            else if (ctrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveActiveSceneAs();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveActiveScene();
        }

        RenderMenuBar();
        RenderSaveAsPopup(); // popup modal - precisa ser chamado todo frame, mesmo fechado (ver comentario no metodo)

        ImGui::End();

        // Paineis - cada um e sua propria janela ImGui, e o dockspace acima
        // permite que o usuario os arraste/organize livremente.
        RenderViewportPanel();
        RenderHierarchyPanel();
        RenderPropertiesPanel();
        RenderConsolePanel();
        RenderContentBrowserPanel();
        RenderCameraPreviewPanel();
    }

    void EditorLayer::RenderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Arquivo")) {
                if (ImGui::MenuItem("Novo Mapa")) {
                    NewMap();
                }
                if (ImGui::MenuItem("Salvar Mapa", "Ctrl+S")) {
                    SaveActiveScene();
                }
                if (ImGui::MenuItem("Salvar Como...", "Ctrl+Shift+S")) {
                    SaveActiveSceneAs();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Fechar Projeto")) {
                    Prism::Application::Get().Close(); // TODO: voltar ao ProjectManagerLayer em vez de fechar
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Editar")) {
                std::string undoLabel = m_CommandHistory.CanUndo() ? ("Desfazer '" + m_CommandHistory.PeekUndoName() + "'") : "Desfazer";
                std::string redoLabel = m_CommandHistory.CanRedo() ? ("Refazer '" + m_CommandHistory.PeekRedoName() + "'") : "Refazer";

                if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, m_CommandHistory.CanUndo()))
                    m_CommandHistory.Undo();
                if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, m_CommandHistory.CanRedo()))
                    m_CommandHistory.Redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Entidade")) {
                if (ImGui::MenuItem("Criar Cubo")) {
                    auto command = Prism::CreateScope<CreateEntityCommand>(m_ActiveScene, "Cubo", glm::vec3(0.85f, 0.55f, 0.2f));
                    CreateEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Luz")) {
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_ActiveScene, "Luz", "Luz",
                        [](Prism::Entity entity) { entity.AddComponent<Prism::LightComponent>(); });
                    CreatePresetEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Character")) {
                    // Preset de conveniencia: combo comum para um NPC/player
                    // controlavel por script - mesh visivel + collider +
                    // rigidbody cinematico (nao cai por gravidade sozinho,
                    // controle fica com o script/codigo - ver
                    // BodyType::Kinematic em Components.h) + slot de script
                    // vazio pronto para preencher.
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_ActiveScene, "Character", "Character",
                        [](Prism::Entity entity) {
                            auto& mesh = entity.AddComponent<Prism::MeshRendererComponent>();
                            mesh.Color = glm::vec3(0.3f, 0.8f, 0.4f);

                            auto& collider = entity.AddComponent<Prism::ColliderComponent>();
                            collider.Shape = Prism::ColliderShape::Capsule;
                            collider.Size = { 0.4f, 1.8f, 0.0f };

                            auto& rigidBody = entity.AddComponent<Prism::RigidBodyComponent>();
                            rigidBody.Type = Prism::BodyType::Kinematic;

                            entity.AddComponent<Prism::ScriptComponent>();
                        });
                    CreatePresetEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                }
                if (ImGui::MenuItem("Criar Camera")) {
                    // Preset de camera de jogo: so Transform + CameraComponent
                    // (nasce Primary=true - ver Components.h). Se ja existir
                    // outra Primary na cena, desmarcamos ela para manter a
                    // regra de "no maximo uma Primary" (mesmo ajuste feito
                    // em RenderAddComponentButton() para o botao Add Component).
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_ActiveScene, "Camera", "Camera",
                        [](Prism::Entity entity) { entity.AddComponent<Prism::CameraComponent>(); });
                    CreatePresetEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                    if (m_SelectedEntity)
                        SetPrimaryCamera(m_SelectedEntity); // garante unicidade - desmarca qualquer Primary anterior
                }
                if (ImGui::MenuItem("Criar Entidade Vazia")) {
                    // So Transform (todo Scene::CreateEntity ja da isso) -
                    // ponto de partida para montar qualquer combinacao de
                    // components manualmente via "+ Add Component".
                    auto command = Prism::CreateScope<CreatePresetEntityCommand>(m_ActiveScene, "Entidade Vazia", "Entidade Vazia",
                        CreatePresetEntityCommand::SetupFn{});
                    CreatePresetEntityCommand* raw = command.get();
                    m_CommandHistory.Execute(std::move(command));
                    m_SelectedEntity = raw->GetCreatedEntity();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Excluir selecionada", nullptr, false, (bool)m_SelectedEntity)) {
                    m_CommandHistory.Execute(Prism::CreateScope<DeleteEntityCommand>(m_ActiveScene, m_SelectedEntity));
                    m_SelectedEntity = {};
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Janela")) {
                ImGui::MenuItem("Viewport", nullptr, true, false);
                ImGui::MenuItem("Hierarquia", nullptr, true, false);
                ImGui::MenuItem("Propriedades", nullptr, true, false);
                ImGui::MenuItem("Console", nullptr, true, false);
                ImGui::MenuItem("Conteudo do Projeto", nullptr, true, false);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

    void EditorLayer::RenderViewportPanel() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        ImVec2 size = ImGui::GetContentRegionAvail();
        // Nunca deixamos o tamanho chegar a zero - um framebuffer 0x0 e
        // invalido em OpenGL (ver Framebuffer::Resize, que ja ignora isso
        // tambem por seguranca).
        m_ViewportSize[0] = std::max(size.x, 1.0f);
        m_ViewportSize[1] = std::max(size.y, 1.0f);

        // A cena ja foi desenhada no framebuffer em OnUpdate() deste mesmo
        // frame - aqui so pegamos o color attachment (uma textura OpenGL
        // comum) e desenhamos como uma imagem dentro do painel ImGui. E
        // exatamente assim que Unity/Unreal/Godot/Hazel mostram a viewport
        // 3D dentro de uma janela de UI dockavel.
        uint32_t textureID = m_ViewportFramebuffer->GetColorAttachmentID();
        ImGui::Image((ImTextureID)(uintptr_t)textureID, ImVec2(m_ViewportSize[0], m_ViewportSize[1]),
                     ImVec2(0, 1), ImVec2(1, 0)); // UV invertido no Y: origem do framebuffer OpenGL e embaixo a esquerda.

        // Controle de camera minimo: segurar botao direito do mouse sobre a
        // viewport e arrastar orbita a camera; scroll (com o mouse sobre a
        // viewport) aproxima/afasta. E deliberadamente simples - vira a
        // camera de editor "de verdade" (com pan, foco em objeto, etc)
        // quando o resto do editor (gizmos de manipulacao, picking por
        // raycast) existir.
        if (m_ViewportHovered) {
            ImGuiIO& io = ImGui::GetIO();

            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                ImVec2 delta = io.MouseDelta;
                m_CameraYaw += delta.x * 0.4f;
                m_CameraPitch = std::clamp(m_CameraPitch - delta.y * 0.4f, -89.0f, 89.0f);
            }

            if (io.MouseWheel != 0.0f) {
                m_CameraDistance = std::clamp(m_CameraDistance - io.MouseWheel * 0.5f, 1.0f, 25.0f);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::RenderCameraPreviewPanel() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Camera");

        ImVec2 size = ImGui::GetContentRegionAvail();
        m_CameraPreviewSize[0] = std::max(size.x, 1.0f);
        m_CameraPreviewSize[1] = std::max(size.y, 1.0f);

        // Framebuffer criado sob demanda, na primeira vez que este painel
        // e desenhado - evita alocar uma textura de GPU extra em sessoes
        // que nunca abrem o painel Camera (ele fica fechado por padrao?
        // nao - ImGui::Begin sempre desenha a janela se ela nao foi
        // explicitamente escondida - mas o custo de checar aqui e minimo,
        // e deixa o codigo resistente a um futuro "fechar painel").
        if (!m_CameraPreviewFramebuffer)
            m_CameraPreviewFramebuffer = Prism::Framebuffer::Create({ (uint32_t)m_CameraPreviewSize[0], (uint32_t)m_CameraPreviewSize[1] });

        // Existe alguma entidade Primary agora? Se nao, RenderCameraPreview()
        // so limpou o framebuffer (fundo solido) - mostramos uma mensagem
        // em vez da imagem, para deixar claro que falta marcar uma camera
        // como Primary (Properties panel > secao Camera > checkbox Primary).
        bool hasPrimary = false;
        {
            auto view = m_ActiveScene->GetRegistry().view<Prism::CameraComponent>();
            for (auto entityHandle : view) {
                if (view.get<Prism::CameraComponent>(entityHandle).Primary) {
                    hasPrimary = true;
                    break;
                }
            }
        }

        if (hasPrimary) {
            uint32_t textureID = m_CameraPreviewFramebuffer->GetColorAttachmentID();
            ImGui::Image((ImTextureID)(uintptr_t)textureID, ImVec2(m_CameraPreviewSize[0], m_CameraPreviewSize[1]),
                         ImVec2(0, 1), ImVec2(1, 0)); // UV invertido no Y, mesmo motivo do painel Viewport.
        } else {
            ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));
            ImGui::TextWrapped("Nenhuma camera marcada como Primary. Selecione uma entidade com CameraComponent e marque \"Primary\" na Properties panel.");
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::RenderHierarchyPanel() {
        ImGui::Begin("Hierarquia");

        // So desenha as RAIZES (entidades sem pai) - cada uma desenha seus
        // proprios filhos recursivamente por dentro de RenderHierarchyNode.
        // Isso substitui a lista plana antiga por uma arvore de verdade
        // (ver RelationshipComponent em Components.h).
        m_ActiveScene->ForEachRootEntity([&](entt::entity handle, Prism::TagComponent&) {
            RenderHierarchyNode(Prism::Entity(handle, m_ActiveScene.get()));
        });

        // Area vazia do painel tambem e um alvo de drop valido - soltar
        // uma entidade aqui a torna raiz de novo (reparentar para
        // "nenhum pai"). Precisa vir DEPOIS do loop acima para cobrir o
        // espaco em branco abaixo da arvore inteira.
        ImGui::Dummy(ImGui::GetContentRegionAvail());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRISM_ENTITY_HANDLE")) {
                entt::entity draggedHandle = *(const entt::entity*)payload->Data;
                Prism::Entity dragged(draggedHandle, m_ActiveScene.get());
                if (dragged) {
                    Prism::Entity oldParent;
                    if (auto* rel = m_ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(draggedHandle)) {
                        if (rel->Parent != entt::null)
                            oldParent = Prism::Entity(rel->Parent, m_ActiveScene.get());
                    }
                    if (oldParent) // so gera comando se realmente tinha pai (senao ja era raiz, nada muda)
                        m_CommandHistory.Execute(Prism::CreateScope<SetParentCommand>(m_ActiveScene, dragged, oldParent, Prism::Entity{}));
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Clicar em area vazia do painel desseleciona - convencao comum em
        // editores (Unity/Godot fazem o mesmo). IsAnyItemHovered ja e
        // false aqui porque o Dummy acima nao conta como "item" para fins
        // de clique (so como alvo de drag-and-drop).
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
            m_SelectedEntity = {};

        ImGui::End();
    }

    void EditorLayer::RenderHierarchyNode(Prism::Entity entity) {
        entt::entity handle = entity.GetHandle();
        auto& tag = entity.GetComponent<Prism::TagComponent>();
        bool isSelected = (m_SelectedEntity == entity);

        auto* rel = m_ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(handle);
        bool hasChildren = rel && !rel->Children.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (isSelected)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)handle, flags, "%s", tag.Tag.c_str());
        if (ImGui::IsItemClicked())
            m_SelectedEntity = entity;

        // Origem do drag: qualquer node pode ser arrastado. So guardamos o
        // handle bruto (4 bytes) no payload - suficiente para reconstruir
        // uma Prism::Entity do lado de quem recebe (m_ActiveScene.get() e
        // sempre a mesma Scene).
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("PRISM_ENTITY_HANDLE", &handle, sizeof(entt::entity));
            ImGui::Text("%s", tag.Tag.c_str());
            ImGui::EndDragDropSource();
        }

        // Alvo do drag: soltar outra entidade sobre este node a torna
        // FILHA dele. Scene::SetParent ja recusa ciclos internamente (ver
        // Scene.cpp) - um IsAncestorOf() extra aqui so evita nem tentar
        // caso a UI queira, no futuro, desenhar feedback visual diferente
        // para um drop invalido; por ora deixamos SetParent decidir.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRISM_ENTITY_HANDLE")) {
                entt::entity draggedHandle = *(const entt::entity*)payload->Data;
                if (draggedHandle != handle) {
                    Prism::Entity dragged(draggedHandle, m_ActiveScene.get());
                    if (dragged) {
                        Prism::Entity oldParent;
                        if (auto* draggedRel = m_ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(draggedHandle)) {
                            if (draggedRel->Parent != entt::null)
                                oldParent = Prism::Entity(draggedRel->Parent, m_ActiveScene.get());
                        }
                        m_CommandHistory.Execute(Prism::CreateScope<SetParentCommand>(m_ActiveScene, dragged, oldParent, entity));
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (open && hasChildren) {
            // Copia a lista de filhos antes de iterar: um reparentamento
            // (drag-and-drop) disparado enquanto desenhamos esta arvore
            // poderia mudar RelationshipComponent::Children no meio do
            // loop - iterar a copia evita invalidar o iterator/o proprio
            // vector sendo lido.
            std::vector<entt::entity> childrenCopy = rel->Children;
            for (entt::entity childHandle : childrenCopy) {
                if (m_ActiveScene->GetRegistry().valid(childHandle))
                    RenderHierarchyNode(Prism::Entity(childHandle, m_ActiveScene.get()));
            }
            ImGui::TreePop();
        } else if (open && !hasChildren) {
            // Leaf com NoTreePushOnOpen nao empurra um nivel de arvore -
            // nada a fazer aqui, mas o 'open' de um Leaf via
            // NoTreePushOnOpen sempre vem true quando clicado (nao abre
            // nada de fato); sem TreePop correspondente porque
            // NoTreePushOnOpen nunca empurrou um.
        }
    }

    void EditorLayer::RenderPropertiesPanel() {
        ImGui::Begin("Propriedades");

        if (!m_SelectedEntity) {
            ImGui::TextDisabled("Nada selecionado.");
            ImGui::End();
            return;
        }

        auto& tag = m_SelectedEntity.GetComponent<Prism::TagComponent>();
        char nameBuffer[256];
        strncpy(nameBuffer, tag.Tag.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        if (ImGui::InputText("Nome", nameBuffer, sizeof(nameBuffer)))
            tag.Tag = nameBuffer;

        ImGui::Separator();

        if (m_SelectedEntity.HasComponent<Prism::TransformComponent>()) {
            auto& transform = m_SelectedEntity.GetComponent<Prism::TransformComponent>();
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Cada DragFloat3 e verificado individualmente logo apos ser
                // desenhado - IsItemActivated()/IsItemDeactivatedAfterEdit()
                // sempre se referem ao ULTIMO item desenhado, entao nao da
                // para checar os tres so no final (so pegaria o de Escala).
                ImGui::DragFloat3("Posicao", glm::value_ptr(transform.Translation), 0.05f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<TransformCommand>(m_SelectedEntity, m_TransformBeforeEdit, transform));

                ImGui::DragFloat3("Rotacao", glm::value_ptr(transform.Rotation), 0.5f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<TransformCommand>(m_SelectedEntity, m_TransformBeforeEdit, transform));

                ImGui::DragFloat3("Escala", glm::value_ptr(transform.Scale), 0.05f, 0.01f, 100.0f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<TransformCommand>(m_SelectedEntity, m_TransformBeforeEdit, transform));
            }
            // Transform nao tem botao de remover - toda entidade tem um por
            // definicao (ver Scene::CreateEntity) e o resto da engine
            // assume isso (ex: RenderScene le GetTransform() sem checar
            // HasComponent primeiro).
        }

        if (m_SelectedEntity.HasComponent<Prism::MeshRendererComponent>()) {
            auto& meshRenderer = m_SelectedEntity.GetComponent<Prism::MeshRendererComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Mesh Renderer", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* meshNames[] = { "Cubo", "Esfera", "Capsula", "Cilindro", "Plano" };
                int meshIndex = (int)meshRenderer.Mesh;
                if (ImGui::Combo("Mesh", &meshIndex, meshNames, IM_ARRAYSIZE(meshNames)))
                    meshRenderer.Mesh = (Prism::PrimitiveMesh)meshIndex;
                ImGui::TextDisabled("Primitiva embutida (sem importacao de assets ainda).");

                ImGui::ColorEdit3("Cor", glm::value_ptr(meshRenderer.Color));
                if (ImGui::IsItemActivated())
                    m_ColorBeforeEdit = meshRenderer.Color;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_CommandHistory.Execute(Prism::CreateScope<MeshColorCommand>(m_SelectedEntity, m_ColorBeforeEdit, meshRenderer.Color));
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::MeshRendererComponent>>(m_SelectedEntity, "Mesh Renderer"));
        }

        if (m_SelectedEntity.HasComponent<Prism::LightComponent>()) {
            auto& light = m_SelectedEntity.GetComponent<Prism::LightComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Light", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* typeNames[] = { "Point (Omni)", "Spot", "Directional" };
                int typeIndex = (int)light.Type;
                if (ImGui::Combo("Tipo", &typeIndex, typeNames, IM_ARRAYSIZE(typeNames)))
                    light.Type = (Prism::LightType)typeIndex;

                ImGui::ColorEdit3("Cor##Light", glm::value_ptr(light.Color));
                ImGui::DragFloat("Intensidade", &light.Intensity, 0.05f, 0.0f, 100.0f);

                if (light.Type != Prism::LightType::Directional)
                    ImGui::DragFloat("Alcance", &light.Range, 0.1f, 0.0f, 1000.0f);
                if (light.Type == Prism::LightType::Spot)
                    ImGui::DragFloat("Angulo do Cone", &light.SpotAngle, 0.5f, 1.0f, 90.0f);

                ImGui::TextDisabled("Ainda nao afeta a renderizacao (ver README).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::LightComponent>>(m_SelectedEntity, "Light"));
        }

        if (m_SelectedEntity.HasComponent<Prism::ColliderComponent>()) {
            auto& collider = m_SelectedEntity.GetComponent<Prism::ColliderComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Collider", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* shapeNames[] = { "Caixa", "Esfera", "Capsula" };
                int shapeIndex = (int)collider.Shape;
                if (ImGui::Combo("Forma", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames)))
                    collider.Shape = (Prism::ColliderShape)shapeIndex;

                switch (collider.Shape) {
                    case Prism::ColliderShape::Box:
                        ImGui::DragFloat3("Half-Extents", glm::value_ptr(collider.Size), 0.05f, 0.01f, 100.0f);
                        break;
                    case Prism::ColliderShape::Sphere:
                        ImGui::DragFloat("Raio", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                        break;
                    case Prism::ColliderShape::Capsule:
                        ImGui::DragFloat("Raio##Capsule", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                        ImGui::DragFloat("Altura##Capsule", &collider.Size.y, 0.05f, 0.01f, 100.0f);
                        break;
                }

                ImGui::Checkbox("E um Trigger", &collider.IsTrigger);
                ImGui::TextDisabled("Ainda nao alimenta simulacao de fisica (ver README).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ColliderComponent>>(m_SelectedEntity, "Collider"));
        }

        if (m_SelectedEntity.HasComponent<Prism::RigidBodyComponent>()) {
            auto& rigidBody = m_SelectedEntity.GetComponent<Prism::RigidBodyComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Rigid Body", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                if (!m_SelectedEntity.HasComponent<Prism::ColliderComponent>())
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Sem Collider - adicione um para a fisica funcionar.");

                const char* bodyTypeNames[] = { "Static", "Kinematic", "Dynamic" };
                int bodyTypeIndex = (int)rigidBody.Type;
                if (ImGui::Combo("Tipo##RigidBody", &bodyTypeIndex, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames)))
                    rigidBody.Type = (Prism::BodyType)bodyTypeIndex;

                bool dynamicOnly = (rigidBody.Type == Prism::BodyType::Dynamic);
                ImGui::BeginDisabled(!dynamicOnly);
                ImGui::DragFloat("Massa (kg)", &rigidBody.Mass, 0.1f, 0.01f, 10000.0f);
                ImGui::Checkbox("Usa Gravidade", &rigidBody.UseGravity);
                ImGui::EndDisabled();

                ImGui::Checkbox("Colisao Continua (CCD)", &rigidBody.ContinuousCollisionDetection);
                ImGui::TextDisabled("Ainda nao alimenta simulacao de fisica (ver README).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::RigidBodyComponent>>(m_SelectedEntity, "Rigid Body"));
        }

        if (m_SelectedEntity.HasComponent<Prism::CameraComponent>()) {
            auto& camera = m_SelectedEntity.GetComponent<Prism::CameraComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Camera", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* projectionNames[] = { "Perspectiva", "Ortografica" };
                int projectionIndex = (int)camera.ProjectionType;
                if (ImGui::Combo("Projecao", &projectionIndex, projectionNames, IM_ARRAYSIZE(projectionNames)))
                    camera.ProjectionType = (Prism::CameraProjectionType)projectionIndex;

                if (camera.ProjectionType == Prism::CameraProjectionType::Perspective)
                    ImGui::DragFloat("FOV", &camera.FOV, 0.5f, 1.0f, 179.0f);
                else
                    ImGui::DragFloat("Tamanho Ortografico", &camera.OrthoSize, 0.1f, 0.01f, 1000.0f);

                ImGui::DragFloat("Near Clip", &camera.NearClip, 0.01f, 0.001f, camera.FarClip - 0.01f);
                ImGui::DragFloat("Far Clip", &camera.FarClip, 1.0f, camera.NearClip + 0.01f, 100000.0f);

                // Marcar esta camera como Primary desmarca qualquer outra
                // na cena (ver SetPrimaryCamera) - so faz sentido existir
                // uma Primary por vez, ja que e ela que o modo Play (e a
                // propria viewport do editor, como fallback - ver
                // RenderScene) usa para renderizar.
                bool isPrimary = camera.Primary;
                if (ImGui::Checkbox("Primary", &isPrimary)) {
                    if (isPrimary)
                        SetPrimaryCamera(m_SelectedEntity);
                    else
                        camera.Primary = false; // desmarcar a unica Primary e permitido - so significa "nenhuma camera de jogo definida ainda"
                }
                if (!isPrimary)
                    ImGui::TextDisabled("Nao e a camera principal - o modo Play/viewport nao vai usar esta.");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::CameraComponent>>(m_SelectedEntity, "Camera"));
        }

        if (m_SelectedEntity.HasComponent<Prism::ScriptComponent>()) {
            auto& script = m_SelectedEntity.GetComponent<Prism::ScriptComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Script", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                char scriptPathBuffer[256];
                strncpy(scriptPathBuffer, script.ScriptPath.c_str(), sizeof(scriptPathBuffer) - 1);
                scriptPathBuffer[sizeof(scriptPathBuffer) - 1] = '\0';
                ImGui::InputTextWithHint("Arquivo", "ex: player_controller.lua", scriptPathBuffer, sizeof(scriptPathBuffer));
                if (ImGui::IsItemDeactivatedAfterEdit())
                    script.ScriptPath = scriptPathBuffer;

                ImGui::TextDisabled("Relativo a Scripts/. Ainda nao executa (Lua nao esta embutido ainda).");
            }
            if (!keepOpen)
                m_CommandHistory.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ScriptComponent>>(m_SelectedEntity, "Script"));
        }

        ImGui::Dummy(ImVec2(0, 8));
        RenderAddComponentButton();

        ImGui::Separator();
        ImGui::TextDisabled("Camera do editor");
        ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_CameraYaw, m_CameraPitch);
        ImGui::Text("Distancia: %.2f", m_CameraDistance);

        ImGui::End();
    }

    void EditorLayer::RenderAddComponentButton() {
        // So mostra o botao se sobrar pelo menos um component que a
        // entidade ainda nao tem - evita um popup vazio (a entidade ja
        // tem TransformComponent sempre, entao esse nunca entra na lista).
        bool hasAnyMissing = !m_SelectedEntity.HasComponent<Prism::MeshRendererComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::LightComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::ColliderComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::RigidBodyComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::ScriptComponent>()
                           || !m_SelectedEntity.HasComponent<Prism::CameraComponent>();

        if (!hasAnyMissing) {
            ImGui::TextDisabled("(todos os components ja adicionados)");
            return;
        }

        if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!m_SelectedEntity.HasComponent<Prism::MeshRendererComponent>() && ImGui::MenuItem("Mesh Renderer")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::MeshRendererComponent>>(m_SelectedEntity, "Mesh Renderer"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::LightComponent>() && ImGui::MenuItem("Light")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::LightComponent>>(m_SelectedEntity, "Light"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::ColliderComponent>() && ImGui::MenuItem("Collider")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::ColliderComponent>>(m_SelectedEntity, "Collider"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::RigidBodyComponent>() && ImGui::MenuItem("Rigid Body")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::RigidBodyComponent>>(m_SelectedEntity, "Rigid Body"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::ScriptComponent>() && ImGui::MenuItem("Script")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::ScriptComponent>>(m_SelectedEntity, "Script"));
                ImGui::CloseCurrentPopup();
            }
            if (!m_SelectedEntity.HasComponent<Prism::CameraComponent>() && ImGui::MenuItem("Camera")) {
                m_CommandHistory.Execute(Prism::CreateScope<AddComponentCommand<Prism::CameraComponent>>(m_SelectedEntity, "Camera"));
                // CameraComponent nasce com Primary=true por padrao (ver
                // Components.h) - se ja existir outra camera Primary na
                // cena, isso violaria a regra de "no maximo uma" ate o
                // usuario mexer manualmente no checkbox. Corrige aqui na
                // hora, fora do historico de undo (e so um ajuste de
                // consistencia, nao uma edicao que o usuario pediu).
                bool anyOtherPrimary = false;
                auto view = m_ActiveScene->GetRegistry().view<Prism::CameraComponent>();
                for (auto entityHandle : view) {
                    Prism::Entity entity(entityHandle, m_ActiveScene.get());
                    if (entity != m_SelectedEntity && entity.GetComponent<Prism::CameraComponent>().Primary) {
                        anyOtherPrimary = true;
                        break;
                    }
                }
                if (anyOtherPrimary)
                    m_SelectedEntity.GetComponent<Prism::CameraComponent>().Primary = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorLayer::RenderConsolePanel() {
        m_ConsolePanel.OnImGuiRender();
    }

    void EditorLayer::RenderContentBrowserPanel() {
        m_ContentBrowser.OnImGuiRender();
    }

}
