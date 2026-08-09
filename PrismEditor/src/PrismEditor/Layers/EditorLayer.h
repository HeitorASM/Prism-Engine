#pragma once

// ============================================================================
// EditorLayer.h
// O editor de verdade: dockspace com viewport 3D, painel de hierarquia,
// painel de propriedades e console. So existe depois que um Project esta
// ativo (ver ProjectManagerLayer).
//
// Agora possui uma Scene real (Prism::Scene) com Entities de verdade -
// a Hierarchy panel lista as entidades da cena, clicar seleciona uma, e a
// Properties panel edita o TransformComponent da entidade selecionada. A
// Viewport desenha todas as entidades com MeshRendererComponent, cada uma
// com seu proprio TransformComponent, num Framebuffer offscreen mostrado
// via ImGui::Image.
//
// A Scene agora tambem persiste em disco (Prism::SceneSerializer, formato
// binario .prismmap dentro de Project::GetMapDirectory()) - ver
// LoadOrCreateScene() e SaveActiveScene().
//
// Undo/Redo (Ctrl+Z / Ctrl+Y) cobre: mover/rotacionar/escalar entidade,
// mudar cor, criar entidade, excluir entidade - ver m_CommandHistory e
// PrismEditor/Commands/EditorCommands.h.
//
// Content Browser (m_ContentBrowser) mostra os arquivos do projeto ativo
// (Assets/Maps/Scripts/Cache) e permite navegar/selecionar; duplo-clique
// num .prismmap chama LoadScene() para abrir aquele mapa.
// ============================================================================

#include <Prism.h>
#include <glm/glm.hpp>
#include "../Commands/EditorCommands.h"
#include "../Panels/ConsolePanel.h"
#include "../Panels/ContentBrowserPanel.h"
#include "../Play/PlayWindow.h"

namespace PrismEditor {

    class EditorLayer : public Prism::Layer {
    public:
        EditorLayer();

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnImGuiRender() override;
        void OnEvent(Prism::Event& event) override;

    private:
        void RenderDockspace();
        void RenderMenuBar();
        void RenderViewportPanel();
        void RenderHierarchyPanel();
        // Desenha um unico node da arvore (e recursivamente seus filhos) -
        // extraido de RenderHierarchyPanel() porque a recursao precisa
        // chamar a si mesma para cada nivel da hierarquia.
        void RenderHierarchyNode(Prism::Entity entity);

        // Chamado pelo botao "Play" da menu bar - abre a PlayWindow (janela
        // separada do SO, ver Play/PlayWindow.h) com uma COPIA clonada da
        // Scene ativa. A Scene de edicao nunca e tocada por scripts/fisica
        // - diferente da abordagem antiga (Play dentro da propria viewport,
        // com snapshot/restore e popup de "salvar antes de rodar?"), que
        // foi removida quando esta janela separada passou a existir (ver
        // README "Nota sobre Play").
        void OnPlayButtonClicked();

        // Chamado pelo botao "Parar" (so aparece quando a PlayWindow esta
        // aberta) - fecha a PlayWindow (para scripts/fisica da copia e
        // destroi a janela). A Scene de edicao, que nunca foi tocada, nao
        // precisa de nenhuma restauracao.
        void OnStopButtonClicked();

        void RenderPropertiesPanel();
        // ^ RenderPropertiesPanel() desenha uma secao por component que a
        //   entidade selecionada ja tem (com um "X" para remover, exceto
        //   Transform); o botao "+ Add Component" no final fica em
        //   RenderAddComponentButton() por ser um bloco grande o bastante
        //   (o popup com a lista de components disponiveis) para nao
        //   poluir o corpo principal da funcao.
        void RenderAddComponentButton();
        void RenderConsolePanel();
        // ^ RenderConsolePanel() so delega para m_ConsolePanel.OnImGuiRender()
        //   - ver EditorLayer.cpp. O painel de verdade vive em
        //   PrismEditor/Panels/ConsolePanel.h porque tem estado e logica
        //   proprios (filtros, auto-scroll) grandes o bastante para nao
        //   fazer sentido inline aqui.
        void RenderContentBrowserPanel();
        // ^ mesmo padrao do Console: delega para m_ContentBrowser.OnImGuiRender().

        // Painel "Camera" - mostra a visao da entidade com
        // CameraComponent::Primary=true, renderizada num framebuffer PROPRIO
        // (m_CameraPreviewFramebuffer), separado do m_ViewportFramebuffer da
        // viewport principal do editor. Isso e deliberado: a viewport
        // principal do editor NUNCA e substituida pela camera de jogo (ela
        // continua sempre sendo a camera de orbita livre, do mesmo jeito que
        // Unity/Unreal/Godot fazem) - assim voce nunca fica "preso" dentro
        // de um mesh ou sem visao de trabalho so por ter marcado uma camera
        // como Primary. Ver RenderCameraPreview() em EditorLayer.cpp.
        void RenderCameraPreviewPanel();

