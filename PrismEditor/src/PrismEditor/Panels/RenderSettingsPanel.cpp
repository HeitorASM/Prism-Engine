#include "RenderSettingsPanel.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

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
