#include "Autosave.h"
#include "FileDialog.h"
#include <Prism/Core/AtomicFile.h>
#include <Prism/Core/FileBackup.h>
#include <imgui.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

namespace PrismEditor::Autosave {

    namespace fs = std::filesystem;

    namespace {

        float s_Accumulated = 0.0f;
        fs::path s_LastDirectory; // lembrado para Discard() ainda funcionar se o projeto ativo ja tiver sumido

        struct PendingRecovery {
            bool Active = false;
            bool PopupOpened = false;
            fs::path SceneFile;     // autosave.prismmap
            fs::path OriginalMap;   // mapa a que pertence ("" = cena que nunca foi salva)
            std::string SceneName;
            long long SavedAt = 0;  // segundos desde a epoca
        };
        PendingRecovery s_Pending;

        fs::path AutosaveDirectory() {
            auto project = Prism::Project::GetActive();
            if (project)
                s_LastDirectory = project->GetProjectDirectory() / "Cache" / "Autosave";
            return s_LastDirectory;
        }

        fs::path SceneFilePath(const fs::path& dir) { return dir / "autosave.prismmap"; }
        fs::path InfoFilePath(const fs::path& dir) { return dir / "autosave.info"; }

        void WriteInfo(const fs::path& dir, const fs::path& relativeMap, const std::string& sceneName) {
            const fs::path info = InfoFilePath(dir);
            const fs::path temp = Prism::AtomicTempPath(info);
            {
                std::ofstream out(temp, std::ios::binary);
                if (!out)
                    return;
                out << "Map=" << (relativeMap.empty() ? std::string() : FileDialog::ToUtf8(relativeMap)) << "\n";
                out << "Scene=" << sceneName << "\n";
                out << "Time=" << (long long)std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\n";
            }
            if (!Prism::CommitAtomicFile(temp, info))
                Prism::DiscardAtomicFile(temp);
        }

        // Le "Chave=Valor" por linha (chaves desconhecidas sao ignoradas).
        void ReadInfo(const fs::path& dir, std::string& map, std::string& sceneName, long long& time) {
            std::ifstream in(InfoFilePath(dir), std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const size_t eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                const std::string key = line.substr(0, eq);
                const std::string value = line.substr(eq + 1);
                if (key == "Map") map = value;
                else if (key == "Scene") sceneName = value;
                else if (key == "Time") time = std::atoll(value.c_str());
            }
        }

        std::string FormatTime(long long seconds) {
            if (seconds <= 0)
                return "hora desconhecida";
            const std::tm tm = Prism::FileBackup::detail::LocalTime((std::time_t)seconds);
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
            return buffer;
        }

    } // namespace

    bool IsRecoveryPending() {
        return s_Pending.Active;
    }

    void Discard() {
        const fs::path dir = AutosaveDirectory();
        if (dir.empty())
            return;
        std::error_code ec;
        fs::remove(SceneFilePath(dir), ec);
        fs::remove(InfoFilePath(dir), ec);
        fs::remove(Prism::AtomicTempPath(SceneFilePath(dir)), ec);
        fs::remove(Prism::AtomicTempPath(InfoFilePath(dir)), ec);
        s_Accumulated = 0.0f;
    }

    void PeekRecovery() {
        s_Pending = PendingRecovery{};
        s_Accumulated = 0.0f;

        auto project = Prism::Project::GetActive();
        if (!project)
            return;

        const fs::path dir = AutosaveDirectory();
        std::error_code ec;
        if (!fs::is_regular_file(SceneFilePath(dir), ec) || fs::file_size(SceneFilePath(dir), ec) == 0)
            return;

        std::string map, sceneName;
        long long time = 0;
        ReadInfo(dir, map, sceneName, time);

        s_Pending.Active = true;
        s_Pending.SceneFile = SceneFilePath(dir);
        s_Pending.SceneName = sceneName.empty() ? std::string("Cena") : sceneName;
        s_Pending.SavedAt = time;
        if (!map.empty())
            s_Pending.OriginalMap = project->GetProjectDirectory() / FileDialog::FromUtf8(map);
        PRISM_WARN("Foi encontrado um autosave de uma sessao que nao terminou normalmente (", FormatTime(time), ").");
    }

