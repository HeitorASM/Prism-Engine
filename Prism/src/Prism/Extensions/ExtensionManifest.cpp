#include "ExtensionManifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>

namespace Prism {

    namespace {
        std::string Trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace((unsigned char)s[a])) ++a;
            while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
            return s.substr(a, b - a);
        }
        std::string Lower(std::string s) {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }
        std::string Unquote(const std::string& s) {
            if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
                return s.substr(1, s.size() - 2);
            return s;
        }
        bool ParseBool(const std::string& v, bool& out) {
            const std::string l = Lower(v);
            if (l == "true" || l == "1" || l == "yes" || l == "sim") { out = true; return true; }
            if (l == "false" || l == "0" || l == "no" || l == "nao") { out = false; return true; }
            return false;
        }
        bool IsValidLibraryName(const std::string& s) {
            if (s.empty() || s.size() > 128) return false;
            if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos) return false;
            if (s.find("..") != std::string::npos) return false;
            return true;
        }
    }

    bool IsValidExtensionName(const std::string& name) {
        if (name.empty() || name.size() > 64) return false;
        for (char c : name) {
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-'))
                return false;
        }
        return true;
    }

    std::string SetManifestValue(const std::string& text, const std::string& key, const std::string& value) {
        const std::string wantedKey = Lower(Trim(key));
        const bool crlf = text.find("\r\n") != std::string::npos;
        const std::string newline = crlf ? "\r\n" : "\n";
        const std::string replacement = wantedKey + " = " + value;

        std::string result;
        bool replaced = false;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            const bool hasNewline = end != std::string::npos;
            if (!hasNewline) end = text.size();
            std::string line = text.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            bool isTarget = false;
            if (!replaced) {
                const std::string trimmed = Trim(line);
                if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';') {
                    const size_t eq = trimmed.find('=');
                    if (eq != std::string::npos && Lower(Trim(trimmed.substr(0, eq))) == wantedKey)
                        isTarget = true;
                }
            }
            if (isTarget) {
                result += replacement;
                replaced = true;
            }
            else {
                result += line;
            }
            if (hasNewline) result += newline;
            pos = hasNewline ? end + 1 : end;
        }
        if (!replaced) {
            if (!result.empty() && result.back() != '\n') result += newline;
            result += replacement + newline;
        }
        return result;
    }

    const char* ExtensionTargetName(ExtensionTarget target) {
        switch (target) {
            case ExtensionTarget::Editor: return "editor";
            case ExtensionTarget::Runtime: return "runtime";
            default: return "both";
        }
    }

    ManifestParseResult ParseExtensionManifest(const std::string& rawText) {
        ManifestParseResult result;
        std::string text = rawText;
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
            text.erase(0, 3); // BOM UTF-8

        ExtensionManifest& m = result.Manifest;
        bool hasName = false, hasApi = false;
        size_t lineNumber = 0;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            std::string line = Trim(text.substr(pos, end - pos));
            pos = end + 1;
            ++lineNumber;
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                result.Error = "linha " + std::to_string(lineNumber) + ": esperado 'chave = valor'.";
                return result;
            }
            const std::string key = Lower(Trim(line.substr(0, eq)));
            const std::string value = Unquote(Trim(line.substr(eq + 1)));

            if (key == "name") {
                if (!IsValidExtensionName(value)) {
                    result.Error = "'name' invalido (use 1 a 64 caracteres de letras, numeros, _ . -).";
                    return result;
                }
                m.Name = value; hasName = true;
            } else if (key == "api") {
                char* endPtr = nullptr;
                const long v = std::strtol(value.c_str(), &endPtr, 10);
                if (value.empty() || *endPtr != '\0' || v <= 0 || v > 100000) {
                    result.Error = "'api' precisa ser um inteiro positivo.";
                    return result;
                }
                m.ApiVersion = (uint32_t)v; hasApi = true;
            } else if (key == "version") {
                m.Version = value;
            } else if (key == "description") {
                m.Description = value;
            } else if (key == "author") {
                m.Author = value;
            } else if (key == "library") {
                if (!IsValidLibraryName(value)) {
                    result.Error = "'library' invalido (nome sem pasta, sem '..').";
                    return result;
                }
                m.Library = value;
            } else if (key == "target") {
                const std::string l = Lower(value);
                if (l == "both") m.Target = ExtensionTarget::Both;
                else if (l == "editor") m.Target = ExtensionTarget::Editor;
                else if (l == "runtime") m.Target = ExtensionTarget::Runtime;
                else {
                    result.Error = "'target' deve ser both, editor ou runtime.";
                    return result;
                }
            } else if (key == "depends") {
                m.Dependencies.clear();
                size_t start = 0;
                while (start <= value.size()) {
                    size_t comma = value.find(',', start);
                    if (comma == std::string::npos) comma = value.size();
                    const std::string dep = Trim(value.substr(start, comma - start));
                    start = comma + 1;
                    if (dep.empty()) continue;
                    if (!IsValidExtensionName(dep)) {
                        result.Error = "dependencia invalida: '" + dep + "'.";
                        return result;
                    }
                    if (std::find(m.Dependencies.begin(), m.Dependencies.end(), dep) == m.Dependencies.end())
                        m.Dependencies.push_back(dep);
                }
            } else if (key == "enabled") {
                if (!ParseBool(value, m.Enabled)) {
                    result.Error = "'enabled' deve ser true ou false.";
                    return result;
                }
            } else {
                result.Warnings.push_back("chave desconhecida ignorada: '" + key + "'.");
            }
        }

        if (!hasName) { result.Error = "falta 'name'."; return result; }
        if (!hasApi) { result.Error = "falta 'api'."; return result; }
        if (m.Library.empty()) m.Library = m.Name;
        if (std::find(m.Dependencies.begin(), m.Dependencies.end(), m.Name) != m.Dependencies.end()) {
            result.Error = "a extensao nao pode depender de si mesma.";
            return result;
        }
        result.Ok = true;
        return result;
    }

    std::vector<std::string> ResolveLoadOrder(const std::vector<ExtensionManifest>& manifests,
                                              const std::set<std::string>& alreadyLoaded,
                                              std::map<std::string, std::string>& failures) {
        std::map<std::string, const ExtensionManifest*> byName;
        for (const ExtensionManifest& m : manifests)
            byName.emplace(m.Name, &m);

        std::vector<std::string> order;
        std::set<std::string> visiting, done;
        std::map<std::string, bool> ok;

        std::function<bool(const std::string&)> visit = [&](const std::string& name) -> bool {
            if (done.count(name)) return ok[name];
            if (visiting.count(name)) {
                failures[name] = "dependencia ciclica.";
                return false;
            }
            visiting.insert(name);
            bool good = true;
            std::string why;
            for (const std::string& dep : byName[name]->Dependencies) {
                if (alreadyLoaded.count(dep)) continue;
                if (!byName.count(dep)) {
                    good = false;
                    why = "depende de '" + dep + "', que nao foi encontrada.";
                    continue;
                }
                if (!visit(dep)) {
                    good = false;
                    why = "a dependencia '" + dep + "' nao pode ser carregada.";
                }
            }
            visiting.erase(name);
            done.insert(name);
            ok[name] = good;
            if (good) order.push_back(name);
            else if (!failures.count(name)) failures[name] = why;
            return good;
        };

        for (const ExtensionManifest& m : manifests)
            visit(m.Name);
        return order;
    }

} // namespace Prism
