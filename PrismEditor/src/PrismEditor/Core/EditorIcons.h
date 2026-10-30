#pragma once

// ============================================================================
// EditorIcons.h
// Icones desenhados por codigo (ImDrawList): sem arquivos de imagem, escalam
// com o tamanho pedido e usam as cores do EditorTheme.
// ============================================================================

#include <imgui.h>

namespace PrismEditor {

    enum class FileIcon { Folder, File, Map, Prefab, Material, Model, Audio, Image, Script, Project };
    enum class ToolIcon { Save, Undo, Redo, Refresh, Back, NewFolder, Play, Stop };

    // Desenha o icone de arquivo/pasta dentro do retangulo [min, max].
    void DrawFileIcon(ImDrawList* dl, FileIcon icon, ImVec2 min, ImVec2 max, ImU32 color);

    // Desenha um icone de barra de ferramentas dentro de [min, max].
    void DrawToolIcon(ImDrawList* dl, ToolIcon icon, ImVec2 min, ImVec2 max, ImU32 color);

    // Botao quadrado so com icone, com tooltip. 'tint' 0 = cor de texto do tema.
    bool IconButton(const char* id, ToolIcon icon, const char* tooltip, ImU32 tint = 0, float size = 28.0f);

}
