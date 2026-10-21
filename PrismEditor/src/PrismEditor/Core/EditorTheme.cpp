#include "EditorTheme.h"

#include <Prism/Core/Paths.h>

#include <filesystem>
#include <vector>

namespace PrismEditor {

    namespace {
        bool s_Initialized = false;

        // Mesma fonte da UI in-game (Prism/fonts/DejaVuSans.ttf): codigo-fonte
        // (quando definido pelo CMake), ao lado do executavel, diretorio de trabalho.
        std::filesystem::path FindEditorFont() {
            const char* kName = "DejaVuSans.ttf";
            std::vector<std::filesystem::path> candidates;
#ifdef PRISM_EDITOR_FONT_SOURCE_DIR
            candidates.emplace_back(std::filesystem::path(PRISM_EDITOR_FONT_SOURCE_DIR) / kName);
#endif
            std::filesystem::path exeDir = Prism::GetExecutableDirectory();
            if (!exeDir.empty())
                candidates.push_back(exeDir / "fonts" / kName);
            std::error_code cwdError;
            std::filesystem::path cwd = std::filesystem::current_path(cwdError);
            if (!cwdError)
                candidates.push_back(cwd / "fonts" / kName);

            for (const auto& path : candidates) {
                std::error_code ec;
                if (std::filesystem::is_regular_file(path, ec))
                    return path;
            }
            return {};
        }
    }

    EditorTheme& EditorTheme::Get() {
        static EditorTheme theme;
        return theme;
    }

    void EditorTheme::Init() {
        if (s_Initialized || !ImGui::GetCurrentContext())
            return;
        s_Initialized = true;

        EditorTheme& theme = Get();

        // Fonte: se nao achar o .ttf, fica a fonte padrao do ImGui (o editor
        // continua funcionando, so sem a tipografia nova).
        std::filesystem::path fontPath = FindEditorFont();
        if (!fontPath.empty()) {
            ImGuiIO& io = ImGui::GetIO();
            // A faixa padrao do ImGui cobre Latin-1 (acentos do portugues).
            ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), theme.FontSize);
            if (font)
                io.FontDefault = font;
        }

        theme.Apply();
    }

    void EditorTheme::Apply() const {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* c = style.Colors;

        c[ImGuiCol_Text]                 = Text;
        c[ImGuiCol_TextDisabled]         = TextDim;
        c[ImGuiCol_WindowBg]             = Bg;
        c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_PopupBg]              = ImVec4(BgDeep.x, BgDeep.y, BgDeep.z, 0.98f);
        c[ImGuiCol_Border]               = Border;
        c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

        c[ImGuiCol_FrameBg]              = Surface;
        c[ImGuiCol_FrameBgHovered]       = SurfaceHot;
        c[ImGuiCol_FrameBgActive]        = SurfaceDown;

        c[ImGuiCol_TitleBg]              = BgDeep;
        c[ImGuiCol_TitleBgActive]        = BgDeep;
        c[ImGuiCol_TitleBgCollapsed]     = BgDeep;
        c[ImGuiCol_MenuBarBg]            = BgDeep;

        c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0.15f);
        c[ImGuiCol_ScrollbarGrab]        = Surface;
        c[ImGuiCol_ScrollbarGrabHovered] = SurfaceHot;
        c[ImGuiCol_ScrollbarGrabActive]  = Accent;

        c[ImGuiCol_CheckMark]            = Accent;
        c[ImGuiCol_SliderGrab]           = Accent;
        c[ImGuiCol_SliderGrabActive]     = AccentHot;

        c[ImGuiCol_Button]               = Surface;
        c[ImGuiCol_ButtonHovered]        = SurfaceHot;
        c[ImGuiCol_ButtonActive]         = SurfaceDown;

        c[ImGuiCol_Header]               = ImVec4(Accent.x, Accent.y, Accent.z, 0.30f);
        c[ImGuiCol_HeaderHovered]        = ImVec4(Accent.x, Accent.y, Accent.z, 0.45f);
        c[ImGuiCol_HeaderActive]         = ImVec4(Accent.x, Accent.y, Accent.z, 0.60f);

        c[ImGuiCol_Separator]            = Border;
        c[ImGuiCol_SeparatorHovered]     = AccentHot;
        c[ImGuiCol_SeparatorActive]      = Accent;
        c[ImGuiCol_ResizeGrip]           = ImVec4(Accent.x, Accent.y, Accent.z, 0.20f);
        c[ImGuiCol_ResizeGripHovered]    = ImVec4(Accent.x, Accent.y, Accent.z, 0.60f);
        c[ImGuiCol_ResizeGripActive]     = Accent;

        c[ImGuiCol_Tab]                  = BgDeep;
        c[ImGuiCol_TabHovered]           = SurfaceHot;
        c[ImGuiCol_TabSelected]          = Bg;
        c[ImGuiCol_TabSelectedOverline]  = Accent;
        c[ImGuiCol_TabDimmed]            = BgDeep;
        c[ImGuiCol_TabDimmedSelected]    = Bg;
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(Accent.x, Accent.y, Accent.z, 0.4f);

        c[ImGuiCol_DockingPreview]       = ImVec4(Accent.x, Accent.y, Accent.z, 0.45f);
        c[ImGuiCol_DockingEmptyBg]       = BgDeep;

        c[ImGuiCol_TextSelectedBg]       = ImVec4(Accent.x, Accent.y, Accent.z, 0.35f);
        c[ImGuiCol_DragDropTarget]       = AccentHot;
        c[ImGuiCol_TableHeaderBg]        = BgDeep;
        c[ImGuiCol_TableBorderStrong]    = Border;
        c[ImGuiCol_TableBorderLight]     = ImVec4(Border.x, Border.y, Border.z, 0.5f);

        style.WindowRounding    = 0.0f; // com viewports, janelas destacadas precisam de canto reto
        style.ChildRounding     = Rounding;
        style.FrameRounding     = Rounding;
        style.PopupRounding     = Rounding;
        style.ScrollbarRounding = Rounding;
        style.GrabRounding      = Rounding;
        style.TabRounding       = Rounding;

        style.WindowBorderSize  = 1.0f;
        style.FrameBorderSize   = 0.0f;
        style.PopupBorderSize   = 1.0f;
        style.WindowPadding     = ImVec2(8.0f, 8.0f);
        style.FramePadding      = ImVec2(8.0f, 4.0f);
        style.ItemSpacing       = ImVec2(8.0f, 5.0f);
        style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
        style.IndentSpacing     = 16.0f;
        style.ScrollbarSize     = 13.0f;
        style.GrabMinSize       = 10.0f;
        style.WindowMenuButtonPosition = ImGuiDir_None;
    }

}
