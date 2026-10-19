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

    // --- Leitura/escrita de floats no .prismproj ---------------------------
    //
    // Sempre com o locale "C" (classic), NUNCA o do sistema: num Windows em
    // portugues, um stream com o locale global trocado escreveria "0,36" e
    // leria "0.36" como 0 - o mesmo projeto abriria com valores diferentes
    // conforme a maquina. As chaves antigas do arquivo sao texto puro e nao
    // tem esse problema; so estes campos numericos precisam do cuidado.

    static std::string FormatFloat(float v) {
        std::ostringstream ss;
        ss.imbue(std::locale::classic());
        ss << v;
        return ss.str();
    }

    static std::string FormatFloat3(const float* v) {
        return FormatFloat(v[0]) + "," + FormatFloat(v[1]) + "," + FormatFloat(v[2]);
    }

    // true so se 'text' inteiro for um float FINITO valido ("1.5", " 2 ").
    // Lixo ("abc", "1.5x", "nan", "") devolve false e deixa 'out' intacto -
    // quem chama mantem o valor padrao em vez de assumir 0.
    static bool ParseFloat(const std::string& text, float& out) {
        std::istringstream ss(text);
        ss.imbue(std::locale::classic());
        float v = 0.0f;
        if (!(ss >> v) || !std::isfinite(v))
            return false;
        ss >> std::ws;
        if (!ss.eof())
            return false; // sobrou texto depois do numero
        out = v;
        return true;
    }

    // "r,g,b" - exatamente 3 floats validos, senao false e 'out' intacto.
    static bool ParseFloat3(const std::string& text, float* out) {
        float parsed[3];
        size_t start = 0;
        for (int i = 0; i < 3; i++) {
            size_t comma = text.find(',', start);
            bool last = (i == 2);
            if ((comma == std::string::npos) != last)
                return false; // virgulas a menos ou a mais
            std::string part = text.substr(start, last ? std::string::npos : comma - start);
            if (!ParseFloat(part, parsed[i]))
                return false;
            start = comma + 1;
        }
        out[0] = parsed[0]; out[1] = parsed[1]; out[2] = parsed[2];
        return true;
    }

    // Limita aos intervalos aceitos (ver RenderSettings::k*). Um arquivo
    // editado a mao com Exposure=0 deixaria a cena inteira preta.
    static void SanitizeRenderSettings(RenderSettings& s) {
        s.Exposure = std::min(std::max(s.Exposure, RenderSettings::kExposureMin), RenderSettings::kExposureMax);
        s.Ambient  = std::min(std::max(s.Ambient, 0.0f), RenderSettings::kAmbientMax);
        int sky = (int)s.Sky;
        if (sky < 0 || sky > (int)SkyMode::Panorama)
            s.Sky = SkyMode::Solid;
        s.SkyRotation = std::min(std::max(s.SkyRotation, -180.0f), 180.0f);
        s.SkyIntensity = std::min(std::max(s.SkyIntensity, 0.0f), RenderSettings::kSkyIntensityMax);
        for (float* color : { s.EnvZenith, s.EnvHorizon, s.EnvGround, s.SkyColor })
            for (int i = 0; i < 3; i++)
                color[i] = std::min(std::max(color[i], 0.0f), 1.0f);
    }

    // Grava 'text' em 'filepath' de forma atomica: escreve num arquivo
    // temporario ao lado e so depois troca pelo definitivo. Um crash ou
    // falta de disco no meio do save nunca deixa o .prismproj pela metade
    // (o projeto e regravado a cada ajuste de renderizacao). Mesmo padrao do
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

        // Ajustes de renderizacao (ver RenderSettings em Project.h). Sempre
        // gravados, mesmo com o valor padrao: o arquivo fica autoexplicativo.
        const RenderSettings& render = m_Project->GetRenderSettings();
        out << "Exposure=" << FormatFloat(render.Exposure) << "\n";
        out << "Ambient=" << FormatFloat(render.Ambient) << "\n";
        out << "EnvZenith=" << FormatFloat3(render.EnvZenith) << "\n";
        out << "EnvHorizon=" << FormatFloat3(render.EnvHorizon) << "\n";
        out << "EnvGround=" << FormatFloat3(render.EnvGround) << "\n";
        out << "Sky=" << (int)render.Sky << "\n";
        out << "SkyColor=" << FormatFloat3(render.SkyColor) << "\n";
        out << "SkyTexture=" << render.SkyTexture << "\n";
        out << "SkyRotation=" << FormatFloat(render.SkyRotation) << "\n";
        out << "SkyIntensity=" << FormatFloat(render.SkyIntensity) << "\n";

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
        // Comeca nos padroes: um .prismproj de antes destas chaves existirem
        // simplesmente nao as tem, e deve abrir com o visual de sempre.
        RenderSettings render;
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
            else if (key == "SkyTexture") render.SkyTexture = value;
            else if (key == "Sky") {
                float mode = 0.0f;
                if (ParseFloat(value, mode))
                    render.Sky = (SkyMode)(int)mode;
                else
                    PRISM_CORE_WARN("Valor invalido em 'Sky' no projeto (\"", value, "\") - usando o padrao.");
            }
            else if (key == "Exposure" || key == "SkyColor" || key == "SkyRotation" || key == "SkyIntensity" || key == "Ambient" || key == "EnvZenith" || key == "EnvHorizon" || key == "EnvGround") {
                // Valor invalido: avisa e mantem o padrao (nunca 0, que
                // apagaria a cena). Uma chave ruim nao invalida as outras.
                bool ok = false;
                if (key == "Exposure")        ok = ParseFloat(value, render.Exposure);
                else if (key == "Ambient")    ok = ParseFloat(value, render.Ambient);
                else if (key == "SkyColor")   ok = ParseFloat3(value, render.SkyColor);
                else if (key == "SkyRotation")  ok = ParseFloat(value, render.SkyRotation);
                else if (key == "SkyIntensity") ok = ParseFloat(value, render.SkyIntensity);
                else if (key == "EnvZenith")  ok = ParseFloat3(value, render.EnvZenith);
                else if (key == "EnvHorizon") ok = ParseFloat3(value, render.EnvHorizon);
                else                          ok = ParseFloat3(value, render.EnvGround);
                if (!ok)
                    PRISM_CORE_WARN("Valor invalido em '", key, "' no projeto (\"", value, "\") - usando o padrao.");
            }
        }

        SanitizeRenderSettings(render);

        m_Project->m_Config = config;
        m_Project->m_RenderSettings = render;
        return true;
    }

}
