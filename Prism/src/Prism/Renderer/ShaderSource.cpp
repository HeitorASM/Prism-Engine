#include "ShaderSource.h"

#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Prism {

    namespace {

        struct Context {
            fs::path RootDir;
            const ShaderDefines* Defines = nullptr;
            std::vector<fs::path> Files;
            std::vector<std::string> Stack;           // arquivos em expansao (deteccao de ciclo)
            std::unordered_set<std::string> Included; // '#pragma once' implicito
            std::string Out;
            std::string Error;
            bool VersionSeen = false;
        };

        std::string Canonical(const fs::path& p) {
            std::error_code ec;
            fs::path c = fs::weakly_canonical(p, ec);
            return (ec ? p : c).generic_string();
        }

        bool ReadFile(const fs::path& p, std::string& out) {
            std::ifstream in(p, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
                out.erase(0, 3); // BOM UTF-8: o compilador GLSL nao o aceita
            return true;
        }

        std::string_view Trim(std::string_view s) {
            size_t b = 0, e = s.size();
            while (b < e && (s[b] == ' ' || s[b] == '\t')) b++;
            while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
            return s.substr(b, e - b);
        }

        bool StartsWithDirective(std::string_view trimmed, std::string_view name, std::string_view& rest) {
            if (trimmed.empty() || trimmed[0] != '#')
                return false;
            trimmed.remove_prefix(1);
            trimmed = Trim(trimmed);
            if (trimmed.substr(0, name.size()) != name)
                return false;
            if (trimmed.size() > name.size()) { // '#versionX' nao e '#version'
                char c = trimmed[name.size()];
                if (c != ' ' && c != '\t' && c != '"' && c != '<')
                    return false;
            }
            rest = Trim(trimmed.substr(name.size()));
            return true;
        }

        std::string Where(const fs::path& file, int line) {
            return file.generic_string() + ":" + std::to_string(line);
        }

        bool Fail(Context& ctx, std::string message) {
            ctx.Error = std::move(message);
            ctx.Stack.pop_back();
            return false;
        }

        bool Expand(Context& ctx, const fs::path& file, int depth, bool isRoot) {
            if (depth > ShaderSource::kMaxIncludeDepth) {
                ctx.Error = "includes aninhados demais (limite " + std::to_string(ShaderSource::kMaxIncludeDepth) + ") em " + file.generic_string();
                return false;
            }

            const std::string canonical = Canonical(file);
            for (const std::string& open : ctx.Stack) {
                if (open == canonical) {
                    std::string chain;
                    for (const std::string& s : ctx.Stack) chain += s + " -> ";
                    ctx.Error = "inclusao circular: " + chain + canonical;
                    return false;
                }
            }
            if (!isRoot && !ctx.Included.insert(canonical).second)
                return true; // ja incluido neste shader
            if (isRoot)
                ctx.Included.insert(canonical);

            const int fileIndex = (int)ctx.Files.size();
            ctx.Files.push_back(file);

            std::string text;
            if (!ReadFile(file, text)) {
                ctx.Error = "nao foi possivel abrir '" + file.generic_string() + "'";
                return false;
            }

            ctx.Stack.push_back(canonical);
            if (!isRoot)
                ctx.Out += "#line 1 " + std::to_string(fileIndex) + "\n";

            int lineNo = 0;
            size_t pos = 0;
            while (pos <= text.size()) {
                size_t nl = text.find('\n', pos);
                const bool lastChunk = (nl == std::string::npos);
                std::string_view line(text.data() + pos, (lastChunk ? text.size() : nl) - pos);
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);
                if (lastChunk && line.empty())
                    break; // arquivo terminado em '\n': nao ha linha extra
                lineNo++;

                std::string_view trimmed = Trim(line);
                std::string_view rest;

                if (StartsWithDirective(trimmed, "include", rest)) {
                    if (rest.size() < 2 || rest[0] != '"')
                        return Fail(ctx, Where(file, lineNo) + ": '#include' deve ser da forma #include \"arquivo\"");
                    size_t close = rest.find('"', 1);
                    if (close == std::string_view::npos)
                        return Fail(ctx, Where(file, lineNo) + ": '#include' sem aspas de fechamento");
                    std::string_view tail = Trim(rest.substr(close + 1));
                    if (!tail.empty() && tail.substr(0, 2) != "//")
                        return Fail(ctx, Where(file, lineNo) + ": texto inesperado depois do '#include'");

                    const fs::path name = fs::path(std::string(rest.substr(1, close - 1)));
                    fs::path resolved;
                    for (const fs::path& base : { file.parent_path(), ctx.RootDir }) {
                        std::error_code ec;
                        fs::path candidate = base / name;
                        if (fs::is_regular_file(candidate, ec)) { resolved = candidate; break; }
                    }
                    if (resolved.empty())
                        return Fail(ctx, Where(file, lineNo) + ": include '" + name.generic_string() + "' nao encontrado (procurado em '"
                            + file.parent_path().generic_string() + "' e '" + ctx.RootDir.generic_string() + "')");

                    if (!Expand(ctx, resolved, depth + 1, false)) {
                        ctx.Stack.pop_back();
                        return false;
                    }
                    ctx.Out += "#line " + std::to_string(lineNo + 1) + " " + std::to_string(fileIndex) + "\n";
                } else if (isRoot && !ctx.VersionSeen && StartsWithDirective(trimmed, "version", rest)) {
                    ctx.VersionSeen = true;
                    ctx.Out.append(line);
                    ctx.Out += "\n";
                    if (ctx.Defines && !ctx.Defines->empty()) {
                        for (const auto& [name, value] : *ctx.Defines)
                            ctx.Out += "#define " + name + " " + value + "\n";
                        ctx.Out += "#line " + std::to_string(lineNo + 1) + " " + std::to_string(fileIndex) + "\n";
                    }
                } else {
                    ctx.Out.append(line);
                    ctx.Out += "\n";
                }

                if (lastChunk)
                    break;
                pos = nl + 1;
            }

            ctx.Stack.pop_back();
            return true;
        }

    }

    ShaderSourceResult ShaderSource::Load(const fs::path& file, const ShaderDefines& defines) {
        Context ctx;
        ctx.RootDir = file.parent_path();
        ctx.Defines = &defines;

        ShaderSourceResult result;
        const bool ok = Expand(ctx, file, 0, true);

        if (ok && !defines.empty() && !ctx.VersionSeen)
            ctx.Error = "'" + file.generic_string() + "' nao tem '#version'; os defines nao tem onde ser injetados";

        result.Ok = ok && ctx.Error.empty();
        result.Error = ctx.Error;
        result.Files = std::move(ctx.Files);
        if (result.Ok)
            result.Source = std::move(ctx.Out);
        return result;
    }

}
