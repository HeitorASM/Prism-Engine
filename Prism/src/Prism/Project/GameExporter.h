#pragma once

// ============================================================================
// GameExporter.h
// Empacota um projeto Prism como um jogo distribuivel: uma pasta com o
// PrismRuntime (renomeado para o nome do jogo), shaders, fontes, DLLs e o
// projeto em "Game/". O PrismRuntime ja procura o projeto em "Game/" ao lado
// do executavel quando abre sem argumentos (ver RuntimeLayer.cpp).
//
//   <Saida>/
//     MeuJogo.exe            PrismRuntime renomeado
//     assimp-*.dll, ...      o que mais o Runtime precisa
//     shaders/  fonts/
//     Game/                  o projeto (Assets, Maps, Scripts, Binaries/NativeScripts.*)
//     .prism-export          manifesto: lista o que ESTA exportacao criou
//
// O que NAO vai: Source/ (codigo dos scripts C++), Cache/, .git, builds, backups.
//
// So usa std::filesystem (sem OpenGL, GLFW nem EnTT): roda em thread de fundo e
// e testado isoladamente (tests/ExportTests.cpp).
// ============================================================================

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Prism {

    struct GameExportOptions {
        // Raiz do projeto (a pasta do .prismproj) e nome do .prismproj.
        std::filesystem::path ProjectDirectory;
        // Mapa inicial (ProjectConfig::StartMap, relativo a pasta de mapas). Vazio = erro:
        // o jogo exportado nao saberia o que abrir.
        std::filesystem::path StartMap;
        std::filesystem::path MapDirectory = "Maps"; // relativo a ProjectDirectory

        // Pasta com o PrismRuntime ja compilado (e as DLLs/shaders/fonts ao lado dele).
        std::filesystem::path RuntimeDirectory;

        // Pasta de destino (criada se faltar). Tem que estar vazia, ou ser uma exportacao
        // anterior (tem .prism-export) - nunca apaga o que a engine nao criou.
        std::filesystem::path OutputDirectory;

        // Nome do executavel (sem extensao). Caracteres invalidos viram '_' (ver SanitizeGameName).
        std::string GameName = "Game";

        // Versao da engine, gravada no manifesto (informativo).
        std::string EngineVersion;
    };

    struct GameExportResult {
        bool Success = false;
        std::string Error;                  // preenchido quando Success == false
        std::vector<std::string> Warnings;  // nao impedem a exportacao
        std::filesystem::path Executable;   // o .exe/binario gerado
        uint32_t FilesCopied = 0;
        uint64_t BytesCopied = 0;
    };

    // Recebe uma linha de progresso por vez. Chamada na MESMA thread que roda ExportGame.
    using GameExportLog = std::function<void(const std::string&)>;

    // Nome de arquivo seguro para o executavel: troca \ / : * ? " < > | e controles por '_',
    // tira espacos/pontos nas pontas, limita a 64 bytes (sem cortar um caractere UTF-8 ao
    // meio). Vazio vira "Game". Nomes reservados do Windows (CON, NUL, COM1...) ganham '_'.
    std::string SanitizeGameName(const std::string& name);

    // Faz a exportacao. Nunca lanca: erros de disco viram Result.Error.
    GameExportResult ExportGame(const GameExportOptions& options, const GameExportLog& log = {});

}
