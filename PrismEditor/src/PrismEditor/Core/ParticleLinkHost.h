#pragma once

// ============================================================================
// ParticleLinkHost.h
// Cola do editor para o vinculo vivo emissor <-> recurso (.prismtres/.prismres):
// resolve AssetID -> caminho pelo AssetRegistry do projeto ativo, coleta os emissores da
// cena ativa e roda o ParticleLinkSync (Prism/Resources/ParticleLinkSync.h).
// ============================================================================

#include "EditorContext.h"
#include <imgui.h>

namespace PrismEditor::ParticleLinkHost {

    inline Prism::ParticleLinkSync::PathResolver Resolver() {
        return [](Prism::AssetID id) -> std::filesystem::path {
            auto project = Prism::Project::GetActive();
            return project ? project->GetAssetRegistry().AbsolutePath(id) : std::filesystem::path{};
        };
    }

    // Uma vez por frame (fora do Play: durante o Play scripts mudam o emissor e isso nao deve ir para o arquivo).
    inline void Tick(EditorContext& ctx) {
        if (!ctx.ActiveScene || (ctx.IsPlaying && ctx.IsPlaying()))
            return;
        std::vector<Prism::ParticleEmitterComponent*> emitters;
        auto view = ctx.ActiveScene->GetRegistry().view<Prism::ParticleEmitterComponent>();
        for (auto handle : view)
            emitters.push_back(&view.get<Prism::ParticleEmitterComponent>(handle));
        ctx.ParticleLinks.Update(emitters, Resolver(), ImGui::GetTime(), ImGui::IsMouseDown(ImGuiMouseButton_Left));
        const std::string error = ctx.ParticleLinks.TakeLastError();
        if (!error.empty())
            PRISM_ERROR("Recurso de particulas: ", error);
    }

    // Grava pendencias e esquece o estado: troca de cena / fechar o editor. Chamar ANTES de trocar ActiveScene.
    inline void FlushAndReset(EditorContext& ctx) {
        ctx.ParticleLinks.FlushAll(Resolver());
        ctx.ParticleLinks.Reset();
    }
}
