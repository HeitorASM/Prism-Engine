#include "ComponentRegistry.h"
#include "../Core/Log.h"

#include <string>
#include <unordered_set>

namespace Prism {

    std::vector<ComponentTypeInfo> ComponentRegistry::s_Registry;
    uint32_t ComponentRegistry::s_CurrentBlockVersion = 1;

    void ComponentRegistry::WriteString(std::ostream& out, const std::string& str) {
        uint32_t length = (uint32_t)str.size();
        out.write(reinterpret_cast<const char*>(&length), sizeof(length));
        if (length > 0)
            out.write(str.data(), length);
    }

    bool ComponentRegistry::ReadString(std::istream& in, std::string& outStr) {
        uint32_t length = 0;
        in.read(reinterpret_cast<char*>(&length), sizeof(length));
        if (!in) return false;

        // Sanidade: um nome gigante e sinal de arquivo corrompido ou lido
        // com offset errado; nao alocamos as cegas.
        if (length > (16 * 1024 * 1024)) {
            PRISM_CORE_ERROR("ComponentRegistry::ReadString: string absurdamente grande (", length, " bytes) - arquivo provavelmente corrompido.");
            return false;
        }

        outStr.resize(length);
        if (length > 0)
            in.read(outStr.data(), length);
        return (bool)in;
    }

    // Teto de um bloco de component: bem acima de qualquer component real, mas
    // impede que um tamanho corrompido faca alocar gigabytes.
    static constexpr uint32_t kMaxComponentBlockBytes = 64u * 1024u * 1024u;

    void ComponentRegistry::WriteComponentBlocks(std::ostream& out, Entity entity) {
        uint32_t count = 0;
        for (auto& info : s_Registry)
            if (info.Has(entity))
                count++;
        WriteRaw(out, count);

        for (auto& info : s_Registry) {
            if (!info.Has(entity))
                continue;
            // Serializa para a memoria primeiro: o tamanho precisa vir ANTES dos bytes.
            std::ostringstream block(std::ios::out | std::ios::binary);
            info.Serialize(block, entity);
            const std::string bytes = block.str();

            WriteString(out, info.DisplayName);
            WriteRaw(out, info.Version);
            WriteRaw(out, (uint32_t)bytes.size());
            if (!bytes.empty())
                out.write(bytes.data(), (std::streamsize)bytes.size());
        }
    }

    bool ComponentRegistry::ReadComponentBlocks(std::istream& in, Entity entity, uint32_t entityIndex) {
        // Avisos "uma vez por nome": um mapa com 10 mil entidades de um
        // component desconhecido nao pode inundar o console.
        static std::unordered_set<std::string> warnedUnknown;
        static std::unordered_set<std::string> warnedNewer;

        uint32_t count = 0;
        if (!ReadRaw(in, count)) {
            PRISM_CORE_ERROR("ComponentRegistry: arquivo corrompido (contagem de components da entidade ", entityIndex, ").");
            return false;
        }
        // O registro tem poucas dezenas de tipos; um numero enorme e lixo.
        if (count > 4096) {
            PRISM_CORE_ERROR("ComponentRegistry: contagem de components absurda (", count, ") na entidade ", entityIndex, " - arquivo corrompido.");
            return false;
        }

        for (uint32_t i = 0; i < count; i++) {
            std::string name;
            uint32_t version = 0;
            uint32_t size = 0;
            if (!ReadString(in, name) || !ReadRaw(in, version) || !ReadRaw(in, size)) {
                PRISM_CORE_ERROR("ComponentRegistry: arquivo corrompido (cabecalho de component da entidade ", entityIndex, ").");
                return false;
            }
            if (size > kMaxComponentBlockBytes) {
                PRISM_CORE_ERROR("ComponentRegistry: bloco de '", name, "' absurdo (", size, " bytes) na entidade ", entityIndex, " - arquivo corrompido.");
                return false;
            }
            std::string bytes(size, '\0');
            if (size > 0) {
                in.read(bytes.data(), (std::streamsize)size);
                if (!in) {
                    PRISM_CORE_ERROR("ComponentRegistry: arquivo truncado (bloco de '", name, "' da entidade ", entityIndex, ").");
                    return false;
                }
            }

            ComponentTypeInfo* found = nullptr;
            for (auto& info : s_Registry) {
                if (info.DisplayName == name) { found = &info; break; }
            }
            if (!found) {
                if (warnedUnknown.insert(name).second)
                    PRISM_CORE_WARN("ComponentRegistry: component desconhecido '", name, "' ignorado (arquivo de uma versao mais nova da engine?).");
                continue;
            }
            if (found->Has(entity)) {
                PRISM_CORE_WARN("ComponentRegistry: bloco duplicado de '", name, "' na entidade ", entityIndex, " ignorado.");
                continue;
            }
            if (version > found->Version && warnedNewer.insert(name).second)
                PRISM_CORE_WARN("ComponentRegistry: '", name, "' foi gravado com uma versao mais nova (v", version, ", esta engine conhece v", found->Version, "); campos extras serao ignorados.");

            std::istringstream blockStream(bytes, std::ios::in | std::ios::binary);
            s_CurrentBlockVersion = version;
            bool ok = found->Deserialize(blockStream, entity, entityIndex);
            s_CurrentBlockVersion = found->Version;
            if (!ok) {
                // A mensagem especifica ja foi logada pelo Deserialize. Como o
                // bloco tem tamanho, o resto do arquivo segue valido: descarta so
                // este component (sem deixar um component pela metade).
                if (found->Has(entity))
                    found->Remove(entity);
                PRISM_CORE_ERROR("ComponentRegistry: component '", name, "' da entidade ", entityIndex, " descartado (dados invalidos).");
            }
        }
        return true;
    }

