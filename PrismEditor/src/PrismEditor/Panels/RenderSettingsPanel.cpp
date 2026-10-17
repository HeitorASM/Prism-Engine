#include "RenderSettingsPanel.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace PrismEditor {

    void RenderSettingsPanel::OnImGuiRenderMenu() {
        // Sem projeto ativo nao ha onde guardar os valores: nao mostra o menu.
        auto project = Prism::Project::GetActive();
        if (!project)
            return;

        if (!ImGui::BeginMenu("Renderizacao"))
            return;

        Prism::RenderSettings& settings = project->GetRenderSettings();
        bool changed = false;

        // Largura fixa: sem ela, os widgets de um menu (janela que se
        // ajusta ao conteudo) saem com largura minima/instavel.
        ImGui::PushItemWidth(240.0f);

        ImGui::SeparatorText("Camera");
        changed |= ImGui::SliderFloat("Exposicao", &settings.Exposure, 0.1f, 8.0f, "%.2f",
            ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetItemTooltip("Brilho final da imagem, aplicado antes do tone mapping.\n1.0 = neutro; maior clareia, menor escurece.");

        ImGui::SeparatorText("Ambiente");
        changed |= ImGui::SliderFloat("Intensidade", &settings.Ambient, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetItemTooltip("Brilho medio da luz ambiente.\n0 = so as luzes iluminam (areas fora do alcance ficam escuras).");
        changed |= ImGui::ColorEdit3("Ceu", settings.EnvZenith);
        changed |= ImGui::ColorEdit3("Horizonte", settings.EnvHorizon);
        changed |= ImGui::ColorEdit3("Chao", settings.EnvGround);

        ImGui::SeparatorText("Fundo (Skybox)");
        const char* skyModes[] = { "Cor chapada", "Gradiente do ambiente", "Panorama (imagem)" };
        int skyMode = (int)settings.Sky;
        if (ImGui::Combo("Tipo##Sky", &skyMode, skyModes, 3)) {
            settings.Sky = (Prism::SkyMode)skyMode;
            changed = true;
        }
        if (settings.Sky == Prism::SkyMode::Solid) {
            changed |= ImGui::ColorEdit3("Cor do fundo", settings.SkyColor);
        }
        else if (settings.Sky == Prism::SkyMode::Gradient) {
            ImGui::TextDisabled("Usa as cores Ceu / Horizonte / Chao acima.");
        }
        else {
            std::string label = settings.SkyTexture.empty() ? "(nenhuma)" : settings.SkyTexture;
            if (ImGui::BeginCombo("Imagem##Sky", label.c_str())) {
                if (ImGui::Selectable("(nenhuma)", settings.SkyTexture.empty())) {
                    settings.SkyTexture.clear();
                    changed = true;
                }
                // Lista as imagens do projeto so enquanto o combo esta aberto.
                std::error_code ec;
                std::vector<std::string> images;
                for (auto it = std::filesystem::recursive_directory_iterator(project->GetAssetDirectory(), ec);
                     !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                    if (!it->is_regular_file(ec))
                        continue;
                    std::string ext = it->path().extension().string();
                    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
                    if (ext != ".hdr" && ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".tga" && ext != ".bmp")
                        continue;
                    std::error_code relativeError;
                    auto relative = std::filesystem::relative(it->path(), project->GetProjectDirectory(), relativeError);
                    if (!relativeError)
                        images.push_back(relative.generic_string());
                }
                for (const std::string& image : images) {
                    bool selected = image == settings.SkyTexture;
                    if (ImGui::Selectable(image.c_str(), selected)) {
                        settings.SkyTexture = image;
                        changed = true;
                    }
                }
                if (images.empty())
                    ImGui::TextDisabled("Nenhuma imagem em Assets/.");
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Panorama equirretangular (2:1). .hdr mantem a faixa dinamica.\nO fundo nao ilumina a cena: a luz de ambiente vem das cores acima.");
            changed |= ImGui::SliderFloat("Rotacao##Sky", &settings.SkyRotation, -180.0f, 180.0f, "%.0f graus", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Brilho##Sky", &settings.SkyIntensity, 0.0f, 8.0f, "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
        }

        ImGui::Spacing();
        if (ImGui::Button("Restaurar padrao")) {
            settings = Prism::RenderSettings{};
            changed = true;
        }
        ImGui::TextDisabled("Salvo no projeto (.prismproj)");

        ImGui::PopItemWidth();

        if (changed) {
            // Aplica na hora (a viewport atualiza ao vivo); a gravacao no
            // arquivo fica para FlushRenderSettingsSave.
            Prism::Renderer::ApplyRenderSettings(settings);
            m_NeedSave = true;
            m_LastEdit = ImGui::GetTime();
        }

        ImGui::EndMenu();
    }

    void RenderSettingsPanel::FlushSave() {
        if (!m_NeedSave)
            return;

        // Arrastando slider ou cor: o mouse esta apertado. Espera soltar.
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            return;

        // Ainda editando (teclado com repeticao, varios ajustes seguidos).
        if (ImGui::GetTime() - m_LastEdit < kSaveDelay)
            return;

        m_NeedSave = false;
        if (!Prism::Project::SaveActive())
            PRISM_ERROR("Nao foi possivel gravar os ajustes de renderizacao no projeto.");
    }

    void RenderSettingsPanel::FlushOnShutdown() {
        // Ajuste de renderizacao editado poucos instantes antes de fechar
        // (a espera de FlushRenderSettingsSave ainda nao acabou): grava agora
        // para nao perder. Project::SaveActive so usa o Project estatico -
        // nada de Application::Get() aqui (ver comentario acima). Sem log:
        // durante a destruicao do Application o logging nao e garantido.
        if (m_NeedSave) {
            m_NeedSave = false;
            Prism::Project::SaveActive();
        }
    }

}
