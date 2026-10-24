#include "PropertiesPanel.h"
#include <imgui.h>
#include <Prism/Animation/AnimationSystem.h>
#include "../Commands/EditorCommands.h"
#include "ComponentEditUtils.h"
#include <algorithm>
#include <string>
#include <vector>

// PropertiesPanel_Animator.cpp
// UI do AnimatorComponent: escolhe o modelo (esqueleto/clipes), o clipe inicial,
// loop e velocidade. Os botoes de PREVIA tocam o clipe no proprio editor
// (so visual - nao altera a cena salva).

namespace PrismEditor {

    // Primeiro MeshRenderer com modelo na entidade ou nos descendentes (para "Usar modelo da malha").
    static Prism::AssetID FindModelInSubtree(Prism::Scene& scene, entt::entity handle, int depth = 0) {
        auto& registry = scene.GetRegistry();
        if (depth > 32 || !registry.valid(handle))
            return {};
        if (auto* meshRenderer = registry.try_get<Prism::MeshRendererComponent>(handle))
            if (meshRenderer->ModelAsset.IsValid())
                return meshRenderer->ModelAsset;
        if (auto* relationship = registry.try_get<Prism::RelationshipComponent>(handle)) {
            for (entt::entity child : relationship->Children) {
                Prism::AssetID found = FindModelInSubtree(scene, child, depth + 1);
                if (found.IsValid())
                    return found;
            }
        }
        return {};
    }

    void PropertiesPanel::DrawAnimatorUI() {
        auto& animator = m_Ctx.SelectedEntity.GetComponent<Prism::AnimatorComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Animator", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const Prism::AnimatorComponent before = animator; // ver ComponentEditUtils.h

            auto project = Prism::Project::GetActive();
            std::filesystem::path modelPath = (project && animator.ModelAsset.IsValid()) ? project->GetAssetRegistry().AbsolutePath(animator.ModelAsset) : std::filesystem::path{};
            const bool brokenLink = animator.ModelAsset.IsValid() && modelPath.empty();

            std::string label = !animator.ModelAsset.IsValid() ? "Modelo: nenhum"
                : brokenLink ? "Modelo: [vinculo quebrado]"
                : "Modelo: " + modelPath.filename().string();
            ImGui::Button(label.c_str(), ImVec2(-1, 0));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("O modelo (.glb/.gltf/.fbx) de onde vem o esqueleto e os clipes.\nTodas as malhas desse modelo, nesta entidade ou abaixo dela, sao animadas.");

            if (ImGui::SmallButton("Usar modelo da malha")) {
                Prism::AssetID found = FindModelInSubtree(*m_Ctx.ActiveScene, m_Ctx.SelectedEntity.GetHandle());
                if (found.IsValid()) {
                    Prism::AnimationSystem::Stop(m_Ctx.SelectedEntity);
                    animator.ModelAsset = found;
                    animator.Clip.clear();
                    std::vector<std::string> clips = Prism::AnimationSystem::GetClipNames(found);
                    if (!clips.empty())
                        animator.Clip = clips[0];
                    CommitComponentEdit(m_Ctx, before, animator, "Animator");
                }
                else {
                    PRISM_WARN("Nenhum Mesh Renderer com modelo importado nesta entidade ou abaixo dela.");
                }
            }

            std::vector<std::string> clips = Prism::AnimationSystem::GetClipNames(animator.ModelAsset);
            if (animator.ModelAsset.IsValid() && clips.empty())
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "Este modelo nao tem animacoes.");

