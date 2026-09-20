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

namespace Prism {

    class Scene;

    class SceneSerializer {
    public:
        SceneSerializer(Ref<Scene> scene);

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        // Apos um Deserialize() bem sucedido, esta e a Scene carregada -
        // quem chamou deve usar este getter (nao o Ref<Scene> passado no
        // construtor, que permanece intocado) para pegar o resultado.
        Ref<Scene> GetScene() const { return m_Scene; }

    private:
        Ref<Scene> m_Scene;
    };

}
