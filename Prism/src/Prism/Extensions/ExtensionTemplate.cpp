#include "ExtensionTemplate.h"
#include "ExtensionManifest.h"

#include <cctype>
#include <fstream>

namespace Prism {

    namespace {
        std::string ReplaceAll(std::string text, const std::string& from, const std::string& to) {
            size_t pos = 0;
            while ((pos = text.find(from, pos)) != std::string::npos) {
                text.replace(pos, from.size(), to);
                pos += to.size();
            }
            return text;
        }

        const char* kManifest =
            "name        = @NAME@\n"
            "api         = 1\n"
            "version     = 0.1.0\n"
            "description = Extensao @NAME@\n"
            "author      = \n"
            "target      = both\n"
            "enabled     = true\n";

        const char* kCMake =
            "# Extensao @NAME@ (Prism). O botao \"Compilar\" da aba Extensoes do editor roda isto por voce;\n"
            "# manualmente (mesmo compilador/configuracao do editor):\n"
            "#   cmake -S . -B build -DPRISM_SDK_FILE=<pasta do editor>/PrismSDK.cmake\n"
            "#   cmake --build build --config Release\n"
            "# O resultado vai para ./Binaries (ao lado do extension.ini).\n"
            "cmake_minimum_required(VERSION 3.20)\n"
            "project(@NAME@Extension LANGUAGES CXX)\n"
            "\n"
            "set(PRISM_SDK_FILE \"\" CACHE FILEPATH \"PrismSDK.cmake (fica ao lado do executavel do editor)\")\n"
            "if(NOT PRISM_SDK_FILE)\n"
            "    message(FATAL_ERROR \"Passe -DPRISM_SDK_FILE=<caminho>/PrismSDK.cmake\")\n"
            "endif()\n"
            "include(${PRISM_SDK_FILE})\n"
            "\n"
            "add_library(@NAME@ SHARED @NAME@.cpp)\n"
            "target_include_directories(@NAME@ PRIVATE ${PRISM_SDK_INCLUDE_DIRS})\n"
            "target_compile_features(@NAME@ PRIVATE cxx_std_20)\n"
            "set_target_properties(@NAME@ PROPERTIES\n"
            "    CXX_VISIBILITY_PRESET hidden\n"
            "    VISIBILITY_INLINES_HIDDEN ON\n"
            "    LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/Binaries\"\n"
            "    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/Binaries\")\n"
            "foreach(CONFIG_NAME ${CMAKE_CONFIGURATION_TYPES})\n"
            "    string(TOUPPER ${CONFIG_NAME} CONFIG_UPPER)\n"
            "    set_target_properties(@NAME@ PROPERTIES\n"
            "        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} \"${CMAKE_CURRENT_SOURCE_DIR}/Binaries\"\n"
            "        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} \"${CMAKE_CURRENT_SOURCE_DIR}/Binaries\")\n"
            "endforeach()\n"
            "if(MSVC)\n"
            "    target_compile_options(@NAME@ PRIVATE /utf-8)\n"
            "endif()\n";

