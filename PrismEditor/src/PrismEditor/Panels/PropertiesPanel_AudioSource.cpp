#include "PropertiesPanel.h"
#include <imgui.h>
#include <Prism/Audio/AudioEngine.h>
#include "../Commands/EditorCommands.h"
#include "ComponentEditUtils.h"
#include <algorithm>
#include <filesystem>
#include <string>

// PropertiesPanel_AudioSource.cpp
// UI do AudioSourceComponent (clip + volume/pitch/loop/3D) e do
// AudioListenerComponent. O som so toca durante o Play; o botao "Ouvir"
// toca o clip no editor, so para conferir o arquivo.

namespace PrismEditor {

    // Path absoluto (vindo do Content Browser) -> AssetID, com um Refresh() de
    // segunda chance para arquivos copiados por fora do editor.
    static Prism::AssetID ResolveAudioAssetID(const std::filesystem::path& absolutePath) {
        auto project = Prism::Project::GetActive();
        if (!project)
            return {};
        auto& registry = project->GetAssetRegistry();
        std::string relative = registry.ToRelative(absolutePath);
        if (relative.empty()) {
            PRISM_WARN("O audio precisa estar dentro da pasta de Assets do projeto: ", absolutePath.string());
            return {};
        }
        Prism::AssetID id = registry.IdForPath(relative);
        if (!id.IsValid()) {
            registry.Refresh();
            id = registry.IdForPath(relative);
        }
        return id;
    }

    void PropertiesPanel::DrawAudioSourceUI() {
        auto& source = m_Ctx.SelectedEntity.GetComponent<Prism::AudioSourceComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Audio Source", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const Prism::AudioSourceComponent before = source; // ver ComponentEditUtils.h

            auto project = Prism::Project::GetActive();
            std::filesystem::path clipPath = (project && source.Clip.IsValid()) ? project->GetAssetRegistry().AbsolutePath(source.Clip) : std::filesystem::path{};
            bool brokenLink = source.Clip.IsValid() && clipPath.empty();

            std::string label = !source.Clip.IsValid() ? "Clip: arraste um audio aqui"
                : brokenLink ? "Clip: [vinculo quebrado]"
                : "Clip: " + clipPath.filename().string();
            ImGui::Button(label.c_str(), ImVec2(-1, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_AUDIO_PATH")) {
                    std::filesystem::path dropped(std::string((const char*)payload->Data, payload->DataSize - 1));
                    Prism::AssetID id = ResolveAudioAssetID(dropped);
                    if (id.IsValid()) {
                        source.Clip = id;
                        CommitComponentEdit(m_Ctx, before, source, "Audio Source");
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Arraste um .wav, .mp3 ou .flac do painel Conteudo do Projeto.");

            if (source.Clip.IsValid()) {
                bool previewing = Prism::AudioEngine::IsPreviewPlaying();
                ImGui::BeginDisabled(clipPath.empty());
                if (ImGui::SmallButton(previewing ? "Parar##AudioPreview" : "Ouvir##AudioPreview")) {
                    if (previewing)
                        Prism::AudioEngine::StopPreview();
                    else
                        Prism::AudioEngine::PlayPreview(clipPath);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("Limpar##AudioClip")) {
                    Prism::AudioEngine::StopPreview();
                    source.Clip = {};
                    CommitComponentEdit(m_Ctx, before, source, "Audio Source");
                }
            }

            if (ImGui::SliderFloat("Volume", &source.Volume, 0.0f, 2.0f))
                source.Volume = std::clamp(source.Volume, 0.0f, 2.0f);
            TrackContinuousEdit(m_Ctx, m_AudioSourceBeforeEdit, source, "Audio Source");

            if (ImGui::SliderFloat("Pitch", &source.Pitch, 0.1f, 4.0f))
                source.Pitch = std::clamp(source.Pitch, 0.1f, 4.0f);
            TrackContinuousEdit(m_Ctx, m_AudioSourceBeforeEdit, source, "Audio Source");

            if (ImGui::Checkbox("Repetir (Loop)", &source.Loop))
                CommitComponentEdit(m_Ctx, before, source, "Audio Source");
            if (ImGui::Checkbox("Tocar ao iniciar", &source.PlayOnStart))
                CommitComponentEdit(m_Ctx, before, source, "Audio Source");
            if (ImGui::Checkbox("Som 3D (espacial)", &source.Spatial))
                CommitComponentEdit(m_Ctx, before, source, "Audio Source");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Marcado: o volume depende da distancia ate o Audio Listener (ou a camera primaria).\nDesmarcado: som 2D, sempre com o mesmo volume (musica, interface).");

            if (source.Spatial) {
                ImGui::DragFloat("Distancia minima", &source.MinDistance, 0.1f, 0.01f, 10000.0f);
                TrackContinuousEdit(m_Ctx, m_AudioSourceBeforeEdit, source, "Audio Source");
                ImGui::DragFloat("Distancia maxima", &source.MaxDistance, 0.1f, 0.02f, 10000.0f);
                TrackContinuousEdit(m_Ctx, m_AudioSourceBeforeEdit, source, "Audio Source");
                if (source.MaxDistance <= source.MinDistance)
                    source.MaxDistance = source.MinDistance + 0.01f;
            }

            if (!Prism::AudioEngine::IsInitialized())
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "Nenhum dispositivo de audio disponivel.");
            else if (!m_Ctx.ActiveScene->IsRunning())
                ImGui::TextDisabled("Aperte Play para ouvir no jogo.");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::AudioSourceComponent>>(m_Ctx.SelectedEntity, "Audio Source"));
    }

    void PropertiesPanel::DrawAudioListenerUI() {
        auto& listener = m_Ctx.SelectedEntity.GetComponent<Prism::AudioListenerComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Audio Listener", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const Prism::AudioListenerComponent before = listener;
            if (ImGui::Checkbox("Ativo", &listener.Active))
                CommitComponentEdit(m_Ctx, before, listener, "Audio Listener");
            ImGui::TextDisabled("Os \"ouvidos\" do som 3D (posicao e direcao desta entidade).\nSem nenhum Audio Listener, a camera primaria e usada.");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::AudioListenerComponent>>(m_Ctx.SelectedEntity, "Audio Listener"));
    }

}
