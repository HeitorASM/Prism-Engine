#include "MaterialLinkSync.h"
#include <imgui.h> // ImGui::GetTime - relogio do debounce (mesma base de tempo do resto do editor)

namespace PrismEditor {

    void MaterialLinkSync::MarkDirty(Prism::Entity entity) {
        if (!entity || !entity.HasComponent<Prism::MaterialComponent>())
            return;

        Prism::AssetID linked = entity.GetComponent<Prism::MaterialComponent>().LinkedAsset;
        if (!linked.IsValid())
            return; // material independente (sem vinculo) - nada a gravar em arquivo nenhum

        // Trocou de entidade/material com uma pendencia diferente ainda
        // no ar: grava a pendencia ANTERIOR antes de comecar a rastrear a
        // nova, senao aquela edicao anterior seria perdida (nunca mais
        // teriamos m_DirtyEntity apontando para ela).
        if (m_DirtyEntity && (m_DirtyEntity != entity || m_DirtyAsset != linked))
            FlushSave();

        m_DirtyEntity = entity;
        m_DirtyAsset = linked;
        m_LastEdit = ImGui::GetTime();
    }

    void MaterialLinkSync::FlushSave() {
        if (!m_DirtyEntity)
            return;

        // Mesmo debounce de FlushRenderSettingsSave (ver comentario la):
        // arrastar um slider de Roughness/Metallic ou o color picker do
        // Albedo Tint dispara MarkMaterialLinkDirty a cada frame enquanto
        // o mouse esta apertado - sem esperar o mouse soltar E um
        // intervalo sem edicao, gravariamos o arquivo dezenas de vezes
        // por segundo.
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            return;
        if (ImGui::GetTime() - m_LastEdit < kSaveDelay)
            return;

        Prism::Entity entity = m_DirtyEntity;
        Prism::AssetID asset = m_DirtyAsset;
        m_DirtyEntity = {};
        m_DirtyAsset = {};

        // A entidade pode ter sido apagada (Delete/Undo de criacao) ou o
        // Material removido dela enquanto a edicao esperava o debounce -
        // sem vinculo nenhum mais para gravar, so descarta a pendencia.
        if (!entity || !entity.HasComponent<Prism::MaterialComponent>())
            return;
        auto& material = entity.GetComponent<Prism::MaterialComponent>();
        if (material.LinkedAsset != asset)
            return; // o vinculo mudou (Carregar/Desvincular) entre a edicao e este flush - a pendencia antiga nao vale mais

        auto project = Prism::Project::GetActive();
        if (!project)
            return;
        std::filesystem::path assetPath = project->GetAssetRegistry().AbsolutePath(asset);
        if (assetPath.empty()) {
            // O arquivo do asset sumiu (apagado/movido sem o .meta - ver
            // Assets/AssetRegistry.h) desde que o vinculo foi criado. Nao
            // ha onde gravar; a entidade continua com o AssetID antigo
            // (nao desvinculamos sozinhos - o usuario pode ter so movido
            // o arquivo e um Refresh do Content Browser ainda resolve).
            PRISM_ERROR("Vinculo de material aponta para um asset que nao existe mais - a edicao NAO foi salva em arquivo (o material continua vinculado; use Desvincular ou restaure o arquivo).");
            return;
        }

        if (!Prism::MaterialSerializer::Serialize(material, assetPath))
            PRISM_ERROR("Falha ao gravar o material vinculado em '", assetPath.string(), "' - ver console para detalhes.");
        else
            // Registra a hora que ACABAMOS DE GRAVAR: ReconcileLinkedMaterial
            // (chamado no mesmo frame, logo depois, por RenderScene) usa
            // este valor para saber que ja processou esta escrita e nao
            // precisa reler o arquivo que ele mesmo produziu - ver
            // comentario grande em ReconcileLinkedMaterial sobre por que
            // isto NAO e um eco perdido (outras entidades com o mesmo
            // AssetID ainda sao atualizadas normalmente, e o mtime real do
            // disco e o que decide isso, nao uma flag "fui eu que salvei").
            m_FileTimes[asset] = std::filesystem::last_write_time(assetPath);
    }