    void Tick(EditorContext& ctx, const SceneDocument& document, float deltaTime) {
        if (s_Pending.Active)
            return; // nao mexe nos arquivos enquanto o usuario nao decidiu o que fazer com eles

        s_Accumulated += deltaTime;
        if (s_Accumulated < kIntervalSeconds)
            return;
        s_Accumulated = 0.0f;

        auto project = Prism::Project::GetActive();
        if (!project || !ctx.ActiveScene)
            return;
        if (ctx.IsPlaying && ctx.IsPlaying())
            return; // a cena de edicao nao muda durante o Play; evita um engasgo a toa

        if (!document.HasUnsavedChanges()) {
            Discard(); // nada a proteger
            return;
        }

        const fs::path dir = AutosaveDirectory();
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
            return;

        Prism::SceneSerializer serializer(ctx.ActiveScene);
        if (!serializer.Serialize(SceneFilePath(dir), /*logSuccess*/ false)) {
            PRISM_WARN("Autosave falhou ao gravar em: ", dir.string());
            return;
        }

        fs::path relativeMap;
        if (!ctx.CurrentMapPath.empty()) {
            relativeMap = fs::relative(ctx.CurrentMapPath, project->GetProjectDirectory(), ec);
            if (ec)
                relativeMap.clear();
        }
        WriteInfo(dir, relativeMap, ctx.ActiveScene->GetName());
    }

    void RenderRecoveryPopup(EditorContext& ctx, SceneDocument& document) {
        (void)ctx;
        if (!s_Pending.Active)
            return;

        if (!s_Pending.PopupOpened) {
            ImGui::OpenPopup(kRecoveryPopupId);
            s_Pending.PopupOpened = true;
        }

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(kRecoveryPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        const bool hasMap = !s_Pending.OriginalMap.empty();
        ImGui::TextWrapped("O editor nao foi fechado normalmente na ultima vez.");
        ImGui::Dummy(ImVec2(0, 2));
        if (hasMap)
            ImGui::TextWrapped("Ha uma versao recuperada do mapa '%s', salva automaticamente em %s.",
                s_Pending.OriginalMap.filename().string().c_str(), FormatTime(s_Pending.SavedAt).c_str());
        else
            ImGui::TextWrapped("Ha uma versao recuperada da cena '%s' (que ainda nao tinha sido salva num arquivo), gravada automaticamente em %s.",
                s_Pending.SceneName.c_str(), FormatTime(s_Pending.SavedAt).c_str());
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::TextDisabled("Recuperar abre essa versao como alteracao nao salva: nada e gravado no mapa ate voce salvar.");
        ImGui::Dummy(ImVec2(0, 10));

        bool recover = ImGui::Button("Recuperar", ImVec2(120, 0));
        ImGui::SameLine();
        bool discard = ImGui::Button("Descartar", ImVec2(120, 0));

        if (recover) {
            // O mapa original vira o destino do proximo Ctrl+S; so se ele ainda existir (senao Salvar Como).
            std::error_code ec;
            fs::path associated = (hasMap && fs::exists(s_Pending.OriginalMap, ec)) ? s_Pending.OriginalMap : fs::path();
            const bool ok = document.LoadSceneFrom(s_Pending.SceneFile, associated, /*markUnsaved*/ true);
            if (!ok)
                PRISM_ERROR("Nao foi possivel abrir o autosave: ", s_Pending.SceneFile.string(), " (o arquivo foi mantido).");
            s_Pending = PendingRecovery{}; // os arquivos ficam; o proximo Tick regrava ou o Salvar apaga
            ImGui::CloseCurrentPopup();
        }
        else if (discard) {
            Discard();
            s_Pending = PendingRecovery{};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

}
