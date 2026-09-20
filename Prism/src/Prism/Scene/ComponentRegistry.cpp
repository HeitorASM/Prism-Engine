#include "ComponentRegistry.h"
#include "../Core/Log.h"

namespace Prism {

    std::vector<ComponentTypeInfo> ComponentRegistry::s_Registry;

    void ComponentRegistry::WriteString(std::ofstream& out, const std::string& str) {
        uint32_t length = (uint32_t)str.size();
        out.write(reinterpret_cast<const char*>(&length), sizeof(length));
        if (length > 0)
            out.write(str.data(), length);
    }

    bool ComponentRegistry::ReadString(std::ifstream& in, std::string& outStr) {
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

    // RegisterAll() em si (o corpo com TODOS os Register<T> de cada
    // Component) fica em ComponentRegistration.cpp, nao aqui - separado
    // deliberadamente do resto desta classe (mecanismo) para que
    // adicionar/editar um Component novo signifique tocar em um arquivo
    // pequeno e dedicado (ComponentRegistration.cpp), sem precisar
    // recompilar/entender ComponentRegistry.cpp em si.

}