    void MaterialLinkSync::Reconcile(const Prism::Ref<Prism::Scene>& scene) {
        // Nenhum material foi vinculado ainda nesta sessao (projeto novo,
        // ou ninguem usou "Carregar de Asset"/arrastar um .prismmat) -
        // nem precisa varrer a Scene.
        if (m_FileTimes.empty())
            return;

        auto project = Prism::Project::GetActive();
        if (!project)
            return;

        // 'stale': materiais vinculados cujo ARQUIVO no disco e mais novo
        // que a ultima vez que os lemos - preenchido na primeira passada
        // (barata: so stat, nenhum arquivo aberto) e usado na segunda
        // (que efetivamente re-le e aplica) para nao misturar as duas.
        std::unordered_map<Prism::AssetID, Prism::MaterialComponent> reloaded;
        std::error_code ec;

        for (auto& [asset, lastKnown] : m_FileTimes) {
            std::filesystem::path assetPath = project->GetAssetRegistry().AbsolutePath(asset);
            if (assetPath.empty())
                continue; // asset sumiu - ver mesmo caso em FlushMaterialLinkSave; nada a recarregar

            std::filesystem::file_time_type currentTime = std::filesystem::last_write_time(assetPath, ec);
            if (ec || currentTime == lastKnown)
                continue; // sem mudanca desde a ultima vez que processamos (inclui o que NOS mesmos acabamos de gravar - ver FlushMaterialLinkSave)

            Prism::MaterialComponent loaded;
            if (Prism::MaterialSerializer::Deserialize(assetPath, loaded)) {
                reloaded[asset] = loaded;
                lastKnown = currentTime; // atualiza o cache MESMO em caso de falha logo abaixo, para nao tentar reler o mesmo arquivo quebrado todo frame
            }
            else {
                lastKnown = currentTime;
            }
        }

        if (reloaded.empty())
            return;

        // Aplica em TODA entidade da Scene ativa com um desses AssetIDs -
        // deliberadamente TODAS, nao so a que estava selecionada quando o
        // arquivo mudou: e exatamente isto que faz o vinculo ser "vivo"
        // entre VARIAS entidades (ver MaterialComponent::LinkedAsset,
        // Components.h) em vez de uma relacao 1-para-1.
        //
        // NAO usa CommandHistory aqui: esta e uma sincronizacao de baixo
        // nivel entre arquivo e entidades ja vinculadas (equivalente ao
        // hot-reload de uma textura), nao uma acao do usuario sobre ESTA
        // entidade especifica - um Ctrl+Z logo depois desfaria a ultima
        // acao real do usuario (a edicao original que gerou o arquivo),
        // nao este reflexo em cascata, que e reconstituido de qualquer
        // forma na proxima reconciliacao.
        auto view = scene->GetRegistry().view<Prism::MaterialComponent>();
        for (auto handle : view) {
            auto& material = view.get<Prism::MaterialComponent>(handle);
            auto it = reloaded.find(material.LinkedAsset);
            if (it == reloaded.end())
                continue;

            Prism::AssetID keepLinked = material.LinkedAsset; // Deserialize nao toca em LinkedAsset (ver MaterialSerializer.h) - preservado de qualquer forma, guardado so por clareza
            material = it->second;
            material.LinkedAsset = keepLinked;
        }
    }

    void MaterialLinkSync::WritePendingOnShutdown() {
        // Mesmo raciocinio para uma edicao de material vinculado ainda
        // pendente (a espera do debounce de FlushMaterialLinkSave) - grava
        // direto no .prismmat, sem esperar mais nenhum frame que nao vai
        // vir. Ao contrario do bloco acima (que grava em
        // Project::SaveActive, o .prismproj), MaterialSerializer::Serialize
        // so mexe no arquivo do asset - seguro de chamar aqui mesmo
        // durante a destruicao do Application (mesmo motivo do comentario
        // grande no topo desta funcao: nao usa Application::Get()).
        if (m_DirtyEntity && m_DirtyEntity.HasComponent<Prism::MaterialComponent>()) {
            auto& material = m_DirtyEntity.GetComponent<Prism::MaterialComponent>();
            if (material.LinkedAsset == m_DirtyAsset) {
                if (auto project = Prism::Project::GetActive()) {
                    std::filesystem::path assetPath = project->GetAssetRegistry().AbsolutePath(m_DirtyAsset);
                    if (!assetPath.empty())
                        Prism::MaterialSerializer::Serialize(material, assetPath);
                }
            }
        }
        m_DirtyEntity = {};
    }

}
