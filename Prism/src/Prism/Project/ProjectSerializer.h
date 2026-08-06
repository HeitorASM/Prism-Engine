#pragma once
#include "Project.h"

namespace Prism {

    // O .prismproj em si e um arquivo de TEXTO (formato simples chave=valor),
    // nao binario - ele so guarda configuracao, e ser legivel/diffavel no Git
    // e mais importante aqui do que performance de leitura. O binario entra
    // em cena para Maps/Cenas (Project::GetMapDirectory()), que sao os dados
    // pesados e carregados a cada frame/sessao de jogo.
    class ProjectSerializer {
    public:
        ProjectSerializer(Ref<Project> project);

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

    private:
        Ref<Project> m_Project;
    };

}