        const char* kSource =
            "// Extensao @NAME@: um component de exemplo e um sistema que roda durante o Play.\n"
            "// Edite, clique em \"Compilar\" na aba Extensoes do editor e REINICIE o editor para aplicar.\n"
            "#include <Prism/Extensions/ExtensionAPI.h>\n"
            "#include <Prism/Scene/Scene.h>\n"
            "#include <Prism/Scene/Components.h>\n"
            "\n"
            "using namespace Prism;\n"
            "\n"
            "struct @IDENT@ {\n"
            "    float Value = 1.0f;\n"
            "};\n"
            "\n"
            "class @IDENT@System : public IEngineSystem {\n"
            "public:\n"
            "    const char* GetName() const override { return \"@NAME@.System\"; }\n"
            "    void OnUpdate(Scene& scene, float dt) override {\n"
            "        (void)dt;\n"
            "        for (auto handle : scene.GetRegistry().view<@IDENT@>()) {\n"
            "            @IDENT@& c = scene.GetRegistry().get<@IDENT@>(handle);\n"
            "            (void)c; // sua logica aqui\n"
            "        }\n"
            "    }\n"
            "};\n"
            "\n"
            "static bool Init(const ExtensionHost& host) {\n"
            "    ExtensionComponentDesc d;\n"
            "    d.Name = \"@NAME@\";        // chave gravada nos mapas: nao renomeie depois de publicar\n"
            "    d.Category = \"Extensoes\";\n"
            "    d.Version = 1;              // campos novos: acrescente NO FIM e suba a versao\n"
            "    d.Serialize = [](std::ostream& out, Entity e) {\n"
            "        ComponentRegistry::WriteRaw(out, e.GetComponent<@IDENT@>().Value);\n"
            "    };\n"
            "    d.Deserialize = [](std::istream& in, Entity e, uint32_t /*version*/) {\n"
            "        @IDENT@ c;\n"
            "        if (!ComponentRegistry::ReadRaw(in, c.Value))\n"
            "            return false;\n"
            "        if (e.HasComponent<@IDENT@>()) e.RemoveComponent<@IDENT@>();\n"
            "        e.AddComponent<@IDENT@>(c);\n"
            "        return true;\n"
            "    };\n"
            "    ExtensionUi ui = host.Ui; // por valor: o host so vale durante o Init\n"
            "    d.DrawInspector = [ui](Entity e) {\n"
            "        ui.DragFloat(\"Valor\", &e.GetComponent<@IDENT@>().Value, 0.1f, -100000.0f, 100000.0f);\n"
            "    };\n"
            "    if (!RegisterComponent<@IDENT@>(host, d))\n"
            "        return false;\n"
            "    RegisterSystem<@IDENT@System>(host);\n"
            "    LogMessage(host, 0, \"@NAME@ carregada.\");\n"
            "    return true;\n"
            "}\n"
            "\n"
            "PRISM_EXTENSION(Init)\n";
    }

    std::string ExtensionIdentifier(const std::string& name) {
        std::string id;
        for (char c : name)
            id += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
        if (id.empty() || std::isdigit((unsigned char)id[0]))
            id.insert(id.begin(), '_');
        return id;
    }

    std::vector<std::pair<std::string, std::string>> GenerateExtensionTemplate(const std::string& name) {
        auto fill = [&](const char* text) {
            return ReplaceAll(ReplaceAll(text, "@NAME@", name), "@IDENT@", ExtensionIdentifier(name));
        };
        return {
            { "extension.ini", fill(kManifest) },
            { "CMakeLists.txt", fill(kCMake) },
            { name + ".cpp", fill(kSource) },
            { ".gitignore", "Binaries/\nbuild/\n" },
        };
    }

    bool WriteExtensionTemplate(const std::filesystem::path& parentDirectory, const std::string& name,
                                std::filesystem::path& outDirectory, std::string& error) {
        namespace fs = std::filesystem;
        if (!IsValidExtensionName(name)) {
            error = "Nome invalido: use 1 a 64 caracteres entre letras, numeros, _ . -";
            return false;
        }
        if (name == "." || name == "..") {
            error = "Nome invalido.";
            return false;
        }
        const fs::path directory = parentDirectory / name;
        std::error_code ec;
        if (fs::exists(directory, ec)) {
            error = "Ja existe uma pasta com esse nome.";
            return false;
        }
        if (!fs::create_directories(directory, ec) || ec) {
            error = "Nao foi possivel criar a pasta: " + ec.message();
            return false;
        }
        for (const auto& [relative, content] : GenerateExtensionTemplate(name)) {
            std::ofstream out(directory / relative, std::ios::binary);
            if (!out) {
                error = "Nao foi possivel gravar " + relative;
                return false;
            }
            out << content;
            if (!out.good()) {
                error = "Falha ao gravar " + relative;
                return false;
            }
        }
        outDirectory = directory;
        return true;
    }

} // namespace Prism
