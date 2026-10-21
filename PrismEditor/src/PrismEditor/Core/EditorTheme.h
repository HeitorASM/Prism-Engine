#pragma once

// ============================================================================
// EditorTheme.h
// Identidade visual do editor num so lugar: paleta, arredondamentos,
// espacamentos e a fonte. Antes cada painel carregava cores literais; agora os
// paineis leem EditorTheme::Get() (ex: Accent, Play, Stop) e o ImGuiStyle e
// montado por Apply().
//
// Init() precisa rodar com o contexto do ImGui criado e ANTES do primeiro
// NewFrame (a fonte entra no atlas): e chamado em ProjectManagerLayer::OnAttach,
// o primeiro layer do editor, e e idempotente.
// ============================================================================

#include <imgui.h>

namespace PrismEditor {

    struct EditorTheme {
        // Superficies (do mais escuro ao mais claro)
        ImVec4 BgDeep      { 0.075f, 0.078f, 0.090f, 1.0f }; // barra de menu, abas inativas
        ImVec4 Bg          { 0.105f, 0.110f, 0.125f, 1.0f }; // fundo dos paineis
        ImVec4 Surface     { 0.150f, 0.158f, 0.178f, 1.0f }; // campos, botoes
        ImVec4 SurfaceHot  { 0.215f, 0.228f, 0.256f, 1.0f }; // hover
        ImVec4 SurfaceDown { 0.120f, 0.126f, 0.145f, 1.0f }; // pressionado
        ImVec4 Border      { 0.220f, 0.230f, 0.260f, 1.0f };

        // Texto
        ImVec4 Text        { 0.900f, 0.915f, 0.940f, 1.0f };
        ImVec4 TextDim     { 0.510f, 0.540f, 0.600f, 1.0f };

        // Destaque (selecao, aba ativa, foco)
        ImVec4 Accent      { 0.330f, 0.560f, 0.950f, 1.0f };
        ImVec4 AccentHot   { 0.420f, 0.640f, 1.000f, 1.0f };
        ImVec4 AccentDown  { 0.260f, 0.460f, 0.820f, 1.0f };

        // Semanticas
        ImVec4 Play        { 0.200f, 0.620f, 0.340f, 1.0f };
        ImVec4 PlayHot     { 0.260f, 0.720f, 0.410f, 1.0f };
        ImVec4 Stop        { 0.780f, 0.270f, 0.230f, 1.0f };
        ImVec4 StopHot     { 0.880f, 0.340f, 0.290f, 1.0f };
        ImVec4 Warning     { 0.950f, 0.720f, 0.250f, 1.0f };

        float Rounding     = 4.0f;
        float FontSize     = 16.0f;

        static EditorTheme& Get();

        // Carrega a fonte do editor e aplica o estilo. Idempotente.
        static void Init();

        // (Re)aplica as cores/medidas ao ImGuiStyle.
        void Apply() const;
    };

    // ImU32 com o alfa trocado - atalho para desenhar com a paleta.
    inline ImU32 ToU32(const ImVec4& c, float alpha = -1.0f) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, alpha < 0.0f ? c.w : alpha));
    }

}
