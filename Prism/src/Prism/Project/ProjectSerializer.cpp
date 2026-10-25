#include "ProjectSerializer.h"
#include "../Core/Log.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>

namespace Prism {

    // Grava 'text' em 'filepath' de forma atomica: escreve num arquivo
    // temporario ao lado e so depois troca pelo definitivo. Um crash ou
    // falta de disco no meio do save nunca deixa o .prismproj pela metade
    // (o projeto e regravado ao salvar o mapa inicial). Mesmo padrao do
    // .meta (ver Assets/AssetMeta.cpp).
    static bool WriteFileAtomic(const std::filesystem::path& filepath, const std::string& text) {
        std::filesystem::path temp = filepath;
        temp += ".tmp";
        {
            std::ofstream out(temp);
            if (!out.is_open())
                return false;
            out << text;
            out.flush();
            if (!out) {
                out.close();
                std::error_code ignored;
                std::filesystem::remove(temp, ignored);
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::rename(temp, filepath, ec);
        if (ec) {
            // Alguns sistemas de arquivos recusam rename sobre arquivo existente.
            std::filesystem::copy_file(temp, filepath, std::filesystem::copy_options::overwrite_existing, ec);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return !ec;
        }
        return true;
    }

    ProjectSerializer::ProjectSerializer(Ref<Project> project)
        : m_Project(project) {}

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath) {
        const auto& config = m_Project->GetConfig();

        std::ostringstream out;
        out << "# Prism Project File - nao edite manualmente a menos que saiba o que esta fazendo\n";
        out << "Name=" << config.Name << "\n";
        out << "AssetDirectory=" << config.AssetDirectory.generic_string() << "\n";
        out << "ScriptDirectory=" << config.ScriptDirectory.generic_string() << "\n";
        out << "MapDirectory=" << config.MapDirectory.generic_string() << "\n";
        out << "StartMap=" << config.StartMap.generic_string() << "\n";
        out << "EngineVersion=" << config.EngineVersion << "\n";
        for (int i = 0; i < 32; i++)
            if (m_Project->HasCustomLayerName(i))
                out << "Layer" << i << "=" << m_Project->GetLayerName(i) << "\n";

        if (!WriteFileAtomic(filepath, out.str())) {
            PRISM_CORE_ERROR("Nao foi possivel gravar o arquivo de projeto: ", filepath.string());
            return false;
        }
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
            if (!line.empty() && line.back() == '\r')
                line.pop_back(); // arquivo editado com fim de linha do Windows num sistema POSIX
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
            else if (key == "EngineVersion") config.EngineVersion = value;
            else if (key.size() > 5 && key.compare(0, 5, "Layer") == 0 && key.find_first_not_of("0123456789", 5) == std::string::npos && key.size() <= 7) {
                int layerIndex = std::stoi(key.substr(5));
                if (layerIndex >= 0 && layerIndex < 32)
                    m_Project->m_LayerNames[(size_t)layerIndex] = value;
            }
            // Chaves de render antigas (Exposure, Ambient, Env*, Sky*) sao
            // ignoradas: o visual agora vive na cena (EnvironmentComponent)
            // e na camera (CameraComponent). Projetos antigos abrem normal.
        }

        m_Project->m_Config = config;
        return true;
    }

}
