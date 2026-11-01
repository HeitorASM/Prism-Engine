#pragma once

// ============================================================================
// ExtensionLoader.h
// Descobre, valida e carrega extensoes (dll/so + extension.ini). Lado da ENGINE; o
// lado da extensao e ExtensionAPI.h.
//
// Onde procura (cada subpasta com um extension.ini e uma extensao):
//   <pasta do executavel>/Extensions/<Nome>/     globais (editor e jogo exportado)
//   <pasta do projeto>/Extensions/<Nome>/        do projeto
// O binario e <pasta>/<library>.(dll|so|dylib) ou <pasta>/Binaries/<library>.*
//
// Uma extensao com problema (manifesto invalido, API diferente, dependencia faltando,
// Init falhou/lancou excecao) NAO derruba nem impede as demais: fica com estado Failed
// e o motivo, visivel em GetExtensions() e no log.
//
// IMPORTANTE: extensoes sao codigo nativo com acesso total ao processo. So carregue
// extensoes em que voce confia (as de um projeto de terceiros rodam ao abri-lo).
// ============================================================================

#include "ExtensionManifest.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Prism {

    class Project;

    enum class ExtensionState {
        Loaded,
        Failed,   // erro (ver Message)
        Skipped   // desligada no manifesto ou para o outro alvo (editor/runtime)
    };

    struct ExtensionInfo {
        ExtensionManifest Manifest;
        std::filesystem::path Directory;
        std::filesystem::path Library;   // vazio se nao chegou a achar o binario
        ExtensionState State = ExtensionState::Failed;
        std::string Message;
        bool FromProject = false;
    };

    class ExtensionLoader {
    public:
        // Globais, ao lado do executavel. Idempotente: extensoes ja conhecidas (por nome) sao ignoradas.
        static void LoadInstalled(bool isEditor);

        // Do projeto. Chame ANTES de abrir qualquer mapa do projeto (components so existem se ja
        // estiverem registrados na leitura). Idempotente por nome.
        static void LoadProject(const Project& project, bool isEditor);

        // Nucleo (usado pelos dois acima e pelos testes). 'projectDirectory' pode ser vazio.
        static void LoadFromDirectory(const std::filesystem::path& root, bool isEditor, bool fromProject,
                                      const std::filesystem::path& projectDirectory);

        // Chama o Shutdown de cada extensao (ordem inversa da carga) e remove os sistemas dela.
        // NAO descarrega as bibliotecas (ver comentario no .cpp). Idempotente.
        static void Shutdown();

        static const std::vector<ExtensionInfo>& GetExtensions();
        static bool IsLoaded(const std::string& name);
    };

} // namespace Prism
