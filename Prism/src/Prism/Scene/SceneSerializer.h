#pragma once

// ============================================================================
// SceneSerializer.h
// Salva/carrega uma Scene em disco em formato BINARIO (mais eficiente;
// diferente do .prismproj, que e texto porque precisa ser legivel/diffavel
// no Git). Arquivos de cena ficam dentro de Project::GetMapDirectory() com
// extensao .prismmap.
//
// Formato do arquivo (little-endian, sem padding manual):
//   [4 bytes] magic  "PRSM"
//   [4 bytes] versao do formato (uint32_t) - ver kSceneFormatVersion no .cpp
//   [string]  nome da cena
//   [4 bytes] quantidade de entidades (uint32_t)
//   por entidade:
//     [string]    tag (nome de exibicao)
//     [Transform] Translation, Rotation, Scale (9 floats)
//     components opcionais, cada um precedido de uma flag de presenca e
//     serializado pelo ComponentRegistry (ver ComponentRegistration.cpp,
//     que e a fonte da verdade do layout de cada component)
//     [4 bytes]   indice do pai (int32_t, -1 = sem pai)
//
// Um arquivo de versao diferente de kSceneFormatVersion e recusado (nao
// ha migracao entre versoes). Ver docs/formatos-de-arquivo.md.
// ============================================================================

#include "../Core/Base.h"
#include <filesystem>
#include <cstdint>

namespace Prism {

    class Scene;

    class SceneSerializer {
    public:
        SceneSerializer(Ref<Scene> scene);

        // 'logSuccess' controla so a mensagem "Cena ... salva em: ..." de
        // sucesso (erros SEMPRE sao logados). Existe para ComputeFingerprint()
        // e outros usos internos que gravam num arquivo temporario sem que
        // isso apareca no Console como se o usuario tivesse salvo. O padrao
        // (true) preserva o comportamento de todos os chamadores existentes.
        bool Serialize(const std::filesystem::path& filepath, bool logSuccess = true);

        // "Impressao digital" do estado ATUAL da cena: hash do exato mesmo
        // conteudo binario que Serialize() gravaria em disco. Duas cenas com
        // o mesmo fingerprint serializam identicas byte a byte - logo,
        // comparar o fingerprint de agora com o de quando a cena foi salva
        // (ou carregada) diz se ha alteracoes nao salvas.
        //
        // Por que isto e melhor que marcar "dirty" a cada edicao na UI: usa
        // o MESMO caminho de serializacao que grava o arquivo. Qualquer
        // campo novo de qualquer component entra automaticamente, sem
        // ninguem precisar lembrar de "marcar dirty" - esquecer isso faria
        // o aviso falhar em silencio, o pior tipo de falha para um aviso
        // de "voce vai perder seu trabalho".
        //
        // Custo: serializa a cena inteira (~1 ms para milhares de
        // entidades). Chame sob demanda (ao tentar fechar/trocar de mapa),
        // NAO todo frame.
        //
        // Retorna 0 se nao conseguiu calcular (falha ao gravar o temporario).
        // Quem chama deve tratar 0 como "desconhecido" e, por seguranca,
        // assumir que HA alteracoes.
        uint64_t ComputeFingerprint();
        bool Deserialize(const std::filesystem::path& filepath);

        // Apos um Deserialize() bem sucedido, esta e a Scene carregada -
        // quem chamou deve usar este getter (nao o Ref<Scene> passado no
        // construtor, que permanece intocado) para pegar o resultado.
        Ref<Scene> GetScene() const { return m_Scene; }

    private:
        Ref<Scene> m_Scene;
    };

}
