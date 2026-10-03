#include "ViewportPanel.h"
#include "EditorGizmos.h"
#include "../Core/EntityOps.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp> // glm::decompose() - GLM_ENABLE_EXPERIMENTAL ja definido globalmente em Components.h
#include <glm/gtc/quaternion.hpp> // glm::eulerAngles(quat) - usado em RenderTransformGizmo para converter o resultado de glm::decompose() de volta para graus euler
#include <algorithm>
#include <cmath>
#include <cstring>

namespace PrismEditor {

    void ViewportPanel::Init() {
        Prism::FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_ViewportFramebuffer = Prism::Framebuffer::Create(fbSpec);
    }

    void ViewportPanel::ResizeIfNeeded() {
        // Redimensiona o framebuffer se o painel Viewport mudou de tamanho
        // desde o ultimo frame (arrastar a janela, dockar/desdockar, etc).
        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        if (m_ViewportSize[0] > 0.0f && m_ViewportSize[1] > 0.0f &&
            (spec.Width != (uint32_t)m_ViewportSize[0] || spec.Height != (uint32_t)m_ViewportSize[1])) {
            m_ViewportFramebuffer->Resize((uint32_t)m_ViewportSize[0], (uint32_t)m_ViewportSize[1]);
        }
    }

    void ViewportPanel::RenderScene(float deltaTime) {
        m_ViewportFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        const auto& spec = m_ViewportFramebuffer->GetSpecification();
        float aspect = spec.Height > 0 ? (float)spec.Width / (float)spec.Height : 1.0f;

        // A viewport principal do editor usa SEMPRE a camera LIVRE do
        // editor (EditorContext::Camera.Position + yaw/pitch) - nunca a
        // CameraComponent::Primary da cena, mesmo que exista uma. Ver o
        // comentario em EditorContext::Camera.Position (EditorLayer.h) e em
        // PropertiesPanel::RenderCameraPreview() para o motivo: substituir a viewport
        // principal pela camera de jogo te deixa "preso" dentro de
        // qualquer mesh onde a camera esteja posicionada (ex: dentro da
        // capsula de colisao de um character), sem visao de trabalho para
        // corrigir isso. A camera de jogo tem sua propria preview separada
        // (RenderCameraPreview/m_CameraPreviewFramebuffer).
        //
        // EditorCamera::GetViewMatrix() e o unico lugar que sabe montar esta
        // view (usado aqui E em RenderViewportPanel, para picking/ImGuizmo)
        // - garantir que os dois usem exatamente a mesma matriz e o que
        // impede a selecao com o mouse de dessincronizar da imagem.
        glm::mat4 view = m_Ctx.Camera.GetViewMatrix();
        // Far de 1000 (era 100): com movimento livre, o usuario pode se
        // afastar bastante da cena antes de querer ver tudo - um far
        // pequeno faria a geometria "sumir" do outro lado antes do usuario
        // terminar de se posicionar.
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
        glm::mat4 viewProjection = projection * view;

        // Renderer::DrawScene ja chama SetCameraPosition() internamente
        // (necessario ANTES de qualquer DrawMesh() deste framebuffer - ver
        // comentario em Renderer::SetCameraPosition, Renderer.h, sobre o
        // teste de "face interna transparente") - nao precisa ser feito
        // aqui separadamente.
        //
        // 'view'/'projection' passadas SEPARADAS (nao 'viewProjection'
        // combinada) desde que DrawScene ganhou SSAO - ver comentario na
        // assinatura de Renderer::DrawScene (Renderer.h).
        Prism::Renderer::DrawScene(*m_Ctx.ActiveScene, glm::value_ptr(view), glm::value_ptr(projection), glm::value_ptr(m_Ctx.Camera.Position));
        EditorGizmos::RenderCameraGizmos(m_Ctx, viewProjection);
        EditorGizmos::RenderSelectedColliderGizmo(m_Ctx, viewProjection);
        EditorGizmos::RenderLightGizmos(m_Ctx, viewProjection);
        EditorGizmos::RenderRaycastGizmos(m_Ctx, viewProjection);

        m_ViewportFramebuffer->Unbind();
    }

