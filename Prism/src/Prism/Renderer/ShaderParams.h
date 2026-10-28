#pragma once

// ============================================================================
// ShaderParams.h
// Le os PARAMETROS de um shader de material: as declaracoes 'uniform' do
// arquivo .glsl do projeto, no estilo da Godot.
//
//     uniform float u_Brilho = 1.0;          // range(0, 4)
//     uniform vec3  u_CorFogo = vec3(1.0, 0.3, 0.1);   // color
//     uniform bool  u_Pulsar = true;
//
// Tipos: float, int, bool, vec2, vec3, vec4. Comentario na mesma linha:
//   'color'        vec3/vec4 vira seletor de cor (valor em sRGB no Inspetor)
//   'range(a, b)'  limites do campo e do valor (float/int/vec*)
// Valor padrao: o inicializador ('= 1.0', '= vec3(...)', '= true'); sem ele, zero.
// Nomes: o proprio nome do uniform (precisa ser unico; 'u_' e opcional).
// Sampler e tipos nao listados sao ignorados (fica a cargo do GLSL).
//
// So olha o arquivo PRINCIPAL (nao expande '#include') e ignora comentarios.
// '#if' nao tem tratamento especial: declare os parametros no nivel de topo.
// Nao depende de OpenGL: testado isoladamente.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace Prism {

    enum class ShaderParamType { Float = 0, Int, Bool, Vec2, Vec3, Vec4 };

    struct ShaderParamDecl {
        std::string Name;
        ShaderParamType Type = ShaderParamType::Float;
        float Default[4] = { 0, 0, 0, 0 };
        bool IsColor = false;
        bool HasRange = false;
        float Min = 0.0f, Max = 1.0f;

        int Components() const {
            switch (Type) {
                case ShaderParamType::Vec2: return 2;
                case ShaderParamType::Vec3: return 3;
                case ShaderParamType::Vec4: return 4;
                default: return 1;
            }
        }
    };

    namespace ShaderParams {

        inline std::string Trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace((unsigned char)s[a])) a++;
            while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
            return s.substr(a, b - a);
        }

        // Numeros separados por virgula ("1.0, 0.3, 0.1"); aceita sufixo 'f'. Falso se algo nao for numero.
        inline bool ParseNumbers(const std::string& text, std::vector<float>& out) {
            out.clear();
            size_t pos = 0;
            while (pos <= text.size()) {
                size_t comma = text.find(',', pos);
                std::string item = Trim(text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
                if (item.empty()) return false;
                if (item.back() == 'f' || item.back() == 'F') item.pop_back();
                char* end = nullptr;
                const float value = std::strtof(item.c_str(), &end);
                if (item.empty() || end == item.c_str() || *end != '\0' || !std::isfinite(value)) return false;
                out.push_back(value);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            return true;
        }

        inline void StripBlockComments(std::string& text) {
            size_t pos = 0;
            while ((pos = text.find("/*", pos)) != std::string::npos) {
                size_t end = text.find("*/", pos + 2);
                if (end == std::string::npos) end = text.size() - 2;
                for (size_t i = pos; i < end + 2 && i < text.size(); i++)
                    if (text[i] != '\n') text[i] = ' ';
                pos = end + 2;
            }
        }

        inline bool StartsWith(const std::string& s, size_t at, const char* word) {
            const size_t n = std::char_traits<char>::length(word);
            return s.compare(at, n, word) == 0;
        }

        inline std::vector<ShaderParamDecl> Parse(std::string text) {
            std::vector<ShaderParamDecl> result;
            StripBlockComments(text);

            size_t lineStart = 0;
            while (lineStart <= text.size()) {
                size_t lineEnd = text.find('\n', lineStart);
                std::string line = text.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
                lineStart = (lineEnd == std::string::npos) ? text.size() + 1 : lineEnd + 1;

                std::string comment;
                const size_t slashes = line.find("//");
                if (slashes != std::string::npos) {
                    comment = line.substr(slashes + 2);
                    line.erase(slashes);
                }
                line = Trim(line);
                if (!StartsWith(line, 0, "uniform") || line.size() < 9 || !std::isspace((unsigned char)line[7]))
                    continue;
                size_t semicolon = line.find(';');
                if (semicolon == std::string::npos) continue;
                std::string body = Trim(line.substr(7, semicolon - 7)); // "float u_X = 1.0"

                size_t space = body.find_first_of(" \t");
                if (space == std::string::npos) continue;
                const std::string typeName = body.substr(0, space);
                std::string rest = Trim(body.substr(space));

                ShaderParamDecl decl;
                if (typeName == "float") decl.Type = ShaderParamType::Float;
                else if (typeName == "int") decl.Type = ShaderParamType::Int;
                else if (typeName == "bool") decl.Type = ShaderParamType::Bool;
                else if (typeName == "vec2") decl.Type = ShaderParamType::Vec2;
                else if (typeName == "vec3") decl.Type = ShaderParamType::Vec3;
                else if (typeName == "vec4") decl.Type = ShaderParamType::Vec4;
                else continue;

                std::string initializer;
                const size_t equals = rest.find('=');
                std::string name = Trim(equals == std::string::npos ? rest : rest.substr(0, equals));
                if (equals != std::string::npos) initializer = Trim(rest.substr(equals + 1));
                if (name.empty() || name.find_first_of("[] \t") != std::string::npos || std::isdigit((unsigned char)name[0]))
                    continue; // arrays e nomes invalidos nao viram parametro
                if (std::any_of(result.begin(), result.end(), [&](const ShaderParamDecl& d) { return d.Name == name; }))
                    continue;
                decl.Name = name;

                // Inicializador
                if (!initializer.empty()) {
                    std::vector<float> numbers;
                    bool ok = false;
                    if (decl.Type == ShaderParamType::Bool) {
                        if (initializer == "true") { decl.Default[0] = 1.0f; ok = true; }
                        else if (initializer == "false") { ok = true; }
                    } else if (decl.Components() == 1) {
                        ok = ParseNumbers(initializer, numbers) && numbers.size() == 1;
                    } else {
                        const std::string ctor = typeName + "(";
                        if (StartsWith(initializer, 0, ctor.c_str()) && initializer.back() == ')') {
                            ok = ParseNumbers(initializer.substr(ctor.size(), initializer.size() - ctor.size() - 1), numbers);
                            if (ok && numbers.size() == 1) numbers.assign(decl.Components(), numbers[0]); // vec3(0.5)
                            ok = ok && (int)numbers.size() == decl.Components();
                        }
                    }
                    if (ok && decl.Type != ShaderParamType::Bool)
                        for (size_t i = 0; i < numbers.size() && i < 4; i++) decl.Default[i] = numbers[i];
                }

                // Dicas no comentario
                if (comment.find("color") != std::string::npos
                    && (decl.Type == ShaderParamType::Vec3 || decl.Type == ShaderParamType::Vec4))
                    decl.IsColor = true;
                const size_t range = comment.find("range(");
                if (range != std::string::npos) {
                    const size_t close = comment.find(')', range);
                    std::vector<float> limits;
                    if (close != std::string::npos && ParseNumbers(comment.substr(range + 6, close - range - 6), limits)
                        && limits.size() == 2 && limits[0] < limits[1]) {
                        decl.HasRange = true; decl.Min = limits[0]; decl.Max = limits[1];
                    }
                }
                if (decl.Type == ShaderParamType::Int) // int: valores inteiros
                    decl.Default[0] = std::round(decl.Default[0]);
                if (decl.Type == ShaderParamType::Bool) { decl.HasRange = false; decl.IsColor = false; }
                if (decl.IsColor) { decl.HasRange = true; decl.Min = 0.0f; decl.Max = 1.0f; }

                result.push_back(decl);
                if (result.size() >= 64) break;
            }
            return result;
        }

        // Limita 'value' ao que o parametro aceita (faixa, inteiro, bool 0/1, NaN -> padrao).
        inline void Clamp(const ShaderParamDecl& decl, float value[4]) {
            const int n = decl.Components();
            for (int i = 0; i < n; i++) {
                float v = std::isfinite(value[i]) ? value[i] : decl.Default[i];
                if (decl.HasRange) v = std::min(std::max(v, decl.Min), decl.Max);
                else v = std::min(std::max(v, -1.0e6f), 1.0e6f);
                if (decl.Type == ShaderParamType::Int) v = std::round(v);
                if (decl.Type == ShaderParamType::Bool) v = v != 0.0f ? 1.0f : 0.0f;
                value[i] = v;
            }
        }

    }

}
