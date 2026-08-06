#include "ProjectSerializer.h"
#include "../Core/Log.h"
#include <fstream>
#include <sstream>

namespace Prism {

    ProjectSerializer::ProjectSerializer(Ref<Project> project)
        : m_Project(project) {}

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath) {
        const auto& config = m_Project->GetConfig();

        std::ofstream out(filepath);
        if (!out.is_open()) {
            PRISM_CORE_ERROR("Nao foi possivel criar o arquivo de projeto: ", filepath.string());
            return false;
        }

        out << "# Prism Project File - nao edite manualmente a menos que saiba o que esta fazendo\n";
        out << "Name=" << config.Name << "\n";
        out << "AssetDirectory=" << config.AssetDirectory.generic_string() << "\n";
        out << "ScriptDirectory=" << config.ScriptDirectory.generic_string() << "\n";
        out << "MapDirectory=" << config.MapDirectory.generic_string() << "\n";
        out << "StartMap=" << config.StartMap.generic_string() << "\n";

        return true;
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath) {
        std::ifstream in(filepath);
        if (!in.is_open()) {
            PRISM_CORE_ERROR("Nao foi possivel abrir o arquivo de projeto: ", filepath.string());
            return false;
        }

        ProjectConfig config;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#')
                continue;

            auto eqPos = line.find('=');
            if (eqPos == std::string::npos)
                continue;

            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);

            if (key == "Name") config.Name = value;
            else if (key == "AssetDirectory") config.AssetDirectory = value;
            else if (key == "ScriptDirectory") config.ScriptDirectory = value;
            else if (key == "MapDirectory") config.MapDirectory = value;
            else if (key == "StartMap") config.StartMap = value;
        }

        m_Project->m_Config = config;
        return true;
    }

}