    void ViewportPanel::OnImGuiRender() {
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

        // Posicao/tamanho REAIS (em pixels de tela do SO) de onde a imagem
        // acabou de ser desenhada - GetItemRectMin() pega isso do ULTIMO
        // item (o ImGui::Image logo acima), diferente de GetWindowPos()
        // (que da a posicao da JANELA inteira, incluindo a barra de
        // titulo "Viewport" no topo). Usar GetWindowPos() aqui foi o bug
        // original que deixava o retangulo do ImGuizmo desalinhado da
        // imagem por ~20-30px (a altura da barra de titulo) - o gizmo
        // aparecia desenhado no lugar certo (ele so precisa de
        // view/projection para isso), mas a area de CLIQUE/hover do
        // ImGuizmo usava esse retangulo errado, entao passar o mouse ou
        // clicar em cima dele nao registrava nada. Guardado aqui (em vez
        // de so dentro de RenderTransformGizmo) porque a toolbar abaixo
        // tambem precisa saber onde a imagem comeca.
        ImVec2 imageMin = ImGui::GetItemRectMin();

        // Soltar um .prismprefab do Content Browser sobre a Viewport
        // instancia ele na cena ativa - como RAIZ, na origem (0,0,0),
        // mesmo ponto de partida que todo preset do menu "Entidade" ja
        // usa (ver RenderMenuBar, "Criar Cubo" etc). Posicionar a
        // instancia exatamente sob o cursor (via raycast contra o plano
        // do chao ou a superficie sob o mouse) e um refinamento futuro -
        // por ora, arrastar e so uma forma rapida de trazer o prefab para
        // a cena, ajustar a posicao depois pelo gizmo/Properties panel
        // continua sendo o fluxo normal.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_PREFAB_PATH")) {
                std::string pathString((const char*)payload->Data, payload->DataSize - 1);
                EntityOps::InstantiatePrefab(m_Ctx, pathString);
            }
            ImGui::EndDragDropTarget();
        }

        // Toolbar flutuante do gizmo (Translate/Rotate/Scale + Local/World)
        // - desenhada por CIMA do canto superior esquerdo da IMAGEM (nao
        // da janela) via SetCursorScreenPos, que usa coordenadas de tela
        // absolutas (mesmo espaco de GetItemRectMin() acima) - diferente
        // de SetCursorPos usado antes, que e relativo ao CONTEUDO da
        // janela e nao contava com a barra de titulo, entao a toolbar
        // ficava desenhada por cima/atras dela em vez de dentro da area da
        // imagem. So aparece com alguma entidade selecionada, ja que sem
        // selecao o gizmo em si nao e desenhado (ver RenderTransformGizmo).
        // Atalhos W/E/R fazem a mesma coisa que estes botoes - a toolbar
        // existe para quem prefere clicar, ou nao lembra dos atalhos.
        if (m_Ctx.SelectedEntity) {
            ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
            ImGui::BeginGroup();
            if (ImGui::Button("Mover (W)")) m_GizmoOperation = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::Button("Rotacionar (E)")) m_GizmoOperation = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::Button("Escalar (R)")) m_GizmoOperation = ImGuizmo::SCALE;
            ImGui::SameLine();
            ImGui::TextUnformatted("|");
            ImGui::SameLine();
            if (ImGui::Button(m_GizmoMode == ImGuizmo::WORLD ? "Mundo" : "Local"))
                m_GizmoMode = (m_GizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            ImGui::EndGroup();
        }

        // ============================================================
        // Camera LIVRE do editor - estilo Godot
        // ============================================================
        // Camera com POSICAO livre, usando a mesma convencao de controles
        // do editor da Godot:
        //
        //   - Segurar o botao DIREITO do mouse sobre a viewport entra em
        //     "modo voar": o cursor e escondido e LOCKADO via
        //     GLFW_CURSOR_DISABLED (mesmo modo que jogos FPS usam para
        //     mouse look - o cursor nao sai da janela, entao o usuario
        //     nunca fica "travado na borda" no meio de um voo longo).
        //   - Enquanto voa:
        //       Mouse      = olhar (yaw/pitch)
        //       WASD       = mover no plano (W frente, S tras, A esq, D dir)
        //       Q/E        = descer/subir (eixo Y do MUNDO, absoluto)
        //       Shift      = 3x boost;  Alt = 3x slow (ajuste fino)
        //       Scroll     = ajusta a velocidade base de movimento
        //   - Soltar o RMB sai do modo voar; o cursor volta ao normal.
        //   - Fora do modo voar, scroll sobre a viewport faz DOLLY:
        //     avanca/recua a camera ao longo da direcao que ela olha,
        //     sem mudar a rotacao (mesma convencao da Godot para o
        //     scroll do editor).
        //
        // W/E/R SOZINHOS (sem RMB) continuam trocando a operacao do
        // gizmo (ver RenderTransformGizmo) - sem conflito, porque o
        // movimento exige o botao direito segurado (com o RMB segurado,
        // 'E' e "subir", nao "trocar para modo Rotate").
        GLFWwindow* nativeWindow = (GLFWwindow*)Prism::Application::Get().GetWindow().GetNativeWindow();

        // --- Entrada no modo voar (RMB sobre a viewport) ---------------
        // So entra se o mouse estiver SOBRE a viewport no momento do
        // clique com RMB - mesmo comportamento da Godot (RMB em outro
        // painel faz o que aquele painel faz, nao entra em fly mode).
        if (!m_Ctx.Camera.LookActive && m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_Ctx.Camera.LookActive = true;
            m_Ctx.Camera.LookSkipNextDelta = true; // ver comentario no header
            glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }

        // --- Saida do modo voar (RMB solto) ----------------------------
        // Nao usa m_ViewportHovered aqui: uma vez comecado a voar, o
        // cursor virtual pode sair da area do painel e ainda queremos
        // continuar em fly mode ate o usuario soltar o botao.
        if (m_Ctx.Camera.LookActive && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            m_Ctx.Camera.LookActive = false;
            glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        // --- Enquanto voa: olhar + mover ------------------------------
        if (m_Ctx.Camera.LookActive) {
            ImGuiIO& io = ImGui::GetIO();

            // Ignora o delta do mouse no primeiro frame apos entrar em
            // fly mode: em algumas plataformas, o GLFW reseta a posicao
            // virtual do cursor ao trocar para GLFW_CURSOR_DISABLED, o
            // que geraria um "snap" grande de rotacao (delta enorme num
            // unico frame). Depois desse primeiro frame, io.MouseDelta
            // ja esta correto.
            if (m_Ctx.Camera.LookSkipNextDelta) {
                m_Ctx.Camera.LookSkipNextDelta = false;
            }
            else {
                m_Ctx.Camera.Yaw += io.MouseDelta.x * 0.15f;

                // ============================================================
                // Sinal do eixo Y do mouse look
                // ============================================================
                // O ImGui reporta MouseDelta.y em coordenadas de TELA, que
                // crescem para BAIXO (mover o mouse fisicamente para baixo
                // da um delta.y POSITIVO). No 'forward' da camera (ver
                // ComputeEditorViewMatrix), pitch positivo olha para BAIXO
                // (-sin(pitch) no componente Y).
                //
                // A conta correta e: mouse para CIMA (delta.y negativo) ->
                // pitch DIMINUI (fica mais negativo) -> -sin(pitch) fica
                // POSITIVO -> camera olha para CIMA. Isso exige '+', nao
                // '-': quando delta.y e negativo, somar 'delta.y * k' a
                // EditorContext::Camera.Pitch DIMINUI o pitch (que e o que queremos).
                // Usar '-' inverteria o eixo vertical inteiro (mouse para
                // cima -> camera olha para baixo, e vice-versa).
                m_Ctx.Camera.Pitch = std::clamp(m_Ctx.Camera.Pitch + io.MouseDelta.y * 0.15f, -89.0f, 89.0f);
            }

            // Scroll ajusta a velocidade BASE de movimento (nao mais
            // "distancia de orbita" - a camera nao orbita mais nada).
            // Ajuste multiplicativo para ter um "feel" logaritmico: cada
            // tique do scroll multiplica a velocidade por um fator, entao
            // ajustar para "bem devagar" ou "bem rapido" funciona
            // igualmente bem, sem precisar de muitos cliques.
            if (io.MouseWheel != 0.0f)
                m_Ctx.Camera.MoveSpeed = std::clamp(m_Ctx.Camera.MoveSpeed * (1.0f + io.MouseWheel * 0.15f), 0.1f, 200.0f);

            // WASD/QE - speed base, com boost/slow ao estilo Godot: Shift
            // = 3x mais rapido, Alt = 3x mais lento (util para
            // posicionamento fino sem precisar baixar a velocidade base
            // no scroll).
            float speed = m_Ctx.Camera.MoveSpeed;
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift))
                speed *= 3.0f;
            if (ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt))
                speed *= 0.33f;

            // Mesma "frente" que ComputeEditorViewMatrix usa, para que
            // "andar para frente" (W) mova exatamente na direcao que a
            // camera esta olhando - sem isso, o movimento seria sempre no
            // plano horizontal com "frente" fixa (0,0,-1), independente
            // de onde o usuario esteja olhando.
            float yawRad = glm::radians(m_Ctx.Camera.Yaw);
            float pitchRad = glm::radians(m_Ctx.Camera.Pitch);
            glm::vec3 forward(
                -cosf(pitchRad) * cosf(yawRad),
                -sinf(pitchRad),
                -cosf(pitchRad) * sinf(yawRad)
            );
            // 'right' perpendicular a 'forward' NO PLANO HORIZONTAL -
            // cross com o "up" do mundo (e nao com o up local da camera),
            // o que da um "strafe" sempre paralelo ao chao (mesmo olhando
            // para cima/baixo). Ordem (forward x up) e a que da "direita"
            // no sistema destro do OpenGL (verificado: forward (0,0,-1) x
            // up (0,1,0) = (1,0,0) = +X, que e "direita" quando olhamos
            // para -Z, que e a convencao padrao de camera). Se a camera
            // estiver olhando praticamente reto para cima/baixo (forward
            // quase paralelo a up), o cross degenera - o normalize abaixo
            // cai para um "right" arbitrario nesse caso (mesmo com o
            // clamp de pitch em +/- 89, prefiro nao depender so disso
            // aqui).
            glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(right) > 0.0001f)
                right = glm::normalize(right);
            else
                right = glm::vec3(1.0f, 0.0f, 0.0f); // caso degenerado (olhando reto para cima/baixo) - right arbitrario, movimento horizontal continua funcional

            glm::vec3 moveDir(0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_W)) moveDir += forward;
            if (ImGui::IsKeyDown(ImGuiKey_S)) moveDir -= forward;
            if (ImGui::IsKeyDown(ImGuiKey_D)) moveDir += right;
            if (ImGui::IsKeyDown(ImGuiKey_A)) moveDir -= right;
            // Q/E sempre no eixo Y do MUNDO (nao no up local da camera) -
            // subir/descer deve ser sempre "para cima/para baixo" no
            // sentido absoluto, mesmo se a camera estiver de cabeca para
            // baixo.
            if (ImGui::IsKeyDown(ImGuiKey_E)) moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

            // Normaliza antes de multiplicar por 'speed': sem isso, W+D
            // moveria ~1.41x mais rapido que so W (diagonal mais longa
            // que o lado) - "diagonal strafe" seria mais rapido que
            // andar reto, o que e perceptivel e errado. Normalizando,
            // todas as direcoes tem a mesma velocidade.
            if (glm::length(moveDir) > 0.0001f) {
                // io.DeltaTime (do ImGui) - RenderViewportPanel nao recebe
                // 'deltaTime' do editor (so RenderScene recebe, vindo de
                // OnUpdate), mas o ImGui ja rastreia o delta por frame do
                // proprio NewFrame - usar isso e mais simples que passar
                // o deltaTime por toda a cadeia de chamadas de UI so para
                // este uso.
                m_Ctx.Camera.Position += glm::normalize(moveDir) * speed * io.DeltaTime;
            }
        }
        else if (m_ViewportHovered && ImGui::GetIO().MouseWheel != 0.0f) {
            // --- Fora do modo voar: scroll faz DOLLY (zoom Godot-style)
            // Move a camera para frente/tras ao longo da direcao que ela
            // olha, sem mudar a rotacao - mesma convencao da Godot para
            // o scroll do editor (nao e "orbitar mais perto/longe", e
            // literalmente "andar um passo na direcao do olhar"). O
            // passo de 0.5 unidades por tique e confortavel em cenas de
            // escala "1 unidade = 1 metro".
            float yawRad = glm::radians(m_Ctx.Camera.Yaw);
            float pitchRad = glm::radians(m_Ctx.Camera.Pitch);
            glm::vec3 forward(
                -cosf(pitchRad) * cosf(yawRad),
                -sinf(pitchRad),
                -cosf(pitchRad) * sinf(yawRad)
            );
            m_Ctx.Camera.Position += forward * ImGui::GetIO().MouseWheel * 0.5f;
        }

        // Recalcula as MESMAS matrizes view/projection que RenderScene() ja
        // montou este frame (baratas o bastante para nao valer a pena
        // cachear em membros so por isto) - tanto o picking abaixo quanto o
        // ImGuizmo (RenderTransformGizmo) precisam delas separadas (nao a
        // viewProjection combinada). ComputeEditorViewMatrix garante que
        // sao IDENTICAS as usadas por RenderScene (nao uma segunda copia
        // da formula que poderia dessincronizar).
        //
        // TODO O BLOCO DE PICKING/GIZMO E PULADO enquanto EditorContext::Camera.LookActive
        // e true: com o cursor "disabled" pelo GLFW, a posicao reportada
        // e virtual (pode estar em qualquer lugar), entao clicar em LMB
        // durante um voo selecionaria uma entidade aleatoria - e o
        // ImGuizmo receberia um retangulo de hover baseado numa posicao
        // de cursor que o usuario nem ve. Melhor suspender tudo durante o
        // voo.
        if (!m_Ctx.Camera.LookActive) {
            float aspect = m_ViewportSize[1] > 0.0f ? m_ViewportSize[0] / m_ViewportSize[1] : 1.0f;
            glm::mat4 view = m_Ctx.Camera.GetViewMatrix();
            // Far de 1000 - tem que bater com o far de RenderScene(),
            // senao o raio de picking nao corresponderia exatamente ao que
            // a camera "ve" (na pratica o far de picking e so o limite
            // superior de profundidade que o raio de mundo pode atingir;
            // manter igual e so bom senso).
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);

            // Picking: clique ESQUERDO simples (sem arrastar - IsMouseClicked
            // dispara so no frame em que o botao desce) sobre a viewport
            // seleciona a entidade sob o cursor, igual Unity/Unreal/Godot.
            // Duas checagens evitam roubar o clique de outra coisa:
            //   - !ImGuizmo::IsOver(): um clique EM CIMA do gizmo de
            //     manipulacao (quando ha selecao) deve mover/rotacionar/
            //     escalar a entidade, nao trocar a selecao por baixo dele.
            //   - !ImGui::IsAnyItemHovered(): cobre a toolbar flutuante
            //     (Mover/Rotacionar/Escalar/Mundo, ver acima) desenhada por
            //     cima do canto da viewport - clicar nela nao deve
            //     "vazar" como picking na cena atras.
            if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGuizmo::IsOver() && !ImGui::IsAnyItemHovered()) {

                // Posicao do mouse RELATIVA a imagem da viewport (nao a
                // janela) e em [0,1] - mesmo espaco de imageMin/m_ViewportSize
                // usados por ImGuizmo::SetRect acima.
                ImVec2 mousePos = ImGui::GetMousePos();
                float mouseX = mousePos.x - imageMin.x;
                float mouseY = mousePos.y - imageMin.y;
                float ndcX = (mouseX / m_ViewportSize[0]) * 2.0f - 1.0f;
                // Y de tela cresce para BAIXO, NDC cresce para CIMA - inverte.
                float ndcY = 1.0f - (mouseY / m_ViewportSize[1]) * 2.0f;

                // Unprojection classica: leva dois pontos em clip space (no
                // near e no far plane, mesmo XY de NDC) de volta para
                // espaço de mundo via a inversa da view-projection - a reta
                // entre eles E o raio de mundo que passa pelo pixel
                // clicado. Mais simples e robusto que tentar reconstruir o
                // raio a partir so do FOV/aspect manualmente, e reaproveita
                // exatamente as mesmas view/projection que desenharam a
                // cena, entao nao pode dessincronizar delas.
                glm::mat4 invViewProjection = glm::inverse(projection * view);

                glm::vec4 nearPointClip(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 farPointClip(ndcX, ndcY, 1.0f, 1.0f);

                glm::vec4 nearPointWorld = invViewProjection * nearPointClip;
                glm::vec4 farPointWorld = invViewProjection * farPointClip;
                nearPointWorld /= nearPointWorld.w;
                farPointWorld /= farPointWorld.w;

                Prism::VisualRay ray;
                ray.Origin = glm::vec3(nearPointWorld);
                ray.Direction = glm::normalize(glm::vec3(farPointWorld - nearPointWorld));

                Prism::VisualRaycastHit hit = m_Ctx.ActiveScene->VisualRaycast(ray);
                m_Ctx.SelectedEntity = hit.Hit ? Prism::Entity(hit.Entity, m_Ctx.ActiveScene.get())
                    : Prism::Entity{};
            }

            RenderTransformGizmo(view, projection, imageMin);
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ViewportPanel::RenderTransformGizmo(const glm::mat4& view, const glm::mat4& projection, const ImVec2& imageScreenPos) {
        if (!m_Ctx.SelectedEntity)
            return;
        // So faz sentido manipular Transform de entidades que tem uma (toda
        // entidade tem, ver Scene::CreateEntity, mas a checagem custa nada
        // e protege contra qualquer excecao futura).
        if (!m_Ctx.SelectedEntity.HasComponent<Prism::TransformComponent>())
            return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        // ImGuizmo desenha por cima da JANELA IMGUI ATUAL ("Viewport", ja
        // que RenderTransformGizmo e chamado de dentro de
        // RenderViewportPanel antes do ImGui::End()) - SetRect define a
        // regiao de tela (em pixels, espaco de janela do SO) onde a IMAGEM
        // foi desenhada (imageScreenPos, capturado via GetItemRectMin()
        // logo apos o ImGui::Image em RenderViewportPanel - NAO
        // GetWindowPos(), que da a janela inteira incluindo a barra de
        // titulo e desalinhava a area de clique do gizmo da imagem por
        // conta da altura dessa barra - bug corrigido).
        ImGuizmo::SetRect(imageScreenPos.x, imageScreenPos.y, m_ViewportSize[0], m_ViewportSize[1]);

        // Atalhos de teclado (W/E/R) - so quando a viewport esta em foco E
        // o ImGuizmo nao esta sendo arrastado no momento (nao faz sentido
        // trocar de operacao no meio de um gesto). Mesma convencao de
        // Unity/Unreal/Godot. IsAnyItemActive cobre o caso de estar
        // digitando texto em outro painel (ex: campo "Nome") - nao rouba a
        // tecla 'e' de dentro de um InputText nesse caso.
        //
        // !IsMouseDown(Right): com o RMB segurado, W/E/R pertencem a
        // CAMERA do editor (ver RenderViewportPanel) - 'E' e "subir", nao
        // "trocar o gizmo para Rotate". Sem essa checagem, um voo com E
        // pressionado trocaria a operacao do gizmo silenciosamente.
        // (Note que este bloco so roda quando !EditorContext::Camera.LookActive, entao
        // na pratica o RMB ja esta solto aqui - a checagem extra e
        // defensiva, caso alguem chame RenderTransformGizmo de outro
        // contexto no futuro.)
        if (m_ViewportFocused && !ImGuizmo::IsUsing() && !ImGui::IsAnyItemActive() &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoOperation = ImGuizmo::SCALE;
        }

        // A entidade pode ter um pai (RelationshipComponent) - o gizmo
        // sempre opera em espaco de MUNDO (para o usuario, arrastar "para
        // a direita" deve sempre significar direita do mundo, nao do pai),
        // entao passamos a matriz de MUNDO para o Manipulate() e, se o
        // gesto mudou algo, convertemos o resultado de volta para o espaco
        // LOCAL do pai antes de escrever em TransformComponent (que e
        // sempre local - ver comentario em Scene::GetWorldTransform).
        glm::mat4 worldMatrix = m_Ctx.ActiveScene->GetWorldTransform(m_Ctx.SelectedEntity);

        bool snap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        float snapValues[3] = { 0.0f, 0.0f, 0.0f };
        if (snap) {
            // Passos de snap convencionais: 1 unidade para posicao/escala,
            // 15 graus para rotacao - mesmos defaults que Unity usa com
            // Ctrl segurado.
            float step = (m_GizmoOperation == ImGuizmo::ROTATE) ? 15.0f : 1.0f;
            snapValues[0] = snapValues[1] = snapValues[2] = step;
        }

        // m_GizmoOperation/m_GizmoMode ja sao dos tipos ImGuizmo::OPERATION
        // / ImGuizmo::MODE (ver EditorLayer.h) - NAO sao mais int com um
        // cast "por fora". Era exatamente esse cast de um int=0 assumindo
        // TRANSLATE que fazia o gizmo nao aparecer por padrao ao
        // selecionar uma entidade (dependendo do fork do ImGuizmo, 0 nao
        // e necessariamente TRANSLATE, entao Manipulate() recebia uma
        // operacao invalida e nao desenhava nada).
        ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(projection),
            m_GizmoOperation, m_GizmoMode,
            glm::value_ptr(worldMatrix), nullptr,
            snap ? snapValues : nullptr);

        bool isUsing = ImGuizmo::IsUsing();

        // Captura o estado "antes" no exato frame em que o arraste COMECA
        // - mesmo padrao de m_TransformBeforeEdit para os DragFloat3 da
        // Properties panel (ver comentario no header).
        if (isUsing && !m_GizmoWasUsingLastFrame)
            m_GizmoTransformBeforeEdit = m_Ctx.SelectedEntity.GetComponent<Prism::TransformComponent>();

        if (isUsing) {
            glm::mat4 localMatrix = worldMatrix;

            // Se a entidade tem pai, o TransformComponent e relativo a ELE
            // - multiplicamos pela inversa da matriz de mundo do PAI para
            // voltar ao espaco local.
            Prism::Entity parent;
            if (auto* rel = m_Ctx.ActiveScene->GetRegistry().try_get<Prism::RelationshipComponent>(m_Ctx.SelectedEntity.GetHandle())) {
                if (rel->Parent != entt::null)
                    parent = Prism::Entity(rel->Parent, m_Ctx.ActiveScene.get());
            }
            if (parent) {
                glm::mat4 parentWorld = m_Ctx.ActiveScene->GetWorldTransform(parent);
                localMatrix = glm::inverse(parentWorld) * worldMatrix;
            }

            glm::vec3 translation, scale, skew;
            glm::vec4 perspective;
            glm::quat rotationQuat;
            if (glm::decompose(localMatrix, scale, rotationQuat, translation, skew, perspective)) {
                auto& transform = m_Ctx.SelectedEntity.GetComponent<Prism::TransformComponent>();
                transform.Translation = translation;
                transform.Scale = scale;
                // glm::decompose retorna um quaternion; a engine guarda
                // rotacao em euler-graus (ver TransformComponent,
                // Components.h) - glm::eulerAngles devolve radianos na
                // ordem (pitch=x, yaw=y, roll=z), que e exatamente o que
                // TransformComponent::GetTransform() espera de volta via
                // yawPitchRoll (ver Components.h). Sem isso o objeto
                // "pularia" de rotacao toda vez que o gizmo fosse usado.
                glm::vec3 eulerRad = glm::eulerAngles(rotationQuat);
                transform.Rotation = glm::degrees(eulerRad);
            }
        }
        else if (m_GizmoWasUsingLastFrame) {
            // Arraste acabou de terminar neste frame (IsUsing() era true no
            // frame anterior, false agora) - empurra UM TransformCommand
            // cobrindo o gesto inteiro, mesmo padrao de
            // IsItemDeactivatedAfterEdit() nos DragFloat3 da Properties
            // panel. So gera comando se algo de fato mudou (evita entulhar
            // o historico de undo com um clique que nao moveu nada).
            const auto& after = m_Ctx.SelectedEntity.GetComponent<Prism::TransformComponent>();
            bool changed = m_GizmoTransformBeforeEdit.Translation != after.Translation
                || m_GizmoTransformBeforeEdit.Rotation != after.Rotation
                || m_GizmoTransformBeforeEdit.Scale != after.Scale;
            if (changed) {
                m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(
                    m_Ctx.SelectedEntity, m_GizmoTransformBeforeEdit, after));
            }
        }

        m_GizmoWasUsingLastFrame = isUsing;
    }

}
