#include "GameExporter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <system_error>

namespace Prism {

    namespace fs = std::filesystem;

    namespace {

        constexpr const char* kManifestName = ".prism-export";
        constexpr const char* kManifestHeader = "PrismExport 1";

#if defined(_WIN32)
        constexpr const char* kExeExtension = ".exe";
        constexpr const char* kModuleExtension = ".dll";
#elif defined(__APPLE__)
        constexpr const char* kExeExtension = "";
        constexpr const char* kModuleExtension = ".dylib";
#else
        constexpr const char* kExeExtension = "";
        constexpr const char* kModuleExtension = ".so";
#endif

        // std::string UTF-8 <-> path sem depender da code page do Windows.
        fs::path FromUtf8(const std::string& text) {
            return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }
        std::string ToUtf8(const fs::path& path) {
            std::u8string text = path.u8string();
            return std::string(reinterpret_cast<const char*>(text.data()), text.size());
        }
        std::string Generic(const fs::path& path) {
            std::u8string text = path.generic_u8string();
            return std::string(reinterpret_cast<const char*>(text.data()), text.size());
        }

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
            });
            return text;
        }

        // 'child' esta dentro de 'parent' (ou e a mesma pasta)? Compara com fs::equivalent
        // subindo os ancestrais de 'child', entao nao depende de maiusculas/barras/links.
        bool IsInside(const fs::path& child, const fs::path& parent) {
            std::error_code ec;
            fs::path current = fs::weakly_canonical(child, ec);
            if (ec)
                current = child;
            if (!fs::exists(parent, ec))
                return false;
            while (true) {
                std::error_code step;
                if (fs::exists(current, step) && fs::equivalent(current, parent, step))
                    return true;
                fs::path up = current.parent_path();
                if (up.empty() || up == current)
                    return false;
                current = up;
            }
        }

        bool HasParentComponent(const fs::path& relative) {
            for (const auto& part : relative)
                if (part == "..")
                    return true;
            return false;
        }

        // --- Manifesto -------------------------------------------------------
        // Lista (um caminho relativo por linha, UTF-8) de tudo que uma exportacao criou. E o que
        // permite reexportar na mesma pasta sem apagar nada que o usuario tenha posto la.
        bool ReadManifest(const fs::path& manifest, std::vector<std::string>& entries) {
            std::ifstream in(manifest, std::ios::binary);
            if (!in)
                return false;
            std::string line;
            if (!std::getline(in, line))
                return false;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line != kManifestHeader)
                return false;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty() && line[0] != '#')
                    entries.push_back(line);
            }
            return true;
        }

        void WriteManifest(const fs::path& manifest, const GameExportOptions& options, const std::vector<std::string>& entries) {
            std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
            out << kManifestHeader << "\n";
            out << "# Gerado pela Prism Engine. Lista o que a exportacao criou; so isto e apagado numa nova exportacao.\n";
            if (!options.EngineVersion.empty())
                out << "# engine " << options.EngineVersion << "\n";
            for (const std::string& entry : entries)
                out << entry << "\n";
        }

        // Apaga os arquivos listados no manifesto (e as pastas que ficarem vazias).
        void CleanPrevious(const fs::path& output, const std::vector<std::string>& entries) {
            std::set<fs::path> directories;
            for (const std::string& entry : entries) {
                fs::path relative = FromUtf8(entry);
                if (relative.empty() || relative.is_absolute() || HasParentComponent(relative))
                    continue; // manifesto adulterado: ignora em vez de arriscar sair da pasta
                std::error_code ec;
                fs::remove(output / relative, ec);
                for (fs::path dir = relative.parent_path(); !dir.empty(); dir = dir.parent_path())
                    directories.insert(dir);
            }
            // Mais fundas primeiro.
            std::vector<fs::path> ordered(directories.begin(), directories.end());
            std::sort(ordered.begin(), ordered.end(), [](const fs::path& a, const fs::path& b) {
                return a.generic_string().size() > b.generic_string().size();
            });
            for (const fs::path& dir : ordered) {
                std::error_code ec;
                if (fs::is_directory(output / dir, ec) && fs::is_empty(output / dir, ec))
                    fs::remove(output / dir, ec);
            }
            std::error_code ec;
            fs::remove(output / kManifestName, ec);
        }

        // --- Filtros ---------------------------------------------------------
        // Arquivos do build do Runtime que um jogo distribuido nao precisa.
        bool SkipRuntimeFile(const fs::path& path) {
            const std::string ext = Lower(ToUtf8(path.extension()));
            const std::string name = Lower(ToUtf8(path.filename()));
            return ext == ".pdb" || ext == ".ilk" || ext == ".exp" || ext == ".lib" || ext == ".obj"
                || ext == ".idb" || ext == ".cmake" || name == "imgui.ini";
        }

        bool SkipProjectDirectory(const std::string& name, bool topLevel) {
            const std::string lower = Lower(name);
            if (lower == ".git" || lower == ".svn" || lower == ".hg")
                return true;
            if (!topLevel)
                return false;
            return lower == "source" || lower == "cache" || lower == "build" || lower == "out"
                || lower == ".vs" || lower == ".vscode" || lower == ".idea";
        }

        bool SkipProjectFile(const fs::path& path) {
            const std::string ext = Lower(ToUtf8(path.extension()));
            const std::string name = Lower(ToUtf8(path.filename()));
            if (name == ".gitignore" || name == ".gitattributes" || name == "imgui.ini" || name == "thumbs.db" || name == ".ds_store")
                return true;
            // Backups e fontes de arte que a engine nao le em tempo de execucao.
            return ext == ".bak" || ext == ".tmp" || ext == ".blend" || ext == ".blend1" || ext == ".psd" || ext == ".kra" || ext == ".xcf";
        }

        bool IsSourceFile(const fs::path& path) {
            const std::string ext = Lower(ToUtf8(path.extension()));
            const std::string name = ToUtf8(path.filename());
            if (name == "PrismGenerated.cmake")
                return false; // regravado toda vez que o projeto abre: nao indica codigo novo
            return ext == ".cpp" || ext == ".cc" || ext == ".c" || ext == ".h" || ext == ".hpp" || name == "CMakeLists.txt";
        }

        // Um exportador por chamada: guarda o estado para o codigo ficar linear.
        class Exporter {
        public:
            Exporter(const GameExportOptions& options, GameExportResult& result, const GameExportLog& log)
                : m_Options(options), m_Result(result), m_Log(log) {}

            void Run();
            const std::vector<std::string>& Created() const { return m_Created; }

        private:
            void Say(const std::string& text) const { if (m_Log) m_Log(text); }
            void Fail(const std::string& text) const { throw std::runtime_error(text); }

            void Validate(fs::path& runtimeExe);
            void CopyRuntime(const fs::path& runtimeExe, const std::string& exeName);
            void CopyProject();
            void CheckNativeModule();
            void CopyOne(const fs::path& source, const fs::path& relativeDestination);

            const GameExportOptions& m_Options;
            GameExportResult& m_Result;
            const GameExportLog& m_Log;
            std::vector<std::string> m_Created;
        };

        void Exporter::CopyOne(const fs::path& source, const fs::path& relativeDestination) {
            fs::path destination = m_Options.OutputDirectory / relativeDestination;
            fs::create_directories(destination.parent_path());
            fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
            m_Created.push_back(Generic(relativeDestination));
            m_Result.FilesCopied++;
            std::error_code ec;
            uintmax_t size = fs::file_size(destination, ec);
            if (!ec)
                m_Result.BytesCopied += size;
        }

        void Exporter::Validate(fs::path& runtimeExe) {
            std::error_code ec;

            if (m_Options.ProjectDirectory.empty() || !fs::is_directory(m_Options.ProjectDirectory, ec))
                Fail("A pasta do projeto nao existe: " + ToUtf8(m_Options.ProjectDirectory));
            bool hasProjectFile = false;
            for (const auto& entry : fs::directory_iterator(m_Options.ProjectDirectory, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".prismproj") {
                    hasProjectFile = true;
                    break;
                }
            }
            if (!hasProjectFile)
                Fail("Nao ha arquivo .prismproj em " + ToUtf8(m_Options.ProjectDirectory) + ".");

            if (m_Options.StartMap.empty())
                Fail("O projeto nao tem mapa inicial. Salve um mapa no editor antes de exportar.");
            fs::path startMap = m_Options.ProjectDirectory / m_Options.MapDirectory / m_Options.StartMap;
            if (!fs::is_regular_file(startMap, ec))
                Fail("O mapa inicial nao existe: " + ToUtf8(startMap));

            if (m_Options.RuntimeDirectory.empty() || !fs::is_directory(m_Options.RuntimeDirectory, ec))
                Fail("A pasta do PrismRuntime nao foi encontrada: " + ToUtf8(m_Options.RuntimeDirectory)
                     + ". Compile o alvo PrismRuntime junto com o editor.");
            runtimeExe = m_Options.RuntimeDirectory / (std::string("PrismRuntime") + kExeExtension);
            if (!fs::is_regular_file(runtimeExe, ec))
                Fail("O executavel do Runtime nao existe: " + ToUtf8(runtimeExe) + ". Compile o alvo PrismRuntime.");

            if (m_Options.OutputDirectory.empty())
                Fail("Escolha a pasta de destino da exportacao.");
            if (IsInside(m_Options.OutputDirectory, m_Options.ProjectDirectory))
                Fail("A pasta de destino nao pode ficar dentro da pasta do projeto.");
            if (IsInside(m_Options.OutputDirectory, m_Options.RuntimeDirectory))
                Fail("A pasta de destino nao pode ficar dentro da pasta do Runtime.");
            if (IsInside(m_Options.ProjectDirectory, m_Options.OutputDirectory))
                Fail("A pasta de destino contem o projeto: escolha outra (ela receberia uma copia dele dentro de si mesma).");
            if (IsInside(m_Options.RuntimeDirectory, m_Options.OutputDirectory))
                Fail("A pasta de destino contem o Runtime: escolha outra.");

            if (fs::exists(m_Options.OutputDirectory, ec)) {
                if (!fs::is_directory(m_Options.OutputDirectory, ec))
                    Fail("O destino existe e nao e uma pasta: " + ToUtf8(m_Options.OutputDirectory));
                const fs::path manifest = m_Options.OutputDirectory / kManifestName;
                std::vector<std::string> previous;
                if (fs::exists(manifest, ec)) {
                    if (!ReadManifest(manifest, previous))
                        Fail("O arquivo " + std::string(kManifestName) + " do destino esta ilegivel. Apague-o ou escolha outra pasta.");
                    Say("Limpando a exportacao anterior...");
                    CleanPrevious(m_Options.OutputDirectory, previous);
                }
                else if (!fs::is_empty(m_Options.OutputDirectory, ec)) {
                    Fail("A pasta de destino nao esta vazia e nao e uma exportacao anterior da Prism. "
                         "Escolha uma pasta vazia (nada do que esta nela seria apagado, mas ficaria misturado).");
                }
            }
            else {
                fs::create_directories(m_Options.OutputDirectory);
            }
        }

        void Exporter::CopyRuntime(const fs::path& runtimeExe, const std::string& exeName) {
            Say("Copiando o Runtime...");
            const uint32_t before = m_Result.FilesCopied;
            const fs::path exeDestination = FromUtf8(exeName + kExeExtension);

            for (auto it = fs::recursive_directory_iterator(m_Options.RuntimeDirectory, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it) {
                const fs::path& path = it->path();
                std::error_code ec;
                if (it->is_directory(ec)) {
                    if (Lower(ToUtf8(path.filename())) == "cmakefiles")
                        it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(ec) || SkipRuntimeFile(path))
                    continue;
                if (fs::equivalent(path, runtimeExe, ec))
                    CopyOne(path, exeDestination);
                else
                    CopyOne(path, fs::relative(path, m_Options.RuntimeDirectory));
            }
            m_Result.Executable = m_Options.OutputDirectory / exeDestination;
            Say("  " + std::to_string(m_Result.FilesCopied - before) + " arquivo(s) do Runtime.");
        }

        void Exporter::CopyProject() {
            Say("Copiando o projeto para Game/...");
            const uint32_t before = m_Result.FilesCopied;
            const fs::path gameRoot = "Game";
            const std::string moduleName = std::string("NativeScripts") + kModuleExtension;
            uint32_t skipped = 0;

            for (auto it = fs::recursive_directory_iterator(m_Options.ProjectDirectory, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it) {
                const fs::path& path = it->path();
                const fs::path relative = fs::relative(path, m_Options.ProjectDirectory);
                const bool topLevel = std::distance(relative.begin(), relative.end()) == 1;
                std::error_code ec;

                if (it->is_directory(ec)) {
                    if (SkipProjectDirectory(ToUtf8(path.filename()), topLevel)) {
                        it.disable_recursion_pending();
                        skipped++;
                    }
                    continue;
                }
                if (!it->is_regular_file(ec))
                    continue;
                if (SkipProjectFile(path)) {
                    skipped++;
                    continue;
                }
                // Binaries/: so o modulo de scripts (sem .pdb, .ilk, .lib...).
                if (Lower(ToUtf8(*relative.begin())) == "binaries" && ToUtf8(path.filename()) != moduleName) {
                    skipped++;
                    continue;
                }
                CopyOne(path, gameRoot / relative);
            }
            Say("  " + std::to_string(m_Result.FilesCopied - before) + " arquivo(s) do projeto ("
                + std::to_string(skipped) + " ignorado(s): Source, Cache, backups...).");
        }

        void Exporter::CheckNativeModule() {
            std::error_code ec;
            const fs::path source = m_Options.ProjectDirectory / "Source";
            if (!fs::is_directory(source, ec))
                return; // projeto sem scripts C++: nada a conferir
            const fs::path module = m_Options.ProjectDirectory / "Binaries" / (std::string("NativeScripts") + kModuleExtension);
            if (!fs::is_regular_file(module, ec)) {
                m_Result.Warnings.push_back("O projeto tem scripts C++ (Source/), mas o modulo nao esta compilado (Binaries/NativeScripts"
                                            + std::string(kModuleExtension) + "). No jogo exportado os scripts C++ nao vao rodar: use o botao Compilar e exporte de novo.");
                return;
            }
            const auto moduleTime = fs::last_write_time(module, ec);
            if (ec)
                return;
            for (const auto& entry : fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied, ec)) {
                std::error_code fileError;
                if (!entry.is_regular_file(fileError) || !IsSourceFile(entry.path()))
                    continue;
                const auto sourceTime = fs::last_write_time(entry.path(), fileError);
                if (!fileError && sourceTime > moduleTime) {
                    m_Result.Warnings.push_back("O modulo de scripts C++ parece desatualizado (" + ToUtf8(entry.path().filename())
                                                + " e mais novo que NativeScripts" + kModuleExtension + "). Use o botao Compilar e exporte de novo.");
                    return;
                }
            }
        }

        void Exporter::Run() {
            fs::path runtimeExe;
            Validate(runtimeExe);

            const std::string exeName = SanitizeGameName(m_Options.GameName);
            if (exeName != m_Options.GameName)
                Say("Nome do executavel ajustado para '" + exeName + "'.");

            CheckNativeModule();
            CopyRuntime(runtimeExe, exeName);
            CopyProject();
        }

    }

    std::string SanitizeGameName(const std::string& name) {
        std::string out;
        out.reserve(name.size());
        for (unsigned char c : name) {
            if (c < 32 || c == 127 || std::strchr("\\/:*?\"<>|", c))
                out.push_back('_');
            else
                out.push_back((char)c);
        }

        auto trim = [&out]() {
            while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
                out.pop_back();
            size_t begin = 0;
            while (begin < out.size() && (out[begin] == ' ' || out[begin] == '.'))
                begin++;
            out.erase(0, begin);
        };
        trim();

        constexpr size_t kMaxBytes = 64;
        if (out.size() > kMaxBytes) {
            size_t cut = kMaxBytes;
            while (cut > 0 && (((unsigned char)out[cut]) & 0xC0) == 0x80) // nao corta no meio de um caractere UTF-8
                cut--;
            out.resize(cut);
            trim();
        }

        if (out.empty())
            return "Game";

        // Nomes reservados do Windows (com ou sem extensao): CON, PRN, AUX, NUL, COM1-9, LPT1-9.
        const std::string upper = [&out]() {
            std::string text = out.substr(0, out.find('.'));
            for (char& c : text)
                if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            return text;
        }();
        bool reserved = upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL";
        if (upper.size() == 4 && (upper.compare(0, 3, "COM") == 0 || upper.compare(0, 3, "LPT") == 0) && upper[3] >= '1' && upper[3] <= '9')
            reserved = true;
        if (reserved)
            out.push_back('_');
        return out;
    }

    GameExportResult ExportGame(const GameExportOptions& options, const GameExportLog& log) {
        GameExportResult result;
        Exporter exporter(options, result, log);
        try {
            exporter.Run();
            result.Success = true;
        }
        catch (const std::exception& error) {
            result.Success = false;
            result.Error = error.what();
        }
        catch (...) {
            result.Success = false;
            result.Error = "Erro desconhecido durante a exportacao.";
        }

        // Grava o manifesto mesmo em falha parcial: assim a proxima exportacao limpa o que sobrou.
        if (!exporter.Created().empty()) {
            try {
                WriteManifest(options.OutputDirectory / kManifestName, options, exporter.Created());
            }
            catch (...) {
                if (result.Success) {
                    result.Success = false;
                    result.Error = "Nao foi possivel gravar o manifesto da exportacao em " + ToUtf8(options.OutputDirectory) + ".";
                }
            }
        }

        if (log) {
            for (const std::string& warning : result.Warnings)
                log("AVISO: " + warning);
            if (result.Success) {
                const double megabytes = (double)result.BytesCopied / (1024.0 * 1024.0);
                char text[160];
                std::snprintf(text, sizeof(text), "Pronto: %u arquivo(s), %.1f MB.", result.FilesCopied, megabytes);
                log(text);
                log("Executavel: " + ToUtf8(result.Executable));
            }
            else {
                log("ERRO: " + result.Error);
            }
        }
        return result;
    }

}
