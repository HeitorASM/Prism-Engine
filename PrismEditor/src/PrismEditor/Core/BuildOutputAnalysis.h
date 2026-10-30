#pragma once

// ============================================================================
// BuildOutputAnalysis.h
// Le a saida do compilador/linker dos scripts C++ (so std, testavel) e reconhece
// os erros que NAO sao do codigo do usuario:
//   - arquivo travado por outro processo (LNK1104/LNK1168/LNK1201, "Permission
//     denied", "Text file busy"...): costuma sumir ao tentar de novo ou ao
//     soltar o arquivo (depurador, antivirus, indexador);
//   - simbolo nao resolvido (LNK2019/LNK2001, "undefined reference"): o modulo
//     usou algo da engine que nao faz parte da API nativa.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace PrismEditor::BuildAnalysis {

    inline std::string Lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return text;
    }

    inline bool Contains(const std::string& haystack, const char* needle) {
        return haystack.find(needle) != std::string::npos;
    }

    // Erro de arquivo travado/inacessivel (do linker ou do sistema).
    inline bool IsFileLockError(const std::string& line) {
        const std::string l = Lower(line);
        // MSVC: LNK1104 (nao abre arquivo), LNK1168 (nao abre para escrita), LNK1201 (erro ao escrever o PDB).
        if (Contains(l, "lnk1104") || Contains(l, "lnk1168") || Contains(l, "lnk1201") || Contains(l, "lnk1136"))
            return true;
        // C1041: nao abre o PDB do compilador (mspdbsrv travado).
        if (Contains(l, "c1041") || Contains(l, "c1090"))
            return true;
        if (Contains(l, "cannot open output file") || Contains(l, "cannot open file") || Contains(l, "cannot open input file"))
            return Contains(l, "permission denied") || Contains(l, ".dll") || Contains(l, ".pdb") || Contains(l, ".lib") || Contains(l, ".so");
        if (Contains(l, "permission denied") || Contains(l, "text file busy") || Contains(l, "resource busy"))
            return true;
        if (Contains(l, "being used by another process") || Contains(l, "acesso negado") || Contains(l, "usado por outro processo"))
            return true;
        return false;
    }

    // Simbolo nao resolvido no link do modulo.
    inline bool IsUnresolvedSymbolError(const std::string& line) {
        const std::string l = Lower(line);
        return Contains(l, "lnk2019") || Contains(l, "lnk2001") || Contains(l, "lnk2005")
            || Contains(l, "undefined reference") || Contains(l, "undefined symbols for architecture")
            || Contains(l, "unresolved external");
    }

    enum class Failure { None, FileLocked, UnresolvedSymbol };

    // Classifica a falha de uma compilacao pelas suas linhas. Erros de codigo do usuario (C2xxx,
    // "error:" do gcc) NAO entram aqui: sao reportados como estao.
    inline Failure Classify(const std::vector<std::string>& lines) {
        bool locked = false, unresolved = false;
        for (const std::string& line : lines) {
            locked = locked || IsFileLockError(line);
            unresolved = unresolved || IsUnresolvedSymbolError(line);
        }
        if (unresolved) return Failure::UnresolvedSymbol; // o erro "de verdade" tem prioridade sobre um travamento
        if (locked) return Failure::FileLocked;
        return Failure::None;
    }

    inline std::string Hint(Failure failure) {
        switch (failure) {
            case Failure::FileLocked:
                return "Dica: o linker nao conseguiu gravar um arquivo do modulo (outro processo o esta usando). "
                       "Causas comuns: depurador anexado ao editor (trava o .pdb do modulo carregado), antivirus ou indexador "
                       "do Windows logo depois da gravacao. Ja foi usado um .pdb novo e uma nova tentativa automatica; "
                       "se persistir, exclua a pasta Binaries/Cache do projeto da varredura do antivirus.";
            case Failure::UnresolvedSymbol:
                return "Dica: erro de LINK por simbolo nao resolvido. O modulo nao linka com a engine: use so a API de "
                       "ScriptableEntity (docs/scripting-cpp.md). Funcoes de classes da engine chamadas direto nao existem "
                       "no modulo; se faltar algo, peca para entrar na NativeEngineAPI.";
            case Failure::None:
                break;
        }
        return {};
    }

}
