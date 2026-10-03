#pragma once

// ============================================================================
// ShaderSource.h
// Carrega um shader GLSL de ARQUIVO e o prepara para o compilador:
//
//   * '#include "caminho.glsl"' - expandido aqui (GLSL nao tem include
//     nativo). Procurado ao lado do arquivo que inclui e depois na pasta do
//     arquivo PRINCIPAL. Cada arquivo entra no maximo UMA vez por shader
//     (#pragma once implicito); inclusao circular e erro. O include e
//     textual: nao enxerga '#if'/'#ifdef' nem comentarios de bloco.
//
//   * defines - injetados logo apos o '#version' do arquivo principal, para
//     o C++ ser a fonte unica de constantes compartilhadas (MAX_LIGHTS).
//
//   * '#line N I' em cada fronteira de include, para que as mensagens de
//     erro do driver ("0:23") apontem para a linha certa do arquivo certo.
//     'I' e o indice em ShaderSourceResult::Files.
//
// Nao depende de OpenGL: compila e e testado isoladamente (tests/).
// ============================================================================

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Prism {

    using ShaderDefines = std::vector<std::pair<std::string, std::string>>;

    struct ShaderSourceResult {
        bool Ok = false;
        std::string Error;   // preenchido quando Ok == false
        std::string Source;  // GLSL pronto para glShaderSource

        // Todos os arquivos lidos; Files[0] e o principal. O indice e o
        // "source-string-number" das mensagens do compilador. Mesmo com
        // Ok == false lista os arquivos tentados (o hot reload os observa).
        std::vector<std::filesystem::path> Files;
    };

    class ShaderSource {
    public:
        static ShaderSourceResult Load(const std::filesystem::path& file, const ShaderDefines& defines = {});

        static constexpr int kMaxIncludeDepth = 16;
    };

}
