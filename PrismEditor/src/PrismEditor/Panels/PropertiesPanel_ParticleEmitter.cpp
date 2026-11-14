#include "PropertiesPanel.h"
#include "ComponentEditUtils.h"
#include "ParticleCurveWidgets.h"
#include "../Commands/EditorCommands.h"
#include <imgui.h>
#include <Prism/Particles/ParticleSystem.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <type_traits>

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

            // Combo de enum: copia antes + CommitComponentEdit (widget discreto).
            auto combo = [&](const char* label, auto& value, const char* const* names, int count) {
                using Enum = std::decay_t<decltype(value)>;
                auto before = emitter;
                int current = (int)value;
                if (ImGui::Combo(label, &current, names, count)) {
                    value = (Enum)current;
                    CommitComponentEdit(m_Ctx, before, emitter, title);
                }
            };
            auto dragInt = [&](const char* label, uint32_t& value, int low, int high) {
                int v = (int)value;
                if (ImGui::DragInt(label, &v, 0.2f, low, high))
                    value = (uint32_t)std::min(std::max(v, low), high);
                TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            };
            // Curva/gradiente: UM comando de Undo por gesto (ver ParticleCurveWidgets.h).
            // 'before' = copia do emissor tirada ANTES de chamar o widget.
            auto gesture = [&](const Prism::ParticleEmitterComponent& before, const ParticleWidgetResult& result) {
                if (result.Started)
                    m_ParticleBeforeEdit = before;
                if (result.Finished)
                    CommitComponentEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            };
            auto resetButton = [&](const char* id, auto& target, const auto& neutral) {
                ImGui::SameLine();
                if (ImGui::SmallButton(id)) {
                    auto before = emitter;
                    target = neutral;
                    CommitComponentEdit(m_Ctx, before, emitter, title);
                }
            };

            static const char* const kShapeNames[] = { "Cone", "Esfera", "Hemisferio", "Caixa", "Circulo / anel", "Linha" };
            static const char* const kSpaceNames[] = { "Mundo", "Local (acompanha a entidade)" };
            static const char* const kBlendNames[] = { "Alpha (fumaca, poeira)", "Aditivo (fogo, brilho)", "Aditivo suave (neblina luminosa)", "Multiplicar (escurece)" };
            static const char* const kFlipbookNames[] = { "Ao longo da vida", "FPS fixo", "Quadro aleatorio" };
            using Shape = Prism::ParticleShape;

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

            ImGui::Separator(); ImGui::TextDisabled("Forma de emissao");
            combo("Forma", emitter.Shape, kShapeNames, 6);
            switch (emitter.Shape) {
            case Shape::Sphere:
            case Shape::Hemisphere:
                dragFloat("Raio", emitter.ShapeRadius, 0.01f, 0.0f, 1000.0f);
                dragFloat("Espessura (0 = casca, 1 = cheia)", emitter.ShapeThickness, 0.01f, 0.0f, 1.0f);
                ImGui::TextDisabled("As particulas saem para fora do centro.");
                break;
            case Shape::Box:
                ImGui::DragFloat3("Tamanho", &emitter.ShapeBox.x, 0.01f, 0.0f, 1000.0f);
                TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
                dragFloat("Abertura do cone (graus)", emitter.SpreadAngle, 0.5f, 0.0f, 180.0f);
                dragFloat("Direcao para fora (0-1)", emitter.ShapeRadialBlend, 0.01f, 0.0f, 1.0f);
                break;
            case Shape::Circle:
                dragFloat("Raio", emitter.ShapeRadius, 0.01f, 0.0f, 1000.0f);
                dragFloat("Espessura (0 = aro, 1 = disco)", emitter.ShapeThickness, 0.01f, 0.0f, 1.0f);
                dragFloat("Abertura do cone (graus)", emitter.SpreadAngle, 0.5f, 0.0f, 180.0f);
                dragFloat("Direcao para fora (0-1)", emitter.ShapeRadialBlend, 0.01f, 0.0f, 1.0f);
                break;
            case Shape::Edge:
                dragFloat("Meio-comprimento", emitter.ShapeRadius, 0.01f, 0.0f, 1000.0f);
                dragFloat("Abertura do cone (graus)", emitter.SpreadAngle, 0.5f, 0.0f, 180.0f);
                break;
            case Shape::Cone:
            default:
                dragFloat("Abertura do cone (graus)", emitter.SpreadAngle, 0.5f, 0.0f, 180.0f);
                break;
            }

            ImGui::Separator(); ImGui::TextDisabled("Movimento");
            combo("Espaco de simulacao", emitter.SimulationSpace, kSpaceNames, 2);
            dragFloat("Vida minima (s)", emitter.LifetimeMin, 0.02f, 0.01f, 60.0f);
            dragFloat("Vida maxima (s)", emitter.LifetimeMax, 0.02f, 0.01f, 60.0f);
            dragFloat("Velocidade minima", emitter.SpeedMin, 0.05f, 0.0f, 200.0f);
            dragFloat("Velocidade maxima", emitter.SpeedMax, 0.05f, 0.0f, 200.0f);
            dragFloat("Gravidade (x -9.81)", emitter.GravityScale, 0.02f, -5.0f, 5.0f);
            dragFloat("Atrito", emitter.Drag, 0.02f, 0.0f, 100.0f);
            dragFloat("Rotacao inicial min (graus)", emitter.RotationMin, 0.5f, -3600.0f, 3600.0f);
            dragFloat("Rotacao inicial max (graus)", emitter.RotationMax, 0.5f, -3600.0f, 3600.0f);
            dragFloat("Vel. angular min (graus/s)", emitter.AngularVelocityMin, 0.5f, -3600.0f, 3600.0f);
            dragFloat("Vel. angular max (graus/s)", emitter.AngularVelocityMax, 0.5f, -3600.0f, 3600.0f);

            ImGui::TextUnformatted("Velocidade ao longo da vida");
            resetButton("Resetar##SpeedCurve", emitter.SpeedOverLife, Prism::ParticleCurve{});
            {
                auto before = emitter;
                gesture(before, ParticleCurveEditor("##SpeedCurve", emitter.SpeedOverLife, 0.0f, 2.0f));
            }

            ImGui::Separator(); ImGui::TextDisabled("Aparencia");
            dragFloat("Tamanho inicial", emitter.SizeStart, 0.01f, 0.0f, 50.0f);
            dragFloat("Tamanho final", emitter.SizeEnd, 0.01f, 0.0f, 50.0f);
            dragFloat("Variacao de tamanho (0-1)", emitter.SizeVariation, 0.01f, 0.0f, 1.0f);
            ImGui::TextUnformatted("Tamanho ao longo da vida (multiplica)");
            resetButton("Resetar##SizeCurve", emitter.SizeOverLife, Prism::ParticleCurve{});
            {
                auto before = emitter;
                gesture(before, ParticleCurveEditor("##SizeCurve", emitter.SizeOverLife, 0.0f, 2.0f));
            }

            ImGui::ColorEdit4("Cor inicial", &emitter.ColorStart.x);
            TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            ImGui::ColorEdit4("Cor final", &emitter.ColorEnd.x);
            TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            ImGui::TextUnformatted("Cor ao longo da vida (multiplica)");
            resetButton("Resetar##ColorGradient", emitter.ColorOverLife, Prism::ParticleGradient{});
            {
                auto before = emitter;
                gesture(before, ParticleGradientEditor("##ColorGradient", emitter.ColorOverLife));
            }
            {
                const int selectedStop = ParticleGradientSelection("##ColorGradient", emitter.ColorOverLife);
                ImGui::ColorEdit4("Cor da parada", emitter.ColorOverLife.C[selectedStop]);
                TrackContinuousEdit(m_Ctx, m_ParticleBeforeEdit, emitter, title);
            }
            combo("Mistura", emitter.BlendMode, kBlendNames, 4);

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

            if (!emitter.TexturePath.empty()) {
                ImGui::TextDisabled("Folha de sprites (flipbook): colunas x linhas. 1x1 = textura simples.");
                dragInt("Colunas", emitter.FlipbookColumns, 1, 64);
                dragInt("Linhas", emitter.FlipbookRows, 1, 64);
                if (emitter.FlipbookColumns * emitter.FlipbookRows > 1) {
                    combo("Animacao", emitter.FlipbookMode, kFlipbookNames, 3);
                    if (emitter.FlipbookMode == Prism::ParticleFlipbookMode::FixedFPS)
                        dragFloat("Quadros por segundo", emitter.FlipbookFPS, 0.1f, 0.0f, 240.0f);
                }
            }

            ImGui::TextDisabled("Fora do Play, a entidade selecionada mostra um preview.");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ParticleEmitterComponent>>(m_Ctx.SelectedEntity, "Particle Emitter"));
    }

}
