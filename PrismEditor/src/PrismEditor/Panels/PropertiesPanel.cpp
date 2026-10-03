#include "PropertiesPanel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "ContentBrowserPanel.h"
#include "ScriptEditorPanel.h"
#include "../Play/PlayWindow.h"
#include "../Commands/EditorCommands.h"
#include "../Core/EntityOps.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace PrismEditor {

    void PropertiesPanel::OnImGuiRender() {
        ImGui::Begin("Propriedades");

        if (!m_Ctx.SelectedEntity) {
            ImGui::TextDisabled("Nada selecionado.");
            ImGui::End();
            return;
        }

        auto& tag = m_Ctx.SelectedEntity.GetComponent<Prism::TagComponent>();
        char nameBuffer[256];
        strncpy(nameBuffer, tag.Tag.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        if (ImGui::InputText("Nome", nameBuffer, sizeof(nameBuffer)))
            tag.Tag = nameBuffer;

        ImGui::Separator();

        RenderPrefabInstanceSection();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::TransformComponent>())
            DrawTransformUI();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::MeshRendererComponent>())
            DrawMeshRendererUI();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::MaterialComponent>())
            DrawMaterialUI();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::LightComponent>())
            DrawLightUI();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::ColliderComponent>())
            DrawColliderUI();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::RigidBodyComponent>())
            DrawRigidBodyUI();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::RaycastComponent>())
            DrawRaycastUI();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::CameraComponent>())
            DrawCameraUI();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::ScriptComponent>())
            DrawScriptUI();

        ImGui::Dummy(ImVec2(0, 8));
        RenderAddComponentButton();

        ImGui::Separator();
        ImGui::TextDisabled("Camera do editor (livre)");
        ImGui::Text("Posicao: (%.2f, %.2f, %.2f)", m_Ctx.Camera.Position.x, m_Ctx.Camera.Position.y, m_Ctx.Camera.Position.z);
        ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_Ctx.Camera.Yaw, m_Ctx.Camera.Pitch);
        ImGui::Text("Velocidade: %.2f u/s", m_Ctx.Camera.MoveSpeed);
        if (m_Ctx.Camera.LookActive)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "MODO VOAR ATIVO (solte o RMB para sair)");
        else
            ImGui::TextDisabled("RMB: modo voar (WASD/QE, Shift=boost, Alt=slow, scroll=velocidade)");
        ImGui::TextDisabled("Fora do voo: scroll sobre a viewport = dolly (avanca/recua)");

        ImGui::End();
    }

    void PropertiesPanel::RenderAddComponentButton() {
        // Garante que o registro esta populado - ver comentario
        // equivalente em SceneSerializer::Serialize/Deserialize
        // (ComponentRegistry::RegisterAll e idempotente).
        Prism::ComponentRegistry::RegisterAll();
        const auto& registry = Prism::ComponentRegistry::GetAll();

        // So mostra o botao se sobrar pelo menos um component que a
        // entidade ainda nao tem - evita um popup vazio (a entidade ja
        // tem TransformComponent sempre, que nunca esta neste registro -
        // ver comentario em ComponentRegistration.cpp sobre o motivo).
        bool hasAnyMissing = false;
        for (auto& info : registry) {
            if (!info.Has(m_Ctx.SelectedEntity)) {
                hasAnyMissing = true;
                break;
            }
        }

        if (!hasAnyMissing) {
            ImGui::TextDisabled("(todos os components ja adicionados)");
            return;
        }

        if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup")) {
            // Loop generico sobre TODO Component registrado (ver
            // ComponentRegistry.h/ComponentRegistration.cpp). O Command real (com Undo/Redo, ver
            // AddComponentCommand<T>/EditorCommands.h) ainda e criado por
            // TIPO (nao generico) via AddComponentByRegistryName abaixo,
            // ja que o registro em si (Prism::ComponentRegistry) nao
            // conhece EditorCommands (que vive em PrismEditor, nao em
            // Prism) - ver comentario la sobre essa separacao proposital.
            for (auto& info : registry) {
                if (!info.Has(m_Ctx.SelectedEntity) && ImGui::MenuItem(info.DisplayName.c_str())) {
                    AddComponentByRegistryName(info.DisplayName);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // Instancia o AddComponentCommand<T> certo a partir do DisplayName
    // registrado em ComponentRegistry (ver ComponentRegistration.cpp) e
    // executa via EditorContext::History (com Undo/Redo).
    //
    // POR QUE ISTO NAO VIVE DENTRO DE ComponentRegistry (Prism::): porque
    // AddComponentCommand<T> e Command (Prism::Core::Command) sao dois
    // conceitos DIFERENTES - Command fica em Prism (o motor), mas
    // AddComponentCommand/RemoveComponentCommand especificamente ficam em
    // PrismEditor (EditorCommands.h), ja que "ter undo/redo ao adicionar
    // um component" e uma preocupacao do EDITOR, nao do motor em si (um
    // jogo em modo Runtime, sem editor, nunca precisaria disso). Colocar
    // esta comparacao de string aqui (em vez de um std::function dentro
    // de ComponentTypeInfo) evita que Prism::ComponentRegistry precise
    // #include EditorCommands.h - Prism nunca deveria depender de codigo
    // do Editor.
    //
    // Isto tambem e o UNICO lugar que ainda precisa saber, um por um,
    // quais Components existem - mas apenas para a etapa de "criar o
    // Command certo", nao mais para decidir SE o component deve aparecer
    // no menu ou como serializa-lo (isso already vem do registro). Um
    // Component novo que nao seja adicionado aqui simplesmente nao tera
    // Undo ao ser adicionado - ainda funciona (AddDefault do registro e
    // usado como fallback abaixo), so sem desfazer.
    void PropertiesPanel::AddComponentByRegistryName(const std::string& displayName) {
        Prism::Entity entity = m_Ctx.SelectedEntity;

        if (displayName == "Mesh Renderer")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::MeshRendererComponent>>(entity, displayName));
        else if (displayName == "Light")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::LightComponent>>(entity, displayName));
        else if (displayName == "Collider")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::ColliderComponent>>(entity, displayName));
        else if (displayName == "Rigid Body")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::RigidBodyComponent>>(entity, displayName));
        else if (displayName == "Raycast")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::RaycastComponent>>(entity, displayName));
        else if (displayName == "Script")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::ScriptComponent>>(entity, displayName));
        else if (displayName == "Camera")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::CameraComponent>>(entity, displayName));
        else if (displayName == "Material")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::MaterialComponent>>(entity, displayName));
        else {
            // Fallback generico SEM Undo - usado so se um Component for
            // registrado em ComponentRegistration.cpp mas esquecido aqui
            // (ver comentario grande acima) - melhor funcionar sem
            // desfazer do que nao adicionar nada.
            PRISM_CORE_WARN("PropertiesPanel::AddComponentByRegistryName: '", displayName, "' nao tem um AddComponentCommand mapeado - adicionando sem Undo.");
            for (auto& info : Prism::ComponentRegistry::GetAll()) {
                if (info.DisplayName == displayName) {
                    info.AddDefault(entity);
                    break;
                }
            }
        }

        // OnAfterAddInEditor (ver ComponentRegistry.h) - cobre ajustes de
        // consistencia que dependem do RESTO da cena, como o caso de
        // CameraComponent::Primary (ver ComponentRegistration.cpp) - FORA
        // do historico de Undo.
        for (auto& info : Prism::ComponentRegistry::GetAll()) {
            if (info.DisplayName == displayName && info.OnAfterAddInEditor) {
                info.OnAfterAddInEditor(entity, *m_Ctx.ActiveScene);
                break;
            }
        }
    }

    uint32_t PropertiesPanel::RenderCameraPreview(Prism::Entity cameraEntity, float width, float height) {
        if (!cameraEntity || !cameraEntity.HasComponent<Prism::CameraComponent>())
            return 0;

        // Cria o framebuffer sob demanda
        if (!m_CameraPreviewFramebuffer) {
            Prism::FramebufferSpecification fbSpec;
            fbSpec.Width = (uint32_t)std::max(width, 1.0f);
            fbSpec.Height = (uint32_t)std::max(height, 1.0f);
            m_CameraPreviewFramebuffer = Prism::Framebuffer::Create(fbSpec);
        }

        // Redimensiona se necessário
        const auto& spec = m_CameraPreviewFramebuffer->GetSpecification();
        uint32_t w = (uint32_t)std::max(width, 1.0f);
        uint32_t h = (uint32_t)std::max(height, 1.0f);
        if (spec.Width != w || spec.Height != h) {
            m_CameraPreviewFramebuffer->Resize(w, h);
        }

        m_CameraPreviewFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        float aspect = h > 0 ? (float)w / (float)h : 1.0f;
        auto& camera = cameraEntity.GetComponent<Prism::CameraComponent>();
        glm::mat4 worldTransform = m_Ctx.ActiveScene->GetWorldTransform(cameraEntity);
        glm::mat4 view = glm::inverse(worldTransform);
        glm::mat4 projection = camera.GetProjection(aspect);
        glm::vec3 worldPos = glm::vec3(worldTransform[3]);

        Prism::Renderer::DrawScene(*m_Ctx.ActiveScene, glm::value_ptr(view), glm::value_ptr(projection), glm::value_ptr(worldPos));

        m_CameraPreviewFramebuffer->Unbind();
        return m_CameraPreviewFramebuffer->GetColorAttachmentID();
    }

    void PropertiesPanel::RenderSaveMaterialPopup() {
        if (m_ShowSaveMaterialPopup) {
            ImGui::OpenPopup(kSaveMaterialPopupId);
            m_ShowSaveMaterialPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kSaveMaterialPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!m_Ctx.SelectedEntity || !m_Ctx.SelectedEntity.HasComponent<Prism::MaterialComponent>()) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Nenhuma entidade com Material selecionada.");
                ImGui::Dummy(ImVec2(0, 8));
                if (ImGui::Button("Fechar", ImVec2(120, 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            ImGui::TextWrapped("Nome do material:");
            ImGui::SetNextItemWidth(-1);
            bool confirmedByEnter = ImGui::InputText("##SaveMaterialName", m_SaveMaterialNameBuffer, sizeof(m_SaveMaterialNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            auto project = Prism::Project::GetActive();
            std::string name = m_SaveMaterialNameBuffer;

            static const std::string kForbiddenChars = "/\\:*?\"<>|";
            for (auto& c : name)
                if (kForbiddenChars.find(c) != std::string::npos)
                    c = '_';

            bool nameEmpty = name.empty();
            std::filesystem::path previewPath = project->GetMaterialDirectory() / (name + ".prismmat");
            bool wouldOverwrite = !nameEmpty && std::filesystem::exists(previewPath);

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty)
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o material.");
            else if (wouldOverwrite)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Ja existe um material com este nome - sera sobrescrito.");
            else
                ImGui::TextDisabled("%s", previewPath.filename().string().c_str());

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Salvar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty;

            if (confirmed) {
                auto& material = m_Ctx.SelectedEntity.GetComponent<Prism::MaterialComponent>();
                if (!Prism::MaterialSerializer::Serialize(material, previewPath))
                    PRISM_ERROR("Falha ao salvar material '", name, "' - ver console para detalhes.");
                else if (project) {
                    // O arquivo recem-criado (ou sobrescrito) ainda nao
                    // tem AssetID nenhum atribuido enquanto o
                    // AssetRegistry nao varrer de novo (ver
                    // Assets/AssetRegistry.h) - sem isto, o usuario
                    // arrastaria este .prismmat para vincular a outra
                    // entidade (fluxo natural logo apos salvar) e
                    // LoadMaterialAssetCommand::ResolveAssetID falharia
                    // silenciosamente (o material carregaria os campos
                    // mas SEM vinculo), ate um "Atualizar" manual no
                    // Content Browser. Refresh() e idempotente e barato
                    // (ver AssetRegistry::Refresh) - seguro de chamar
                    // aqui toda vez.
                    project->GetAssetRegistry().Refresh();
                    m_Ctx.ContentBrowser->RefreshEntries();
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void PropertiesPanel::RenderNewScriptPopup() {
        if (m_ShowNewScriptPopup) {
            ImGui::OpenPopup(kNewScriptPopupId);
            m_ShowNewScriptPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kNewScriptPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Nome do script (sem extensao):");
            ImGui::SetNextItemWidth(-1);

            bool confirmedByEnter = ImGui::InputText("##NewScriptName", m_NewScriptNameBuffer, sizeof(m_NewScriptNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            std::string name = m_NewScriptNameBuffer;
            bool nameEmpty = name.empty();

            auto project = Prism::Project::GetActive();
            std::filesystem::path previewPath = project ? (project->GetScriptDirectory() / (name + ".lua")) : std::filesystem::path{};
            bool wouldOverwrite = !nameEmpty && project && std::filesystem::exists(previewPath);

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o script.");
            }
            else if (wouldOverwrite) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Ja existe um script com este nome.");
            }
            else {
                ImGui::TextDisabled("%s", (name + ".lua").c_str());
            }

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Criar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty && !wouldOverwrite;

            if (confirmed) {
                std::filesystem::path relativePath = CreateNewScript(name);
                if (!relativePath.empty() && m_Ctx.SelectedEntity && m_Ctx.SelectedEntity.HasComponent<Prism::ScriptComponent>()) {
                    // Atribui o script recem-criado diretamente ao
                    // ScriptComponent da entidade selecionada (a mesma que
                    // tinha o botao "Novo..." clicado - ver
                    // RenderPropertiesPanel) e ja abre no editor, poupando
                    // o usuario de digitar o nome de novo no combo.
                    m_Ctx.SelectedEntity.GetComponent<Prism::ScriptComponent>().ScriptPath = relativePath.string();
                    m_Ctx.ScriptEditor->Open(project->GetScriptDirectory() / relativePath);
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    std::filesystem::path PropertiesPanel::CreateNewScript(const std::string& name) {
        auto project = Prism::Project::GetActive();
        if (!project || name.empty())
            return {};

        // Mesma sanitizacao ja usada em RenderSaveAsPopup - nomes de
        // arquivo nao devem conter caracteres proibidos pelo SO.
        std::string safeName = name;
        static const std::string kForbiddenChars = "/\\:*?\"<>|";
        for (auto& c : safeName)
            if (kForbiddenChars.find(c) != std::string::npos)
                c = '_';

        std::filesystem::path relativePath = safeName + ".lua";
        std::filesystem::path absolutePath = project->GetScriptDirectory() / relativePath;

        if (std::filesystem::exists(absolutePath)) {
            PRISM_CORE_ERROR("CreateNewScript: ja existe um script chamado '", relativePath.string(), "'.");
            return {};
        }

        // Template minimo - os tres callbacks especiais comentados, mesmo
        // vocabulario/formato dos exemplos em
        // PrismEditor/assets/ScriptExamples/ (ver example_spin.lua) - assim
        // um script novo ja mostra a API basica sem o usuario precisar ir
        // procurar um exemplo em outro lugar.
        std::ofstream file(absolutePath);
        if (!file.is_open()) {
            PRISM_CORE_ERROR("CreateNewScript: falha ao criar '", absolutePath.string(), "'.");
            return {};
        }

        file <<
            "-- " << relativePath.string() << "\n"
            "-- Tres funcoes especiais, TODAS opcionais (defina so as que precisar):\n"
            "--   OnCreate()            -- chamada uma vez, quando o Play comeca\n"
            "--   OnUpdate(deltaTime)   -- chamada toda frame enquanto o Play estiver ligado\n"
            "--   OnDestroy()           -- chamada uma vez quando o Play para\n"
            "--\n"
            "-- A variavel global `entity` ja existe dentro do script - e a PROPRIA\n"
            "-- entidade dona deste ScriptComponent.\n"
            "\n"
            "function OnCreate()\n"
            "    log(entity:GetName() .. \": OnCreate\")\n"
            "end\n"
            "\n"
            "function OnUpdate(deltaTime)\n"
            "\n"
            "end\n"
            "\n"
            "function OnDestroy()\n"
            "\n"
            "end\n";
        file.close();

        PRISM_CORE_INFO("CreateNewScript: criado '", absolutePath.string(), "'.");
        return relativePath;
    }

    std::vector<std::string> PropertiesPanel::ListProjectScripts() const {
        std::vector<std::string> result;

        auto project = Prism::Project::GetActive();
        if (!project)
            return result;

        std::error_code ec;
        std::filesystem::path scriptDir = project->GetScriptDirectory();
        if (!std::filesystem::exists(scriptDir, ec))
            return result;

        // NAO recursivo (ver comentario no header) - so arquivos .lua
        // diretamente dentro de Scripts/, comparado case-insensitive para
        // aceitar ".LUA"/".Lua" tambem (extensao pode vir de qualquer jeito
        // dependendo de como o arquivo foi criado fora do editor).
        for (const auto& entry : std::filesystem::directory_iterator(scriptDir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file())
                continue;

            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".lua")
                continue;

            result.push_back(entry.path().filename().string());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

}