        // Carrega o mapa em 'path' na Scene ativa, substituindo o que
        // estiver aberto no momento (sem perguntar "salvar antes?" ainda -
        // ver nota no README). Chamado tanto por LoadOrCreateScene()
        // (StartMap na abertura do editor) quanto pelo duplo-clique num
        // .prismmap no Content Browser. Retorna false se a leitura falhar -
        // a Scene ativa permanece intocada nesse caso (ver
        // SceneSerializer::Deserialize). Em caso de sucesso, atualiza
        // m_CurrentMapPath para 'path' - dai em diante "Salvar Mapa" grava
        // de volta neste mesmo arquivo.
        bool LoadScene(const std::filesystem::path& mapPath);

        // Cria uma Scene nova vazia (so o nome, sem entidades) e a torna a
        // Scene ativa. m_CurrentMapPath e limpo - a proxima vez que "Salvar
        // Mapa" for usado, se comporta como "Salvar Como" (ainda nao ha
        // arquivo associado a esta cena). Chamado pelo menu Arquivo > Novo Mapa.
        void NewMap();

        // Salva a Scene ativa. Se m_CurrentMapPath ja aponta para um
        // arquivo (cena carregada do disco, ou ja salva antes nesta
        // sessao), grava nele direto. Caso contrario (cena nova, nunca
        // salva), se comporta como SaveActiveSceneAs() - abre o popup de
        // nome, ja que nao ha "onde" salvar ainda. Chamado pelo menu
        // Arquivo > Salvar Mapa / Ctrl+S.
        void SaveActiveScene();

        // Sempre abre o popup pedindo um nome (mesmo se a cena ja tiver um
        // arquivo associado) e salva num arquivo NOVO dentro de
        // Project::GetMapDirectory() - nao sobrescreve o mapa anterior.
        // m_CurrentMapPath passa a apontar para o arquivo recem-criado.
        // Chamado pelo menu Arquivo > Salvar Como.
        void SaveActiveSceneAs();

        // Desenha o popup modal de nome usado por SaveActiveSceneAs() -
        // chamado a cada frame de RenderDockspace() (ImGui::OpenPopup
        // exige isso mesmo quando o popup esta fechado, ver EditorLayer.cpp).
        void RenderSaveAsPopup();

        // Tenta carregar Project::GetConfig().StartMap; se nao existir
        // ainda (projeto novo, primeira vez abrindo o editor), cria uma
        // cena de exemplo em memoria em vez de falhar.
        void LoadOrCreateScene();

        // Desenha todas as entidades da Scene com MeshRendererComponent
        // dentro do m_ViewportFramebuffer, usando a camera de orbita do
        // editor (m_CameraYaw/Pitch/Distance) - NAO usa nenhuma
        // CameraComponent::Primary aqui, mesmo que exista uma na cena (ver
        // nota em RenderCameraPreviewPanel() acima sobre o motivo). Chamado
        // de OnUpdate, antes do ImGui - o resultado (uma textura de cor) e
        // que aparece dentro do painel Viewport neste mesmo frame.
        void RenderScene(float deltaTime);

        // Desenha a cena a partir do ponto de vista da entidade com
        // CameraComponent::Primary=true (se houver) dentro de
        // m_CameraPreviewFramebuffer - mesma logica de desenho de
        // RenderScene(), mas com a view/projection vindas da camera de jogo
        // em vez da orbita do editor. Chamado de OnUpdate logo depois de
        // RenderScene(). Sem entidade Primary na cena, so limpa o
        // framebuffer (ver EditorLayer.cpp) - RenderCameraPreviewPanel()
        // mostra uma mensagem nesse caso em vez da imagem.
        void RenderCameraPreview(float deltaTime);

        // Desenha um wireframe de frustum (Renderer::DrawLines) para toda
        // entidade com CameraComponent na cena - a Primary usa uma cor
        // diferente das demais, para ficar claro qual camera o modo Play
        // vai usar. Chamado de dentro de RenderScene() (nao de
        // RenderCameraPreview() - nao faz sentido a camera desenhar o
        // proprio gizmo dela mesma na sua propria preview), depois de
        // desenhar os meshes - ver EditorLayer.cpp.
        void RenderCameraGizmos(const glm::mat4& viewProjection);

        // Desenha o wireframe do ColliderComponent (Box/Sphere/Capsule) da
        // entidade atualmente SELECIONADA (m_SelectedEntity) - so dela, nao
        // de toda entidade com Collider da cena, para nao poluir a viewport
        // (diferente de RenderCameraGizmos, que desenha todas as cameras
        // sempre). Isso cobre o caso de querer ver a capsula de colisao de
        // um Character/player para saber exatamente onde ela esta (ex: para
        // posicionar uma camera fora dela) - clique na entidade na
        // Hierarchy ou na propria viewport para ver o gizmo. Chamado de
        // dentro de RenderScene(), depois de RenderCameraGizmos().
        void RenderSelectedColliderGizmo(const glm::mat4& viewProjection);

        // Garante que no maximo UMA entidade da cena tenha
        // CameraComponent::Primary = true: ao marcar 'newPrimary' como
        // Primary, desmarca qualquer outra que estivesse marcada.
        // Chamado pela Properties panel quando o checkbox "Primary" e
        // ligado - ver RenderPropertiesPanel().
        void SetPrimaryCamera(Prism::Entity newPrimary);

