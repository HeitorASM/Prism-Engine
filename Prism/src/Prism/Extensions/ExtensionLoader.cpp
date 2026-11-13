#include "ExtensionLoader.h"
#include "ExtensionAPI.h"
#include "SystemRegistry.h"
#include "ExtensionHooks.h"
#include "ExtensionUi.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"
#include "../Project/Project.h"

#include <PrismVersion.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <exception>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace Prism {

    namespace {
        struct Loaded {
            std::string Name;
            void* Handle = nullptr;
            void (*ShutdownFn)() = nullptr;
            bool ShutDown = false;
        };

        std::vector<ExtensionInfo> s_Extensions;
        std::vector<Loaded> s_Loaded; // ordem de carga

        void* OpenLibrary(const std::filesystem::path& path) {
#if defined(_WIN32)
            return (void*)LoadLibraryW(path.wstring().c_str());
#else
            return dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        }
        void* FindSymbol(void* handle, const char* name) {
#if defined(_WIN32)
            return (void*)GetProcAddress((HMODULE)handle, name);
#else
            return dlsym(handle, name);
#endif
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
        std::string LibraryFileName(const std::string& library) {
#if defined(_WIN32)
            return library + LibraryExtension();
#else
            return "lib" + library + LibraryExtension();
#endif
        }

        std::filesystem::path FindLibrary(const std::filesystem::path& dir, const std::string& library) {
            std::error_code ec;
            // Windows: Nome.dll; Linux/macOS: libNome.so (padrao do CMake) ou Nome.so.
            for (const std::filesystem::path& base : { dir, dir / "Binaries" }) {
                for (const std::string& file : { LibraryFileName(library), library + LibraryExtension() }) {
                    std::filesystem::path candidate = base / file;
                    if (std::filesystem::is_regular_file(candidate, ec))
                        return candidate;
                }
            }
            return {};
        }

        // No EDITOR a biblioteca e aberta a partir de uma COPIA numa pasta temporaria: no Windows uma
        // dll carregada fica travada, e o botao "Compilar" da aba Extensoes precisa poder sobrescrever
        // a original enquanto o editor roda. Copia os arquivos da pasta da biblioteca (dlls vizinhas e
        // .pdb), nao recursivo. Devolve vazio se falhar (o chamador abre a original).
        std::filesystem::path MakeCacheCopy(const std::filesystem::path& library, const std::string& name) {
            namespace fs = std::filesystem;
            std::error_code ec;
            static fs::path s_SessionRoot;
            if (s_SessionRoot.empty()) {
                const fs::path root = fs::temp_directory_path(ec) / "PrismExtensionCache";
                // Sobras de sessoes anteriores (as de um editor ainda aberto falham ao apagar - sem problema).
                for (const auto& entry : fs::directory_iterator(root, ec)) {
                    std::error_code ignored;
                    fs::remove_all(entry.path(), ignored);
                }
                const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                s_SessionRoot = root / std::to_string((long long)stamp);
            }
            const fs::path destinationDir = s_SessionRoot / name;
            fs::create_directories(destinationDir, ec);
            if (ec) return {};
            for (const auto& entry : fs::directory_iterator(library.parent_path(), ec)) {
                std::error_code fileError;
                if (!entry.is_regular_file(fileError)) continue;
                fs::copy_file(entry.path(), destinationDir / entry.path().filename(), fs::copy_options::overwrite_existing, fileError);
                if (fileError && entry.path() == library) return {}; // a principal precisa ter sido copiada
            }
            const fs::path copy = destinationDir / library.filename();
            return fs::is_regular_file(copy, ec) ? copy : fs::path();
        }

        bool ReadFile(const std::filesystem::path& path, std::string& out) {
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;
            std::ostringstream ss;
            ss << file.rdbuf();
            out = ss.str();
            return true;
        }

        // --- Tabela entregue as extensoes ----------------------------------------------------
        bool HostRegisterComponent(ExtensionComponentDesc* desc, ExtensionComponentOps* ops) {
            if (!desc || !ops) return false;
            return ComponentRegistry::RegisterExtensionRaw(std::move(*desc), std::move(*ops));
        }
        bool HostRegisterSystem(IEngineSystem* system, const char* source) {
            // Assume a posse mesmo se for recusado (nome repetido): quem registra nao deve dar delete.
            return SystemRegistry::Register(Scope<IEngineSystem>(system), source ? source : "");
        }
        void HostLog(int level, const char* message) {
            const char* text = message ? message : "";
            if (level >= 2) PRISM_CORE_ERROR("[extensao] ", text);
            else if (level == 1) PRISM_CORE_WARN("[extensao] ", text);
            else PRISM_CORE_INFO("[extensao] ", text);
        }

        bool UiDragFloat(const char* l, float* v, float s, float mn, float mx) { return ImGui::DragFloat(l, v, s, mn, mx); }
        bool UiDragInt(const char* l, int* v, float s, int mn, int mx) { return ImGui::DragInt(l, v, s, mn, mx); }
        bool UiCheckbox(const char* l, bool* v) { return ImGui::Checkbox(l, v); }
        bool UiInputText(const char* l, char* b, size_t n) { return ImGui::InputText(l, b, n); }
        void UiText(const char* t) { ImGui::TextUnformatted(t ? t : ""); }

        // --- v2: widgets extras (host.UiEx) e registros de interface/hooks ---------------------
        const char* Safe(const char* t) { return t ? t : ""; }
        bool UxButton(const char* l) { return ImGui::Button(Safe(l)); }
        void UxSameLine() { ImGui::SameLine(); }
        void UxSeparator() { ImGui::Separator(); }
        void UxSpacing() { ImGui::Spacing(); }
        void UxTextWrapped(const char* t) { ImGui::PushTextWrapPos(0.0f); ImGui::TextUnformatted(Safe(t)); ImGui::PopTextWrapPos(); }
        void UxTextDisabled(const char* t) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextUnformatted(Safe(t)); ImGui::PopStyleColor(); }
        void UxTextColored(float r, float g, float b, float a, const char* t) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(r, g, b, a)); ImGui::TextUnformatted(Safe(t)); ImGui::PopStyleColor(); }
        bool UxSliderFloat(const char* l, float* v, float mn, float mx) { return ImGui::SliderFloat(Safe(l), v, mn, mx); }
        bool UxSliderInt(const char* l, int* v, int mn, int mx) { return ImGui::SliderInt(Safe(l), v, mn, mx); }
        bool UxInputFloat(const char* l, float* v) { return ImGui::InputFloat(Safe(l), v); }
        bool UxInputInt(const char* l, int* v) { return ImGui::InputInt(Safe(l), v); }
        bool UxInputTextMultiline(const char* l, char* b, size_t n, float w, float h) { return ImGui::InputTextMultiline(Safe(l), b, n, ImVec2(w, h)); }
        bool UxCombo(const char* l, int* c, const char* items) { return ImGui::Combo(Safe(l), c, items ? items : "\0"); }
        bool UxColorEdit4(const char* l, float* rgba) { return ImGui::ColorEdit4(Safe(l), rgba); }
        bool UxSelectable(const char* l, bool selected) { return ImGui::Selectable(Safe(l), selected); }
        bool UxCollapsingHeader(const char* l) { return ImGui::CollapsingHeader(Safe(l)); }
        bool UxTreeNode(const char* l) { return ImGui::TreeNode(Safe(l)); }
        void UxTreePop() { ImGui::TreePop(); }
        void UxBeginDisabled(bool d) { ImGui::BeginDisabled(d); }
        void UxEndDisabled() { ImGui::EndDisabled(); }
        void UxPushId(const char* id) { ImGui::PushID(Safe(id)); }
        void UxPopId() { ImGui::PopID(); }
        void UxSetTooltipOnHover(const char* t) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Safe(t)); }
        void UxProgressBar(float f, const char* overlay) { ImGui::ProgressBar(f, ImVec2(-FLT_MIN, 0.0f), overlay); }
        void UxGetContentRegionAvail(float* w, float* h) { const ImVec2 a = ImGui::GetContentRegionAvail(); if (w) *w = a.x; if (h) *h = a.y; }

        bool HostRegisterPanel(ExtensionPanelDesc* desc, const char* source) {
            if (!desc) return false;
            const std::string src = Safe(source);
            const bool ok = ExtensionUiRegistry::RegisterPanel(std::move(*desc), src);
            if (!ok) PRISM_CORE_WARN("Extensao '", src, "': painel recusado (Id/Title/Draw faltando ou Id repetido).");
            return ok;
        }
        bool HostRegisterMenuItem(ExtensionMenuItemDesc* desc, const char* source) {
            if (!desc) return false;
            const std::string src = Safe(source);
            const bool ok = ExtensionUiRegistry::RegisterMenuItem(std::move(*desc), src);
            if (!ok) PRISM_CORE_WARN("Extensao '", src, "': item de menu recusado (Path invalido ou sem Action).");
            return ok;
        }
        bool HostSetPanelOpen(const char* source, const char* id, bool open) {
            return ExtensionUiRegistry::SetPanelOpen(Safe(source), Safe(id), open);
        }
        bool HostRegisterComponentHook(ExtensionComponentHookDesc* desc, const char* source) {
            return desc && ExtensionHooks::RegisterComponentHook(std::move(*desc), Safe(source));
        }
        bool HostRegisterStageHook(ExtensionStageHookDesc* desc, const char* source) {
            return desc && ExtensionHooks::RegisterStageHook(std::move(*desc), Safe(source));
        }

        // Tira tudo que a extensao registrou nos registros da engine.
        void UnregisterEverything(const std::string& name) {
            SystemRegistry::UnregisterSource(name);
            ExtensionUiRegistry::UnregisterSource(name);
            ExtensionHooks::UnregisterSource(name);
        }

        void Record(ExtensionInfo info, ExtensionState state, const std::string& message) {
            info.State = state;
            info.Message = message;
            if (state == ExtensionState::Failed)
                PRISM_CORE_ERROR("Extensao '", info.Manifest.Name, "': ", message);
            else if (state == ExtensionState::Skipped)
                PRISM_CORE_INFO("Extensao '", info.Manifest.Name, "' ignorada: ", message);
            s_Extensions.push_back(std::move(info));
        }

        bool IsKnown(const std::string& name) {
            for (const ExtensionInfo& e : s_Extensions)
                if (e.Manifest.Name == name) return true;
            return false;
        }
    }

    const std::vector<ExtensionInfo>& ExtensionLoader::GetExtensions() { return s_Extensions; }

    bool ExtensionLoader::IsLoaded(const std::string& name) {
        for (const ExtensionInfo& e : s_Extensions)
            if (e.Manifest.Name == name && e.State == ExtensionState::Loaded) return true;
        return false;
    }

    void ExtensionLoader::LoadInstalled(bool isEditor) {
        LoadFromDirectory(GetExecutableDirectory() / "Extensions", isEditor, false, {});
    }

    void ExtensionLoader::LoadProject(const Project& project, bool isEditor) {
        LoadFromDirectory(project.GetProjectDirectory() / "Extensions", isEditor, true, project.GetProjectDirectory());
    }

    void ExtensionLoader::LoadFromDirectory(const std::filesystem::path& root, bool isEditor, bool fromProject,
                                            const std::filesystem::path& projectDirectory) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec))
            return; // sem pasta de extensoes: nada a fazer, nao e erro

        std::vector<std::filesystem::path> dirs;
        for (auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (entry.is_directory(ec) && std::filesystem::is_regular_file(entry.path() / "extension.ini", ec))
                dirs.push_back(entry.path());
        }
        std::sort(dirs.begin(), dirs.end());

        // 1) manifestos
        std::vector<ExtensionManifest> manifests;
        std::vector<ExtensionInfo> pending; // paralelo a 'manifests'
        for (const auto& dir : dirs) {
            const bool seenFolder = std::any_of(s_Extensions.begin(), s_Extensions.end(),
                [&](const ExtensionInfo& e) { return e.Directory == dir; });
            if (seenFolder) continue; // ja processada (carregada, ignorada ou com erro) nesta sessao

            ExtensionInfo info;
            info.Directory = dir;
            info.FromProject = fromProject;
            info.Manifest.Name = dir.filename().string(); // nome provisorio para o log, se o manifesto falhar

            std::string text;
            if (!ReadFile(dir / "extension.ini", text)) {
                Record(std::move(info), ExtensionState::Failed, "nao foi possivel ler extension.ini.");
                continue;
            }
            ManifestParseResult parsed = ParseExtensionManifest(text);
            if (!parsed.Ok) {
                Record(std::move(info), ExtensionState::Failed, "extension.ini invalido: " + parsed.Error);
                continue;
            }
            for (const std::string& w : parsed.Warnings)
                PRISM_CORE_WARN("Extensao '", parsed.Manifest.Name, "': ", w);
            info.Manifest = parsed.Manifest;

            if (IsKnown(info.Manifest.Name)) {
                // Ja carregada/registrada nesta sessao (ex: reabrir o projeto): nada a refazer.
                PRISM_CORE_INFO("Extensao '", info.Manifest.Name, "' ja conhecida - ignorando '", dir.string(), "'.");
                continue;
            }
            const bool duplicate = std::any_of(manifests.begin(), manifests.end(),
                [&](const ExtensionManifest& m) { return m.Name == info.Manifest.Name; });
            if (duplicate) {
                Record(std::move(info), ExtensionState::Failed, "nome repetido em outra pasta de extensao.");
                continue;
            }
            manifests.push_back(info.Manifest);
            pending.push_back(std::move(info));
        }
        if (manifests.empty())
            return;

        // 2) ordem por dependencia
        std::set<std::string> alreadyLoaded;
        for (const ExtensionInfo& e : s_Extensions)
            if (e.State == ExtensionState::Loaded) alreadyLoaded.insert(e.Manifest.Name);
        std::map<std::string, std::string> failures;
        std::vector<std::string> order = ResolveLoadOrder(manifests, alreadyLoaded, failures);

        auto findPending = [&](const std::string& name) -> ExtensionInfo* {
            for (ExtensionInfo& p : pending) if (p.Manifest.Name == name) return &p;
            return nullptr;
        };

        for (auto& [name, why] : failures)
            if (ExtensionInfo* p = findPending(name))
                Record(*p, ExtensionState::Failed, why);

        // 3) carga
        for (const std::string& name : order) {
            ExtensionInfo* p = findPending(name);
            if (!p) continue;
            ExtensionInfo info = *p;
            const ExtensionManifest& m = info.Manifest;

            if (!m.Enabled) { Record(info, ExtensionState::Skipped, "desligada no manifesto (enabled = false)."); continue; }
            if ((m.Target == ExtensionTarget::Editor && !isEditor) || (m.Target == ExtensionTarget::Runtime && isEditor)) {
                Record(info, ExtensionState::Skipped, std::string("alvo '") + ExtensionTargetName(m.Target) + "' nao se aplica a este executavel.");
                continue;
            }
            if (m.ApiVersion < kExtensionApiMinVersion || m.ApiVersion > kExtensionApiVersion) {
                Record(info, ExtensionState::Failed, "compilada para a API " + std::to_string(m.ApiVersion) +
                    " (a engine aceita da " + std::to_string(kExtensionApiMinVersion) + " a " + std::to_string(kExtensionApiVersion) +
                    "). " + (m.ApiVersion > kExtensionApiVersion ? "Atualize a engine." : "Recompile a extensao."));
                continue;
            }
            // dependencia que ficou Skipped/Failed nesta ou em outra rodada nao conta como carregada
            bool depsOk = true;
            for (const std::string& dep : m.Dependencies) {
                if (!IsLoaded(dep)) {
                    Record(info, ExtensionState::Failed, "a dependencia '" + dep + "' nao esta carregada.");
                    depsOk = false;
                    break;
                }
            }
            if (!depsOk) continue;

            info.Library = FindLibrary(info.Directory, m.Library);
            if (info.Library.empty()) {
                Record(info, ExtensionState::Failed, "biblioteca '" + LibraryFileName(m.Library) + "' nao encontrada (nem em Binaries/).");
                continue;
            }

            info.LoadedFrom = info.Library;
            if (isEditor) {
                std::filesystem::path copy = MakeCacheCopy(info.Library, m.Name);
                if (!copy.empty())
                    info.LoadedFrom = copy;
            }
            void* handle = OpenLibrary(info.LoadedFrom);
            if (!handle) {
                Record(info, ExtensionState::Failed, "nao foi possivel abrir '" + info.Library.string() + "' (compilador/configuracao diferentes da engine? dependencias faltando?).");
                continue;
            }

            using VersionFn = uint32_t (*)();
            using InitFn = bool (*)(const ExtensionHost*);
            using ShutdownFn = void (*)();
            auto versionFn = (VersionFn)FindSymbol(handle, "PrismExtension_ApiVersion");
            auto initFn = (InitFn)FindSymbol(handle, "PrismExtension_Init");
            auto shutdownFn = (ShutdownFn)FindSymbol(handle, "PrismExtension_Shutdown");
            if (!versionFn || !initFn) {
                Record(info, ExtensionState::Failed, "a biblioteca nao exporta os pontos de entrada (falta PRISM_EXTENSION(...)).");
                continue; // fica aberta de proposito (ver Shutdown)
            }
            const uint32_t libraryApi = versionFn();
            if (libraryApi < kExtensionApiMinVersion || libraryApi > kExtensionApiVersion) {
                Record(info, ExtensionState::Failed, "biblioteca compilada para a API " + std::to_string(libraryApi) +
                    " (a engine aceita da " + std::to_string(kExtensionApiMinVersion) + " a " + std::to_string(kExtensionApiVersion) + "). Recompile.");
                continue;
            }
            if (libraryApi != m.ApiVersion)
                PRISM_CORE_WARN("Extensao '", m.Name, "': o manifesto diz api = ", m.ApiVersion, " mas a biblioteca foi compilada para a ", libraryApi, ".");

            const std::string nameStr = m.Name;
            const std::string dirStr = info.Directory.string();
            const std::string projectStr = projectDirectory.string();
            ExtensionHost host;
            host.ApiVersion = kExtensionApiVersion;
            host.StructSize = (uint32_t)sizeof(ExtensionHost);
            host.EngineVersion = PRISM_VERSION;
            host.ExtensionName = nameStr.c_str();
            host.ExtensionDirectory = dirStr.c_str();
            host.ProjectDirectory = projectStr.c_str();
            host.IsEditor = isEditor;
            host.ImGuiContext = ImGui::GetCurrentContext();
            host.Ui.DragFloat = &UiDragFloat;
            host.Ui.DragInt = &UiDragInt;
            host.Ui.Checkbox = &UiCheckbox;
            host.Ui.InputText = &UiInputText;
            host.Ui.Text = &UiText;
            host.RegisterComponentRaw = &HostRegisterComponent;
            host.RegisterSystemRaw = &HostRegisterSystem;
            host.LogRaw = &HostLog;

            // v2
            host.UiEx = { &UxButton, &UxSameLine, &UxSeparator, &UxSpacing, &UxTextWrapped, &UxTextDisabled, &UxTextColored,
                          &UxSliderFloat, &UxSliderInt, &UxInputFloat, &UxInputInt, &UxInputTextMultiline, &UxCombo, &UxColorEdit4,
                          &UxSelectable, &UxCollapsingHeader, &UxTreeNode, &UxTreePop, &UxBeginDisabled, &UxEndDisabled,
                          &UxPushId, &UxPopId, &UxSetTooltipOnHover, &UxProgressBar, &UxGetContentRegionAvail };
            host.RegisterPanelRaw = &HostRegisterPanel;
            host.RegisterMenuItemRaw = &HostRegisterMenuItem;
            host.SetPanelOpenRaw = &HostSetPanelOpen;
            host.RegisterComponentHookRaw = &HostRegisterComponentHook;
            host.RegisterStageHookRaw = &HostRegisterStageHook;
            {
                ImGuiMemAllocFunc allocFn = nullptr;
                ImGuiMemFreeFunc freeFn = nullptr;
                void* allocUserData = nullptr;
                ImGui::GetAllocatorFunctions(&allocFn, &freeFn, &allocUserData);
                host.ImGuiAllocFn = (void*)allocFn;
                host.ImGuiFreeFn = (void*)freeFn;
                host.ImGuiAllocUserData = allocUserData;
            }

            bool initOk = false;
            std::string error;
            try {
                initOk = initFn(&host);
                if (!initOk) error = "Init devolveu false.";
            } catch (const std::exception& e) {
                error = std::string("Init lancou excecao: ") + e.what();
            } catch (...) {
                error = "Init lancou uma excecao desconhecida.";
            }
            if (!initOk) {
                UnregisterEverything(nameStr); // desfaz o que ja tinha entrado (sistemas, paineis, menus, hooks)
                Record(info, ExtensionState::Failed, error + " (components ja registrados por ela continuam no registro ate fechar o programa)");
                continue;
            }

            Loaded loaded;
            loaded.Name = nameStr;
            loaded.Handle = handle;
            loaded.ShutdownFn = shutdownFn;
            s_Loaded.push_back(loaded);
            PRISM_CORE_INFO("Extensao '", nameStr, "' ", m.Version, " carregada", fromProject ? " (projeto)." : ".");
            Record(info, ExtensionState::Loaded, "");
        }
    }

    void ExtensionLoader::Shutdown() {
        for (auto it = s_Loaded.rbegin(); it != s_Loaded.rend(); ++it) {
            if (it->ShutDown) continue;
            it->ShutDown = true;
            if (it->ShutdownFn) {
                try { it->ShutdownFn(); }
                catch (const std::exception& e) { PRISM_CORE_ERROR("Extensao '", it->Name, "': Shutdown lancou excecao: ", e.what()); }
                catch (...) { PRISM_CORE_ERROR("Extensao '", it->Name, "': Shutdown lancou uma excecao desconhecida."); }
            }
            UnregisterEverything(it->Name);
        }
        // As bibliotecas NAO sao descarregadas (dlclose/FreeLibrary): objetos criados por elas
        // (pools de components no registro das cenas, sistemas, std::function) guardam ponteiros
        // para o codigo da dll e podem ser destruidos DEPOIS deste ponto (as cenas morrem junto com
        // as Layers). O SO as libera ao fim do processo.
    }

} // namespace Prism
