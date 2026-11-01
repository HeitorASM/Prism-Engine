#include "ExtensionsPanel.h"
#include "../Core/EditorTheme.h"
#include "../Core/ShellOps.h"

#include <Prism.h>
#include <Prism/Core/Paths.h>
#include <Prism/Extensions/ExtensionLoader.h>
#include <Prism/Extensions/ExtensionManifest.h>
#include <Prism/Extensions/ExtensionTemplate.h>

#include <imgui.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace PrismEditor {

    namespace fs = std::filesystem;

    namespace {
        bool ReadTextFile(const fs::path& path, std::string& out) {
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;
            std::ostringstream ss;
            ss << file.rdbuf();
            out = ss.str();
            return true;
        }

        bool SamePath(const fs::path& a, const fs::path& b) {
            std::error_code ec;
            return fs::equivalent(a, b, ec);
        }

        const char* LibraryExtension() {
#if defined(_WIN32)
            return ".dll";
#elif defined(__APPLE__)
            return ".dylib";
#else
            return ".so";
#endif
        }

        bool HasLibrary(const fs::path& directory) {
            std::error_code ec;
            for (const fs::path& base : { directory, directory / "Binaries" }) {
                for (const auto& file : fs::directory_iterator(base, ec)) {
                    std::error_code fileError;
                    if (file.is_regular_file(fileError) && file.path().extension() == LibraryExtension())
                        return true;
                }
            }
            return false;
        }
    }

    void ExtensionsPanel::NotifyCompiled(const fs::path& extensionDirectory, bool success) {
        m_LastBuildOk = success;
        m_LastBuildMessage = extensionDirectory.filename().string() + (success ? ": compilada." : ": falhou (veja a aba Saida de Build).");
        if (success)
            m_RestartNeeded = true;
        m_NeedsRescan = true;
    }

    // Junta o que o loader ja conhece (estado/motivo) com o que existe em disco (inclusive extensoes
    // novas, criadas ou copiadas depois que o editor abriu - ainda "nao carregadas").
    void ExtensionsPanel::Rescan() {
        m_NeedsRescan = false;
        m_Entries.clear();

        std::vector<std::pair<fs::path, bool>> roots;
        roots.push_back({ Prism::GetExecutableDirectory() / "Extensions", false });
        if (auto project = Prism::Project::GetActive())
            roots.push_back({ project->GetProjectDirectory() / "Extensions", true });

        for (const auto& [root, fromProject] : roots) {
            std::error_code ec;
            if (!fs::is_directory(root, ec))
                continue;
            std::vector<fs::path> directories;
            for (const auto& item : fs::directory_iterator(root, ec)) {
                std::error_code itemError;
                if (item.is_directory(itemError))
                    directories.push_back(item.path());
            }
            std::sort(directories.begin(), directories.end());

            for (const fs::path& directory : directories) {
                std::error_code fileError;
                if (!fs::is_regular_file(directory / "extension.ini", fileError))
                    continue;
                Entry entry;
                entry.Directory = directory;
                entry.FromProject = fromProject;
                entry.Name = directory.filename().string();
                entry.HasCMake = fs::is_regular_file(directory / "CMakeLists.txt", fileError);
                entry.HasBinary = HasLibrary(directory);

                std::string text;
                if (ReadTextFile(directory / "extension.ini", text)) {
                    Prism::ManifestParseResult parsed = Prism::ParseExtensionManifest(text);
                    entry.ManifestValid = parsed.Ok;
                    if (parsed.Ok) {
                        entry.Name = parsed.Manifest.Name;
                        entry.Version = parsed.Manifest.Version;
                        entry.Author = parsed.Manifest.Author;
                        entry.Description = parsed.Manifest.Description;
                        entry.Target = Prism::ExtensionTargetName(parsed.Manifest.Target);
                        entry.Enabled = parsed.Manifest.Enabled;
                    }
                    else {
                        entry.Message = "extension.ini invalido: " + parsed.Error;
                        entry.State = Status::Failed;
                    }
                }
                else {
                    entry.Message = "nao foi possivel ler o extension.ini.";
                    entry.State = Status::Failed;
                }

                // Estado dado pelo loader (se ele ja viu esta pasta).
                if (entry.ManifestValid) {
                    entry.State = Status::NotLoaded;
                    for (const Prism::ExtensionInfo& info : Prism::ExtensionLoader::GetExtensions()) {
                        if (!SamePath(info.Directory, directory))
                            continue;
                        entry.Message = info.Message;
                        entry.State = info.State == Prism::ExtensionState::Loaded ? Status::Loaded
                            : info.State == Prism::ExtensionState::Failed ? Status::Failed : Status::Skipped;
                        break;
                    }
                    if (entry.State == Status::NotLoaded)
                        entry.Message = "Ainda nao carregada: reinicie o editor.";
                }
                m_Entries.push_back(std::move(entry));
            }
        }
    }

    bool ExtensionsPanel::SetEnabled(const Entry& entry, bool enabled) {
        const fs::path manifest = entry.Directory / "extension.ini";
        std::string text;
        if (!ReadTextFile(manifest, text))
            return false;
        const std::string updated = Prism::SetManifestValue(text, "enabled", enabled ? "true" : "false");
        std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << updated;
        return out.good();
    }

    void ExtensionsPanel::RenderEntry(Entry& entry, int index) {
        const EditorTheme& theme = EditorTheme::Get();
        ImGui::PushID(index);
        ImGui::BeginGroup();

        ImVec4 color = theme.TextDim;
        const char* label = "";
        switch (entry.State) {
            case Status::Loaded:    color = theme.Play;    label = "Carregada"; break;
            case Status::Failed:    color = theme.Stop;    label = "Falhou"; break;
            case Status::Skipped:   color = theme.TextDim; label = "Ignorada"; break;
            case Status::NotLoaded: color = theme.Warning; label = "Nao carregada"; break;
        }
        ImGui::TextUnformatted(entry.Name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(color, "[%s]", label);
        ImGui::SameLine();
        ImGui::TextColored(theme.TextDim, "%s%s%s  %s", entry.Version.empty() ? "" : "v", entry.Version.c_str(),
            entry.Version.empty() ? "" : "", entry.FromProject ? "(projeto)" : "(global)");

        if (!entry.Description.empty())
            ImGui::TextColored(theme.TextDim, "%s", entry.Description.c_str());
        if (!entry.Author.empty())
            ImGui::TextColored(theme.TextDim, "Autor: %s  |  alvo: %s", entry.Author.c_str(), entry.Target.c_str());
        if (!entry.Message.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", entry.Message.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::BeginDisabled(!entry.ManifestValid);
        bool enabled = entry.Enabled;
        if (ImGui::Checkbox("Ativada", &enabled)) {
            if (SetEnabled(entry, enabled)) {
                entry.Enabled = enabled;
                m_RestartNeeded = true;
            }
            else {
                PRISM_CORE_ERROR("Nao foi possivel gravar ", (entry.Directory / "extension.ini").string());
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Grava 'enabled' no extension.ini. Vale depois de reiniciar o editor.");

        ImGui::SameLine();
        ImGui::BeginDisabled(!entry.HasCMake || m_Busy || !m_OnCompile);
        if (ImGui::Button("Compilar") && m_OnCompile)
            m_OnCompile(entry.Directory);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(entry.HasCMake ? "Compila a extensao (saida na aba Saida de Build). Reinicie o editor para carregar."
                                              : "Esta extensao nao tem CMakeLists.txt (so a biblioteca pronta).");

        ImGui::SameLine();
        if (ImGui::Button("Mostrar na pasta"))
            ShellOps::RevealInFileExplorer(entry.Directory);

        const fs::path source = entry.Directory / (entry.Name + ".cpp");
        std::error_code ec;
        if (m_OnOpenFile && fs::is_regular_file(source, ec)) {
            ImGui::SameLine();
            if (ImGui::Button("Abrir codigo"))
                m_OnOpenFile(source);
        }
        if (!entry.HasBinary && entry.ManifestValid) {
            ImGui::SameLine();
            ImGui::TextColored(theme.Warning, "sem biblioteca compilada");
        }

        ImGui::EndGroup();
        ImGui::Separator();
        ImGui::PopID();
    }

    void ExtensionsPanel::RenderCreatePopup() {
        if (m_OpenCreatePopup) {
            ImGui::OpenPopup("Nova extensao##Ext");
            m_OpenCreatePopup = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Nova extensao##Ext", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextWrapped("Cria <Projeto>/Extensions/<Nome>/ com extension.ini, CMakeLists.txt e um .cpp de exemplo (component + sistema).");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(320.0f);
        const bool enter = ImGui::InputText("##extname", m_NewName, sizeof(m_NewName), ImGuiInputTextFlags_EnterReturnsTrue);
        if (!m_PopupError.empty())
            ImGui::TextColored(EditorTheme::Get().Stop, "%s", m_PopupError.c_str());

        const bool confirm = ImGui::Button("Criar", ImVec2(120, 0)) || enter;
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancelar", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

        if (confirm) {
            auto project = Prism::Project::GetActive();
            if (!project) {
                m_PopupError = "Nenhum projeto aberto.";
            }
            else {
                fs::path directory;
                std::string error;
                if (Prism::WriteExtensionTemplate(project->GetProjectDirectory() / "Extensions", m_NewName, directory, error)) {
                    m_RestartNeeded = true;
                    m_NeedsRescan = true;
                    if (m_OnOpenFile)
                        m_OnOpenFile(directory / (std::string(m_NewName) + ".cpp"));
                    ImGui::CloseCurrentPopup();
                }
                else {
                    m_PopupError = error;
                }
            }
        }
        else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ExtensionsPanel::OnImGuiRender() {
        if (!m_Open)
            return;
        if (m_NeedsRescan)
            Rescan();

        if (ImGui::Begin("Extensoes", &m_Open)) {
            const EditorTheme& theme = EditorTheme::Get();
            const bool hasProject = Prism::Project::GetActive() != nullptr;

            ImGui::BeginDisabled(!hasProject);
            if (ImGui::Button("Nova extensao...")) {
                m_NewName[0] = '\0';
                m_PopupError.clear();
                m_OpenCreatePopup = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Atualizar"))
                m_NeedsRescan = true;
            ImGui::SameLine();
            if (ImGui::Button("Pasta de extensoes do projeto") && hasProject) {
                const fs::path folder = Prism::Project::GetActive()->GetProjectDirectory() / "Extensions";
                std::error_code ec;
                fs::create_directories(folder, ec);
                ShellOps::RevealInFileExplorer(folder);
            }

            if (m_RestartNeeded)
                ImGui::TextColored(theme.Warning, "Reinicie o editor para aplicar as mudancas nas extensoes.");
            if (!m_LastBuildMessage.empty())
                ImGui::TextColored(m_LastBuildOk ? theme.Play : theme.Stop, "%s", m_LastBuildMessage.c_str());
            ImGui::TextColored(theme.TextDim, "Extensoes sao codigo nativo com acesso total: so carregue as de quem voce confia.");
            ImGui::Separator();

            if (m_Entries.empty())
                ImGui::TextDisabled("Nenhuma extensao encontrada (pastas com extension.ini em Extensions/).");
            for (int i = 0; i < (int)m_Entries.size(); i++)
                RenderEntry(m_Entries[(size_t)i], i);
        }
        ImGui::End();
        RenderCreatePopup();
    }

}
