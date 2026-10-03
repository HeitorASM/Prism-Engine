#include "PropertiesPanel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "ContentBrowserPanel.h"
#include "ScriptEditorPanel.h"
#include "../Play/PlayWindow.h"
#include "../Commands/EditorCommands.h"
#include "../Core/EntityOps.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// PropertiesPanel_MeshRenderer.cpp
// UI do MeshRendererComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawMeshRendererUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawMeshRendererUI() {
        auto& meshRenderer = m_Ctx.SelectedEntity.GetComponent<Prism::MeshRendererComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Mesh Renderer", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            // --- Modelo IMPORTADO (.obj/.fbx/.gltf/.glb) ------------
            // ModelAsset valido tem prioridade sobre a primitiva 'Mesh'
            // logo abaixo (ver Renderer::ResolveMesh) - por isso o
            // selo/drop target de modelo vem PRIMEIRO no painel: e o
            // que de fato decide a geometria desenhada quando presente.
            bool hasModel = meshRenderer.ModelAsset.IsValid();
            if (hasModel) {
                auto project = Prism::Project::GetActive();
                std::filesystem::path modelPath = project ? project->GetAssetRegistry().AbsolutePath(meshRenderer.ModelAsset) : std::filesystem::path{};
                // Path vazio: mesmo significado de "vinculo quebrado"
                // do Material acima (asset apagado/movido sem o
                // .meta) - Renderer::GetOrLoadModelMesh tambem cai
                // para a primitiva nesse caso (ver ResolveMesh), este
                // selo so avisa visualmente da mesma condicao.
                //
                // Path resolvido mas GetOrLoadModelMesh ainda assim
                // devolve nulo: arquivo EXISTE (o .meta resolve) mas
                // ModelLoader::Load falhou nele (formato corrompido,
                // extensao suportada mas conteudo invalido, etc - ver
                // erro detalhado no console/log). Distinto de
                // "vinculo quebrado" porque a solucao e diferente:
                // corrigir/reexportar o ARQUIVO, nao trocar de
                // vinculo - por isso um terceiro texto de selo em vez
                // de reusar "vinculo quebrado" para os dois casos.
                bool linkBroken = modelPath.empty();
                bool importFailed = !linkBroken && Prism::Renderer::GetOrLoadModelMesh(meshRenderer.ModelAsset) == nullptr;
                ImVec4 badgeColor = (linkBroken || importFailed) ? ImVec4(0.85f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 0.65f, 0.85f, 1.0f); // azul = modelo ok, vermelho = problema
                const char* badgeText = linkBroken ? "[Modelo: vinculo quebrado]" : importFailed ? "[Modelo: falha ao importar]" : "[Modelo importado]";
                ImGui::TextColored(badgeColor, "%s", badgeText);
                if (ImGui::IsItemHovered()) {
                    if (linkBroken)
                        ImGui::SetTooltip("O arquivo de modelo vinculado nao foi encontrado.\nA entidade volta a usar a primitiva 'Mesh' abaixo ate o vinculo ser corrigido ou desfeito.");
                    else if (importFailed)
                        ImGui::SetTooltip("O arquivo foi encontrado, mas nao pode ser importado\n(formato invalido ou corrompido - ver detalhes no log).\nA entidade volta a usar a primitiva 'Mesh' abaixo ate o arquivo ser corrigido.");
                    else
                        ImGui::SetTooltip("Modelo importado de:\n%s\n\nEsta geometria substitui a primitiva 'Mesh' abaixo enquanto vinculada.", modelPath.filename().string().c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Desvincular##Model")) {
                    // Sem Command dedicado, mesmo raciocinio do
                    // "Desvincular" de Material acima: reverter isto e
                    // so voltar a marcar ModelAsset, custo identico ao
                    // de reabrir o modelo - nao justifica undo proprio.
                    meshRenderer.ModelAsset = {};
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove o modelo importado. A entidade volta a usar a primitiva 'Mesh' escolhida abaixo.");
                ImGui::Separator();
            }

            ImGui::TextDisabled(hasModel ? "Primitiva abaixo ignorada enquanto o modelo estiver vinculado:" : "Primitiva embutida:");
            const char* meshNames[] = { "Cubo", "Esfera", "Capsula", "Cilindro", "Plano" };
            int meshIndex = (int)meshRenderer.Mesh;
            if (ImGui::Combo("Mesh", &meshIndex, meshNames, IM_ARRAYSIZE(meshNames)))
                meshRenderer.Mesh = (Prism::PrimitiveMesh)meshIndex;

            ImGui::TextDisabled("ou arraste um modelo (.obj/.fbx/.gltf/.glb) aqui:");
            ImGui::Button("Importar Modelo (arraste aqui)", ImVec2(-1, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MODEL_PATH")) {
                    std::string pathString((const char*)payload->Data, payload->DataSize - 1);
                    m_Ctx.History.Execute(Prism::CreateScope<LoadModelAssetCommand>(m_Ctx.SelectedEntity, pathString));
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Separator();

            ImGui::ColorEdit3("Cor", glm::value_ptr(meshRenderer.Color));
            if (ImGui::IsItemActivated())
                m_ColorBeforeEdit = meshRenderer.Color;
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_Ctx.History.Execute(Prism::CreateScope<MeshColorCommand>(m_Ctx.SelectedEntity, m_ColorBeforeEdit, meshRenderer.Color));
            if (hasModel)
                ImGui::TextDisabled("(usada so se o modelo nao tiver Material com textura de albedo)");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::MeshRendererComponent>>(m_Ctx.SelectedEntity, "Mesh Renderer"));
    }

}
