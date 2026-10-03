#pragma once

// ============================================================================
// EditorCamera.h
// Camera livre do editor (a que a viewport usa, nunca a CameraComponent da
// cena). Antes eram 6 membros soltos de EditorLayer; agora vivem aqui para o
// painel Viewport (que a move) e o painel Propriedades (que a exibe)
// compartilharem o MESMO estado via EditorContext.
// ============================================================================

#include <glm/glm.hpp>

namespace PrismEditor {

    struct EditorCamera {
        // ============================================================
        // Camera LIVRE de voo da viewport principal do editor
        // ============================================================
        // Controles no estilo Godot (ver RenderViewportPanel):
        //   - Segurar botao DIREITO do mouse sobre a viewport entra em
        //     "modo voar": cursor e escondido e LOCKADO (GLFW_CURSOR_DISABLED
        //     - mesmo modo que jogos FPS usam), movimento do mouse gira a
        //     camera (yaw/pitch).
        //   - Enquanto voa:
        //       WASD = mover no plano (W frente, S tras, A esq, D dir)
        //       Q/E  = descer/subir (no eixo Y do MUNDO, absoluto)
        //       Shift = 3x boost;  Alt = 3x slow (ajuste fino)
        //       Scroll = ajusta a velocidade BASE de movimento
        //   - Soltar o RMB sai do modo voar (cursor volta ao normal).
        //   - Fora do modo voar, scroll sobre a viewport faz DOLLY:
        //     avanca/recua a camera ao longo da direcao que ela olha,
        //     sem mudar a rotacao (mesma convencao da Godot para "zoom"
        //     no editor).
        //
        // W/E/R SOZINHOS (sem RMB) continuam trocando a operacao do
        // gizmo (ver RenderTransformGizmo) - sem conflito, porque o
        // movimento exige o botao direito segurado.
        //
        // Esta e SEMPRE a camera usada por ViewportPanel::RenderScene()/painel Viewport,
        // mesmo quando a cena tem uma CameraComponent marcada como Primary
        // - a camera de jogo tem sua propria preview separada (ver
        // m_CameraPreviewFramebuffer / RenderCameraPreview acima),
        // exatamente como Unity/Unreal/Godot fazem. Isso evita o problema
        // de "ficar preso" dentro de um mesh (ex: camera de personagem
        // posicionada dentro da capsula de colisao) sem visao de trabalho
        // na viewport principal.
        //
        // A camera tem POSICAO livre; yaw/pitch so definem a direcao que
        // ela olha, nunca um alvo fixo. Os valores iniciais dao uma visao
        // de cima e de lado da origem no primeiro frame.
        glm::vec3 Position = { 4.5f, 2.5f, -3.1f };

        float Yaw = -35.0f;   // graus

        float Pitch = 25.0f;  // graus

        // Velocidade base de movimento da camera livre (unidades/segundo) -
        // ajustavel via scroll enquanto em modo voar (ver
        // RenderViewportPanel). Shift multiplica isso por 3x enquanto
        // segurado, Alt por 1/3. Comeca em 5.0 (valor confortavel para
        // uma cena de escala "1 unidade = 1 metro" como os cubos de
        // exemplo do editor).
        float MoveSpeed = 5.0f;

        // Estado do "modo voar" da camera do editor (RMB segurado):
        // verdadeiro enquanto o usuario esta segurando o botao direito
        // sobre a viewport - ver ViewportPanel::OnImGuiRender(). Enquanto ativo:
        //   - o cursor e escondido/lockado via GLFW_CURSOR_DISABLED
        //   - movimento do mouse gira a camera (yaw/pitch)
        //   - WASD move no plano, Q/E sobe/desce, Shift=boost, Alt=slow
        //   - scroll ajusta a velocidade base
        // Ao soltar o RMB, o cursor volta ao normal e este flag vira
        // false. Picking/gizmo do ImGuizmo ficam SUSPENSOS enquanto este
        // flag e true (cursor esta invisivel - nao faz sentido clicar em
        // nada).
        bool LookActive = false;

        // Ignora o delta do mouse no primeiro frame apos entrar em modo
        // voar - em algumas plataformas, o GLFW reseta a posicao virtual
        // do cursor ao trocar para GLFW_CURSOR_DISABLED, o que geraria
        // um "snap" grande e perceptivel na rotacao da camera num unico
        // frame. Setado para true ao entrar em modo voar; consumido (e
        // zerado) no frame seguinte.
        bool LookSkipNextDelta = false;

        // Matriz de view a partir de Position/Yaw/Pitch.
        glm::mat4 GetViewMatrix() const;
    };

}