            // Clipe inicial.
            {
                const char* preview = animator.Clip.empty() ? "(nenhum)" : animator.Clip.c_str();
                if (ImGui::BeginCombo("Clipe", preview)) {
                    if (ImGui::Selectable("(nenhum)", animator.Clip.empty())) {
                        animator.Clip.clear();
                        CommitComponentEdit(m_Ctx, before, animator, "Animator");
                    }
                    for (const std::string& clip : clips) {
                        if (ImGui::Selectable(clip.c_str(), clip == animator.Clip)) {
                            animator.Clip = clip;
                            CommitComponentEdit(m_Ctx, before, animator, "Animator");
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            if (ImGui::Checkbox("Tocar ao iniciar", &animator.PlayOnStart))
                CommitComponentEdit(m_Ctx, before, animator, "Animator");
            if (ImGui::Checkbox("Repetir (Loop)", &animator.Loop))
                CommitComponentEdit(m_Ctx, before, animator, "Animator");

            if (ImGui::SliderFloat("Velocidade", &animator.Speed, 0.0f, 4.0f, "%.2fx"))
                animator.Speed = std::clamp(animator.Speed, 0.0f, 20.0f);
            TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");

            if (ImGui::Checkbox("Root motion", &animator.RootMotion))
                CommitComponentEdit(m_Ctx, before, animator, "Animator");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("O movimento horizontal (XZ) do osso raiz do clipe move a ENTIDADE do Animator\n(e some da pose, para o personagem nao escorregar). Nao use junto de um Rigidbody\ndinamico na mesma entidade: a fisica sobrescreve a posicao. So vale em jogo.");

            DrawAnimatorGraphUI(animator, clips);

            // Eventos de animacao.
            if (ImGui::TreeNode("Eventos de animacao")) {
                ImGui::TextDisabled("No script da entidade: function OnAnimationEvent(name) (Lua) / OnAnimationEvent(const char*) (C++).");
                int removeEvent = -1;
                for (int i = 0; i < (int)animator.Events.size(); i++) {
                    ImGui::PushID(i);
                    auto& ev = animator.Events[i];
                    ImGui::SetNextItemWidth(110);
                    if (ImGui::BeginCombo("##c", ev.Clip.empty() ? "(clipe)" : ev.Clip.c_str())) {
                        for (const std::string& clip : clips)
                            if (ImGui::Selectable(clip.c_str(), clip == ev.Clip)) {
                                ev.Clip = clip;
                                CommitComponentEdit(m_Ctx, before, animator, "Animator");
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70);
                    ImGui::DragFloat("##t", &ev.Time, 0.01f, 0.0f, 3600.0f, "%.2f s");
                    TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
                    ImGui::SameLine();
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s", ev.Name.c_str());
                    ImGui::SetNextItemWidth(110);
                    if (ImGui::InputText("##n", buf, sizeof(buf)))
                        ev.Name = buf;
                    TrackContinuousEdit(m_Ctx, m_AnimatorBeforeEdit, animator, "Animator");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X"))
                        removeEvent = i;
                    ImGui::PopID();
                }
                if (removeEvent >= 0) {
                    animator.Events.erase(animator.Events.begin() + removeEvent);
                    CommitComponentEdit(m_Ctx, before, animator, "Animator");
                }
                if (ImGui::SmallButton("+ Evento")) {
                    Prism::AnimEvent ev;
                    ev.Clip = animator.Clip.empty() && !clips.empty() ? clips[0] : animator.Clip;
                    ev.Name = "evento";
                    animator.Events.push_back(ev);
                    CommitComponentEdit(m_Ctx, before, animator, "Animator");
                }
                ImGui::TreePop();
            }

            // Previa no editor (cena parada).
            if (!m_Ctx.ActiveScene->IsRunning()) {
                ImGui::BeginDisabled(animator.Clip.empty() || clips.empty());
                const bool previewing = Prism::AnimationSystem::IsPlaying(m_Ctx.SelectedEntity);
                if (ImGui::SmallButton(previewing ? "Pausar previa##Anim" : "Tocar previa##Anim")) {
                    if (previewing)
                        Prism::AnimationSystem::SetPaused(m_Ctx.SelectedEntity, true);
                    else if (!Prism::AnimationSystem::GetCurrentClip(m_Ctx.SelectedEntity).empty()
                             && Prism::AnimationSystem::GetCurrentClip(m_Ctx.SelectedEntity) == animator.Clip
                             && animator.Runtime.Ptr && animator.Runtime.Ptr->Paused)
                        Prism::AnimationSystem::SetPaused(m_Ctx.SelectedEntity, false);
                    else
                        Prism::AnimationSystem::Play(m_Ctx.SelectedEntity, animator.Clip, 0.0f, -1);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("Parar (pose de bind)##Anim"))
                    Prism::AnimationSystem::Stop(m_Ctx.SelectedEntity);

                if (animator.Runtime.Ptr && animator.Runtime.Ptr->Clip >= 0) {
                    float duration = Prism::AnimationSystem::GetDuration(m_Ctx.SelectedEntity);
                    float time = Prism::AnimationSystem::GetTime(m_Ctx.SelectedEntity);
                    ImGui::TextDisabled("%s  %.2f / %.2f s", Prism::AnimationSystem::GetCurrentClip(m_Ctx.SelectedEntity).c_str(), time, duration);
                }
            }
            else {
                ImGui::TextDisabled("Em jogo: entity:PlayAnimation(\"nome\", 0.2) troca de clipe (0.2 = crossfade em segundos).");
            }
        }
        if (!keepOpen) {
            Prism::AnimationSystem::Stop(m_Ctx.SelectedEntity);
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::AnimatorComponent>>(m_Ctx.SelectedEntity, "Animator"));
        }
    }

}
