#include "ModelImport.h"
#include <Prism/Assets/ModelLoader.h>
#include <system_error>

namespace PrismEditor::ModelImport {

    static Prism::MaterialComponent ToMaterialComponent(const Prism::ImportedMaterial& imported, const std::filesystem::path& projectDirectory) {
        // MaterialComponent guarda caminhos RELATIVOS a pasta do projeto
        // (ver Renderer::GetOrLoadTexture) - mesma conversao do drop de
        // imagem no painel Material.
        auto toRelative = [&](const std::filesystem::path& absolute) -> std::string {
            if (absolute.empty())
                return {};
            std::error_code ec;
            std::filesystem::path relative = std::filesystem::relative(absolute, projectDirectory, ec);
            if (ec || relative.empty())
                return absolute.generic_string();
            return relative.generic_string();
        };

        Prism::MaterialComponent material;
        material.AlbedoPath = toRelative(imported.AlbedoPath);
        material.NormalPath = toRelative(imported.NormalPath);
        material.RoughnessMetallicPath = toRelative(imported.RoughnessMetallicPath);
        material.AlbedoTint = imported.AlbedoTint;
        material.RoughnessFactor = imported.Roughness;
        material.MetallicFactor = imported.Metallic;
        return material;
    }

    PreparedModel Prepare(const std::filesystem::path& absoluteModelPath) {
        PreparedModel result;

        auto project = Prism::Project::GetActive();
        if (!project) {
            result.Error = "Nenhum projeto aberto.";
            return result;
        }

        auto& registry = project->GetAssetRegistry();
        std::string relative = registry.ToRelative(absoluteModelPath);
        if (relative.empty()) {
            result.Error = "O modelo precisa estar dentro da pasta de Assets do projeto: " + absoluteModelPath.string();
            return result;
        }
        Prism::AssetID id = registry.IdForPath(relative);
        if (!id.IsValid()) {
            // Modelo copiado para a pasta por fora do editor, antes de um Refresh().
            registry.Refresh();
            id = registry.IdForPath(relative);
        }
        if (!id.IsValid()) {
            result.Error = "Nao foi possivel obter o AssetID do modelo: " + absoluteModelPath.string();
            return result;
        }

        Prism::ModelImportOptions options;
        options.ExtractEmbeddedTextures = true;
        options.TextureOutputDir = absoluteModelPath.parent_path() / (absoluteModelPath.stem().string() + "_textures");

        Prism::ModelImportResult imported = Prism::ModelLoader::Load(absoluteModelPath, options);
        if (!imported.Success) {
            result.Error = imported.Error;
            return result;
        }

        for (int i = 0; i < (int)imported.Submeshes.size(); i++) {
            Part part;
            part.Name = imported.Submeshes[i].Name;
            part.SubmeshIndex = i;
            part.Material = ToMaterialComponent(imported.Submeshes[i].Material, project->GetProjectDirectory());
            result.Parts.push_back(std::move(part));
        }

        // Texturas recem-extraidas viram assets (com .meta) na proxima varredura.
        std::error_code ec;
        if (std::filesystem::exists(options.TextureOutputDir, ec))
            registry.Refresh();

        result.Success = true;
        result.Asset = id;
        result.Name = absoluteModelPath.stem().string();
        return result;
    }

}
