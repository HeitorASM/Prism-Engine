#pragma once

// ============================================================================
// SceneSerializer.h
// Salva/carrega uma Scene em disco em formato BINARIO (decisao ja tomada no
// guia do prototipo, pela eficiencia - diferente do .prismproj, que e texto
// porque precisa ser legivel/diffavel no Git). Arquivos de cena ficam dentro
// de Project::GetMapDirectory() com extensao .prismmap.
//
// Formato do arquivo (little-endian, sem padding manual - ver .cpp para os
// detalhes campo a campo):
//   [4 bytes] magic  "PRSM"
//   [4 bytes] versao do formato (uint32_t) - ver kSceneFormatVersion no .cpp
//   [string]  nome da cena
//   [4 bytes] quantidade de entidades (uint32_t)
//   por entidade:
//     [string] tag (nome de exibicao)
//     [Transform] Translation, Rotation, Scale (9 floats)
//     [1 byte]  flag: tem MeshRendererComponent?      se sim: PrimitiveMesh (u32) + cor (3 floats)
//     [1 byte]  flag: tem LightComponent?             se sim: LightType (u32) + cor (3 floats) + Intensity + Range + SpotAngle
//     [1 byte]  flag: tem ColliderComponent?          se sim: ColliderShape (u32) + Size (3 floats) + IsTrigger (1 byte)
//     [1 byte]  flag: tem RigidBodyComponent?         se sim: BodyType (u32) + Mass + UseGravity (1 byte) + CCD (1 byte)
//     [1 byte]  flag: tem ScriptComponent?             se sim: ScriptPath (string)
//
// O numero de versao existe desde ja para que, quando novos components
// forem adicionados, Deserialize() consiga detectar um arquivo de versao
// antiga e decidir como lidar com ele (por ora, so versoes iguais a
// kSceneFormatVersion sao aceitas - migração entre versoes fica para quando
// o formato realmente mudar e valer a pena preservar cenas antigas).
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
