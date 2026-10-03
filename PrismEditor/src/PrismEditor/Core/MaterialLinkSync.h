#pragma once

// ============================================================================
// MaterialLinkSync.h
// Vinculo vivo de Material (MaterialComponent::LinkedAsset <-> .prismmat),
// lado do editor. Antes eram 3 metodos e 4 membros de EditorLayer.
// Ver docs/arquitetura.md ("Vinculo vivo de Material").
//
// Fluxo: o painel Propriedades chama MarkDirty() a cada edicao; FlushSave()
// grava no arquivo depois de um pequeno atraso (debounce); Reconcile() roda
// todo frame e propaga mudancas do arquivo para todas as entidades vinculadas.
// ============================================================================

#include <Prism.h>
#include <filesystem>
#include <unordered_map>

namespace PrismEditor {

    class MaterialLinkSync {
    public:
        // Tempo (s) sem editar antes de gravar o .prismmat (cobre arrastar slider
        // e o picker de cor). Mesmo valor do debounce do menu Renderizacao.
        static constexpr double kSaveDelay = 0.35;

        // --- Vinculo vivo de Material (MaterialComponent::LinkedAsset,
        // ver Components.h e o comentario grande em MaterialSerializer.h)
        // ---------------------------------------------------------------
        //
        // Duas direcoes independentes, cada uma sua propria funcao:
        //
        //   EDITAR no painel -> GRAVAR no arquivo (esta entidade, com
        //   debounce - mesmo padrao de FlushRenderSettingsSave/
        //   kRenderSettingsSaveDelay, so que por MaterialComponent em vez
        //   de global). Chamada por RenderPropertiesPanel a cada campo
        //   editado (MarkMaterialLinkDirty) e por RenderMenuBar todo
        //   frame (FlushMaterialLinkSave), igual FlushRenderSettingsSave.
        //
        //   ARQUIVO mudou -> RECARREGAR em toda entidade vinculada aquele
        //   AssetID, INCLUSIVE entidades que a propria acao acima acabou
        //   de gravar (ver comentario em ReconcileLinkedMaterial sobre
        //   por que isso e seguro e nao um eco infinito) e entidades que
        //   nunca abriram o painel Material nesta sessao. Varre TODA a
        //   Scene ativa uma vez por frame (RenderScene), custando um
        //   'view' do EnTT +, no maximo, um stat(2) por material
        //   vinculado distinto (cacheado por AssetID - ver
        //   m_FileTimes) - barato mesmo com muitas entidades
        //   linkadas ao mesmo asset.
        void MarkDirty(Prism::Entity entity);

        void FlushSave();

        void Reconcile(const Prism::Ref<Prism::Scene>& scene);

        // Grava AGORA a edicao pendente, se houver, sem esperar mais nenhum frame
        // (usado em OnDetach, quando nao ha mais frame vindo). Nao usa
        // Application::Get(): e seguro durante a destruicao do Application.
        void WritePendingOnShutdown();

        // Ha edicao pendente desta entidade?
        bool IsPendingFor(Prism::Entity entity) const { return m_DirtyEntity == entity; }

        // Descarta a edicao pendente SEM gravar. Usar quando a Scene e trocada
        // (o handle apontaria para uma entidade da Scene antiga) ou o material
        // foi desvinculado (nao ha mais arquivo para gravar).
        void ForgetPending() { m_DirtyEntity = {}; }

    private:
        // --- Vinculo vivo de Material (ver MarkMaterialLinkDirty acima) ---
        // Analogo a m_RenderSettingsNeedSave/m_RenderSettingsLastEdit, mas
        // guardando TAMBEM qual entidade e qual asset estao pendentes: ao
        // contrario do menu Renderizacao (um unico estado global), varias
        // entidades diferentes podem estar vinculadas a materiais
        // diferentes - so precisamos de UM pendente por vez porque so uma
        // entidade pode estar com o painel Material aberto e sendo editada
        // por vez (EditorContext::SelectedEntity). Se a selecao mudar com uma edicao
        // pendente, RenderPropertiesPanel forca o flush antes de trocar
        // (ver comentario la) - nunca ficamos com uma pendencia "orfa"
        // apontando para uma entidade que o usuario ja nao esta olhando.
        Prism::Entity m_DirtyEntity;

        Prism::AssetID m_DirtyAsset;

        double m_LastEdit = 0.0;

        // Cache "AssetID do material vinculado -> ultima hora de
        // modificacao do arquivo que NOS mesmos observamos" (nao a hora
        // atual do arquivo - a ultima que ja processamos). Usado por
        // ReconcileLinkedMaterial para saber se o .prismmat mudou desde a
        // ultima varredura sem precisar reler o CONTEUDO do arquivo toda
        // vez - so um std::filesystem::last_write_time (stat, nao I/O de
        // dados). Tambem evita reprocessar (e re-logar) o mesmo asset uma
        // vez por entidade vinculada a ele; uma entrada por AssetID basta.
        std::unordered_map<Prism::AssetID, std::filesystem::file_time_type> m_FileTimes;
    };

}
