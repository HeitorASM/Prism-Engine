#include "PropertiesPanel.h"
#include <imgui.h>
#include "../Commands/EditorCommands.h"
#include "ComponentEditUtils.h"
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <string>

// PropertiesPanel_Environment.cpp
// UI do EnvironmentComponent (ambiente + skybox da CENA) no painel
// Propriedades. Antes estas opcoes viviam no menu "Renderizacao" e eram
// gravadas no .prismproj; agora pertencem a cena e vao no .prismmap.
// DrawEnvironmentUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawEnvironmentUI() {
        auto& env = m_Ctx.SelectedEntity.GetComponent<Prism::EnvironmentComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Environment", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const Prism::EnvironmentComponent before = env; // ver ComponentEditUtils.h

            // So vale o primeiro Environment da cena: avisa se este nao e ele.
            {
                auto view = m_Ctx.ActiveScene->GetRegistry().view<Prism::EnvironmentComponent>();
                auto first = view.begin();
                if (first != view.end() && Prism::Entity(*first, m_Ctx.ActiveScene.get()) != m_Ctx.SelectedEntity)
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Ja existe outro Environment na cena - so o primeiro vale.");
            }

            ImGui::SeparatorText("Luz ambiente");
            ImGui::DragFloat("Intensidade##Env", &env.Ambient, 0.005f, 0.0f, 2.0f);
            TrackContinuousEdit(m_Ctx, m_EnvironmentBeforeEdit, env, "Environment");
            ImGui::ColorEdit3("Ceu##Env", glm::value_ptr(env.Zenith));
            TrackContinuousEdit(m_Ctx, m_EnvironmentBeforeEdit, env, "Environment");
            ImGui::ColorEdit3("Horizonte##Env", glm::value_ptr(env.Horizon));
            TrackContinuousEdit(m_Ctx, m_EnvironmentBeforeEdit, env, "Environment");
            ImGui::ColorEdit3("Chao##Env", glm::value_ptr(env.Ground));
            TrackContinuousEdit(m_Ctx, m_EnvironmentBeforeEdit, env, "Environment");

            ImGui::SeparatorText("Skybox");
            const char* skyNames[] = { "Cor chapada", "Gradiente", "Panorama" };
            int skyIndex = (int)env.Sky;
            if (ImGui::Combo("Fundo##Env", &skyIndex, skyNames, IM_ARRAYSIZE(skyNames))) {
                env.Sky = (Prism::SkyMode)skyIndex;
                CommitComponentEdit(m_Ctx, before, env, "Environment");
            }

            if (env.Sky == Prism::SkyMode::Solid) {
                ImGui::ColorEdit3("Cor##EnvSky", glm::value_ptr(env.SkyColor));
                TrackContinuousEdit(m_Ctx, m_EnvironmentBeforeEdit, env, "Environment");
            }
            else if (env.Sky == Prism::SkyMode::Gradient) {
                ImGui::TextDisabled("Usa as cores de Ceu/Horizonte/Chao acima.");
            }
            else {
                auto project = Prism::Project::GetActive();
                std::filesystem::path texturePath = (project && env.SkyTexture.IsValid()) ? project->GetAssetRegistry().AbsolutePath(env.SkyTexture) : std::filesystem::path{};
                bool brokenLink = env.SkyTexture.IsValid() && texturePath.empty();

                std::string label = !env.SkyTexture.IsValid() ? "Imagem: arraste um panorama aqui"
                    : brokenLink ? "Imagem: [vinculo quebrado]"
                    : "Imagem: " + texturePath.filename().string();
                ImGui::Button(label.c_str(), ImVec2(-1, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_IMAGE_PATH")) {
                        std::filesystem::path dropped((const char*)payload->Data); // caminho ABSOLUTO (ver ContentBrowserPanel)
                        if (project) {
                            auto& registry = project->GetAssetRegistry();
                            Prism::AssetID id = registry.IdForPath(registry.ToRelative(dropped));
                            if (id.IsValid()) {
                                env.SkyTexture = id;
                                CommitComponentEdit(m_Ctx, before, env, "Environment");
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Arraste uma imagem equirretangular (.hdr, .png, .jpg) do painel Conteudo do Projeto.");

                if (env.SkyTexture.IsValid()) {
                    if (ImGui::SmallButton("Remover imagem##EnvSky")) {
                        env.SkyTexture = Prism::AssetID{};
                        CommitComponentEdit(m_Ctx, before, env, "Environment");
                    }
                }
                else {
                    ImGui::TextDisabled("Sem imagem: o fundo cai para cor chapada.");
                }

                ImGui::DragFloat("Rotacao##EnvSky", &env.SkyRotation, 0.5f, -180.0f, 180.0f, "%.1f graus");
                TrackContinuousEdit(m_Ctx, m_EnvironmentBeforeEdit, env, "Environment");
                ImGui::DragFloat("Brilho##EnvSky", &env.SkyIntensity, 0.01f, 0.0f, 16.0f);
                TrackContinuousEdit(m_Ctx, m_EnvironmentBeforeEdit, env, "Environment");
            }
            ImGui::TextDisabled("O fundo nao ilumina a cena: a luz ambiente vem sempre do gradiente.");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::EnvironmentComponent>>(m_Ctx.SelectedEntity, "Environment"));
    }

}