    private:
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        float m_ViewportSize[2] = { 0.0f, 0.0f };

        // Framebuffer offscreen onde a cena 3D e desenhada. O color
        // attachment dele e o que vira ImGui::Image() dentro do painel
        // Viewport - ver RenderViewportPanel().
        Prism::Scope<Prism::Framebuffer> m_ViewportFramebuffer;

        // Framebuffer offscreen SEPARADO, usado so pelo painel "Camera"
        // (RenderCameraPreviewPanel/RenderCameraPreview) para mostrar a
        // visao da camera de jogo Primary sem nunca tocar no
        // m_ViewportFramebuffer da viewport principal - ver nota em
        // RenderCameraPreviewPanel() no header acima sobre o motivo dessa
        // separacao. Criado sob demanda (fica nulo ate a primeira vez que o
        // painel Camera e aberto) para nao gastar uma textura de GPU extra
        // em projetos que nunca abrem esse painel.
        Prism::Scope<Prism::Framebuffer> m_CameraPreviewFramebuffer;
        float m_CameraPreviewSize[2] = { 0.0f, 0.0f };

        // A cena ativa do editor. Por ora criada em memoria com uma entidade
        // de exemplo em OnAttach() - salvar/carregar cenas do disco (dentro
        // de Project::GetMapDirectory(), ver Project.h) e o proximo passo
        // natural depois deste (ver README, secao "Proximos passos").
        Prism::Ref<Prism::Scene> m_ActiveScene;

        // Caminho (absoluto) do arquivo .prismmap associado a m_ActiveScene
        // - vazio significa "esta cena ainda nao foi salva em lugar
        // nenhum" (cena nova, criada por NewMap() ou pela cena de exemplo
        // do primeiro OnAttach). "Salvar Mapa" grava neste caminho quando
        // ele existe; quando esta vazio, se comporta como "Salvar Como".
        std::filesystem::path m_CurrentMapPath;

        // Estado do popup modal de "Salvar Como" (ver RenderSaveAsPopup).
        bool m_ShowSaveAsPopup = false;
        char m_SaveAsNameBuffer[128] = "";

        // Janela de Play (janela separada do SO, ver Play/PlayWindow.h) -
        // dona de uma Scene CLONADA, nunca a mesma instancia de
        // m_ActiveScene. OnPlayButtonClicked()/OnStopButtonClicked() abrem/
        // fecham; OnUpdate() e chamado uma vez por frame (ver
        // EditorLayer::OnUpdate) enquanto m_PlayWindow.IsOpen().
        PrismEditor::PlayWindow m_PlayWindow;

        // Entidade atualmente selecionada na Hierarchy panel. Invalida
        // (Entity{}) quando nada esta selecionado.
        Prism::Entity m_SelectedEntity;

        // Historico de undo/redo do editor (ver Prism::CommandHistory /
        // PrismEditor::EditorCommands). Compartilhado por toda edicao de
        // Scene feita atraves da UI - Transform, cor, criar/excluir
        // entidade.
        Prism::CommandHistory m_CommandHistory;

        // Estado "antes" capturado no momento em que o usuario COMECA a
        // arrastar um DragFloat3/ColorEdit3 na Properties panel (via
        // ImGui::IsItemActivated()) - usado para montar um unico
        // TransformCommand/MeshColorCommand quando o arraste termina, em
        // vez de um comando por frame de movimento do mouse. Valido apenas
        // enquanto um drag esta em andamento.
        Prism::TransformComponent m_TransformBeforeEdit;
        glm::vec3 m_ColorBeforeEdit{ 0.0f };

        // Painel de Console - le do Prism::LogBuffer (ver Prism.h) e tem
        // seu proprio estado de UI (filtros, auto-scroll), por isso vive em
        // uma classe separada em vez de ser so metodos soltos aqui.
        ConsolePanel m_ConsolePanel;

        // Painel de navegacao pelos arquivos do projeto (Assets/Maps/
        // Scripts/Cache) - primeira forma de ver o conteudo de um projeto
        // sem sair do editor. Duplo-clique num .prismmap chama LoadScene().
        ContentBrowserPanel m_ContentBrowser;

        // Camera de orbita da viewport principal do editor (nao e a camera
        // FPS/TPS de jogo mencionada no guia). Controle: botao direito do
        // mouse segurado sobre a viewport + arrastar orbita; scroll
        // aproxima/afasta. Esta e SEMPRE a camera usada por RenderScene()/
        // painel Viewport, mesmo quando a cena tem uma CameraComponent
        // marcada como Primary - a camera de jogo tem sua propria preview
        // separada (ver m_CameraPreviewFramebuffer / RenderCameraPreviewPanel
        // acima), exatamente como Unity/Unreal/Godot fazem. Isso evita o
        // problema de "ficar preso" dentro de um mesh (ex: camera de
        // personagem posicionada dentro da capsula de colisao) sem visao de
        // trabalho na viewport principal.
        float m_CameraYaw = -35.0f;   // graus
        float m_CameraPitch = 25.0f;  // graus
        float m_CameraDistance = 6.0f;
    };

}