    // Le e descarta o bloco do antigo UIElementComponent (flag + campos, ver historico do git).
    static bool SkipRemovedLegacyUIElement(std::istream& in) {
        bool has = false;
        if (!ComponentRegistry::ReadRaw(in, has))
            return false;
        if (!has)
            return true;
        char scratch[16];
        std::string text;
        // Type(int) Visible(bool) Layer(int) Anchor(int) Offset(2f) Size(2f) Color(4f)
        in.read(scratch, 4); in.read(scratch, 1); in.read(scratch, 4); in.read(scratch, 4);
        in.read(scratch, 8); in.read(scratch, 8); in.read(scratch, 16);
        if (!in || !ComponentRegistry::ReadString(in, text) || !ComponentRegistry::ReadString(in, text))
            return false; // TexturePath, Text
        // FontSize(f) Align(int) TextColor(4f) Interactable(bool) Hover(4f) Pressed(4f) BlocksPointer(bool)
        in.read(scratch, 4); in.read(scratch, 4); in.read(scratch, 16); in.read(scratch, 1);
        in.read(scratch, 16); in.read(scratch, 16); in.read(scratch, 1);
        return (bool)in;
    }

    bool ComponentRegistry::ReadLegacyComponents(std::istream& in, Entity entity, uint32_t entityIndex, uint32_t sceneFormat) {
        for (auto& info : s_Registry) {
            // O UIElementComponent (UI antiga, formato v19+) saiu da engine, mas ficava ENTRE o
            // Particle Emitter e a Camera no formato "flag + campos": pula o bloco dele.
            if (info.DisplayName == "Camera" && sceneFormat >= 19 && !SkipRemovedLegacyUIElement(in)) {
                PRISM_CORE_ERROR("ComponentRegistry: arquivo corrompido (UI antiga da entidade ", entityIndex, ").");
                return false;
            }
            if (sceneFormat < info.AddedInSceneFormat)
                continue; // nao existia neste formato: nao ha flag no arquivo
            bool has = false;
            if (!ReadRaw(in, has)) {
                PRISM_CORE_ERROR("ComponentRegistry: arquivo corrompido (flag de '", info.DisplayName, "' da entidade ", entityIndex, ").");
                return false;
            }
            if (!has)
                continue;
            s_CurrentBlockVersion = info.LegacyVersion ? info.LegacyVersion(sceneFormat) : 1u;
            bool ok = info.Deserialize(in, entity, entityIndex);
            s_CurrentBlockVersion = info.Version;
            if (!ok)
                return false; // sem tamanho no formato legado: nao da para pular, a leitura inteira aborta
        }
        return true;
    }

    // RegisterAll() em si (o corpo com TODOS os Register<T> de cada
    // Component) fica em ComponentRegistration.cpp, nao aqui - separado
    // deliberadamente do resto desta classe (mecanismo) para que
    // adicionar/editar um Component novo signifique tocar em um arquivo
    // pequeno e dedicado (ComponentRegistration.cpp), sem precisar
    // recompilar/entender ComponentRegistry.cpp em si.

}
