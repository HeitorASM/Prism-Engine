#include "PropertiesPanel.h"
#include "ComponentEditUtils.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>
#include <Prism/Particles/ParticleSystem.h>
#include <algorithm>
#include <filesystem>
#include <string>

// PropertiesPanel_ParticleEmitter.cpp
// UI do ParticleEmitterComponent. Fora do Play, a entidade selecionada e
// animada como preview (ParticleSystem::UpdatePreview, chamado pelo EditorLayer).

namespace PrismEditor {

    void PropertiesPanel::DrawParticleEmitterUI() {
        auto& emitter = m_Ctx.SelectedEntity.GetComponent<Prism::ParticleEmitterComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Particle Emitter", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            // Widgets continuos: TrackContinuousEdit logo apos cada um (ver ComponentEditUtils.h).
            // Discretos (checkbox): copia antes + CommitComponentEdit.
            const char* title = "Particle Emitter";

            auto checkbox = [&](const char* label, bool& value) {
                auto before = emitter;
                if (ImGui::Checkbox(label, &value))
                    CommitComponentEdit(m_Ctx, before, emitter, title);
            };
            auto dragFloat = [&](const char* label, float& value, float speed, float low, float high) {
                ImGui::DragFloat(label, &value, speed, low, high);
                TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            };

            checkbox("Habilitado", emitter.Enabled);
            checkbox("Tocar ao iniciar", emitter.PlayOnStart);
            checkbox("Loop", emitter.Loop);
            if (!emitter.Loop)
                dragFloat("Duracao (s)", emitter.Duration, 0.05f, 0.1f, 600.0f);

            ImGui::Separator(); ImGui::TextDisabled("Emissao");
            int maxParticles = (int)emitter.MaxParticles;
            if (ImGui::DragInt("Maximo de particulas", &maxParticles, 1.0f, 1, 10000))
                emitter.MaxParticles = (uint32_t)std::min(std::max(maxParticles, 1), 10000);
            TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            dragFloat("Particulas por segundo", emitter.EmissionRate, 0.5f, 0.0f, 5000.0f);
            int burst = (int)emitter.BurstCount;
            if (ImGui::DragInt("Burst inicial", &burst, 1.0f, 0, 10000))
                emitter.BurstCount = (uint32_t)std::min(std::max(burst, 0), 10000);
            TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);

            ImGui::Separator(); ImGui::TextDisabled("Movimento");
            dragFloat("Vida minima (s)", emitter.LifetimeMin, 0.02f, 0.01f, 60.0f);
            dragFloat("Vida maxima (s)", emitter.LifetimeMax, 0.02f, 0.01f, 60.0f);
            dragFloat("Velocidade minima", emitter.SpeedMin, 0.05f, 0.0f, 200.0f);
            dragFloat("Velocidade maxima", emitter.SpeedMax, 0.05f, 0.0f, 200.0f);
            dragFloat("Abertura do cone (graus)", emitter.SpreadAngle, 0.5f, 0.0f, 180.0f);
            dragFloat("Gravidade (x -9.81)", emitter.GravityScale, 0.02f, -5.0f, 5.0f);

            ImGui::Separator(); ImGui::TextDisabled("Aparencia");
            dragFloat("Tamanho inicial", emitter.SizeStart, 0.01f, 0.0f, 50.0f);
            dragFloat("Tamanho final", emitter.SizeEnd, 0.01f, 0.0f, 50.0f);
            ImGui::ColorEdit4("Cor inicial", &emitter.ColorStart.x);
            TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            ImGui::ColorEdit4("Cor final", &emitter.ColorEnd.x);
            TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            checkbox("Aditivo (brilho)", emitter.Additive);

            // Textura opcional: arraste uma imagem do Conteudo do Projeto.
            std::string label = emitter.TexturePath.empty() ? "Textura: arraste uma imagem (opcional)" : "Textura: " + emitter.TexturePath;
            ImGui::Button(label.c_str(), ImVec2(-1, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_IMAGE_PATH")) {
                    auto before = emitter;
                    std::filesystem::path dropped((const char*)payload->Data);
                    if (auto project = Prism::Project::GetActive()) {
                        std::error_code ec;
                        auto relative = std::filesystem::relative(dropped, project->GetProjectDirectory(), ec);
                        emitter.TexturePath = !ec ? relative.generic_string() : dropped.generic_string();
                    }
                    else {
                        emitter.TexturePath = dropped.generic_string();
                    }
                    CommitComponentEdit(m_Ctx, before, emitter, title);
                }
                ImGui::EndDragDropTarget();
            }
            if (!emitter.TexturePath.empty()) {
                if (ImGui::SmallButton("Limpar textura##Particle")) {
                    auto before = emitter;
                    emitter.TexturePath.clear();
                    CommitComponentEdit(m_Ctx, before, emitter, title);
                }
            }

            ImGui::TextDisabled("Fora do Play, a entidade selecionada mostra um preview.");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ParticleEmitterComponent>>(m_Ctx.SelectedEntity, "Particle Emitter"));
    }

}
