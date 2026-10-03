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

// PropertiesPanel_Material.cpp
// UI do MaterialComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawMaterialUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawMaterialUI() {
        auto& material = m_Ctx.SelectedEntity.GetComponent<Prism::MaterialComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Material", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            // --- Material Asset (.prismmat) - reutilizar entre entidades ---
            // "Salvar como Asset" grava os campos ATUAIS deste
            // MaterialComponent num arquivo .prismmat (ver
            // MaterialSerializer.h) dentro de Assets/Materials -
            // "Carregar de Asset" faz o inverso, sobrescrevendo os
            // campos deste MaterialComponent com os de um .prismmat
            // existente (via LoadMaterialAssetCommand, com undo - ver
            // EditorCommands.h). Arrastar um .prismmat do Content
            // Browser direto para este cabecalho faz o mesmo que
            // "Carregar de Asset" (ver drop target logo abaixo) - dois
            // jeitos de chegar no mesmo resultado, mesmo espirito dos
            // slots de textura abaixo (arrastar OU digitar o path).
            //
            // VINCULO VIVO (ver comentario grande em
            // MaterialSerializer.h e MaterialComponent::LinkedAsset,
            // Components.h): "Carregar de Asset"/arrastar LIGA o
            // vinculo com aquele arquivo - toda edicao abaixo passa a
            // ser gravada nele automaticamente (com um pequeno atraso,
            // ver MaterialLinkSync::FlushSave), e qualquer OUTRA
            // entidade vinculada ao mesmo arquivo se atualiza sozinha
            // (ver ReconcileLinkedMaterial). O selo dourado "Vinculado"
            // abaixo e o unico sinal permanente disso - sem ele, nada
            // aqui indicaria que estas edicoes saem da tela.
            bool isLinked = material.LinkedAsset.IsValid();
            if (isLinked) {
                auto project = Prism::Project::GetActive();
                std::filesystem::path linkedPath = project ? project->GetAssetRegistry().AbsolutePath(material.LinkedAsset) : std::filesystem::path{};
                // Path vazio: o AssetID nao resolve mais (arquivo
                // apagado/movido sem o .meta - ver Assets/AssetRegistry.h).
                // O vinculo continua tecnicamente presente no
                // component (FlushMaterialLinkSave decide o que fazer
                // ao tentar gravar), mas o selo avisa em vermelho em
                // vez de fingir que esta tudo bem.
                bool linkBroken = linkedPath.empty();
                ImVec4 badgeColor = linkBroken ? ImVec4(0.85f, 0.35f, 0.35f, 1.0f) : ImVec4(0.95f, 0.75f, 0.20f, 1.0f); // dourado = vinculado, vermelho = vinculo quebrado
                ImGui::TextColored(badgeColor, linkBroken ? "[Vinculo quebrado]" : "[Vinculado]");
                if (ImGui::IsItemHovered()) {
                    // Duas chamadas distintas (nao uma string de
                    // formato condicional com argumento fixo) - a
                    // string de 'linkBroken' nao usa %s nenhum, e
                    // ImGui::SetTooltip e IM_FMTARGS-anotado
                    // (-Wformat reclamaria de um argumento sobrando
                    // se so o texto mudasse com o mesmo argumento).
                    if (linkBroken)
                        ImGui::SetTooltip("Este material esta vinculado a um asset que nao foi encontrado.\nEdicoes NAO serao salvas em arquivo ate o vinculo ser corrigido ou desfeito.");
                    else
                        ImGui::SetTooltip("Este material esta vinculado a:\n%s\n\nEditar qualquer campo abaixo atualiza o arquivo, e qualquer\noutra entidade vinculada ao mesmo material acompanha a mudanca.",
                            linkedPath.filename().string().c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Desvincular")) {
                    // Desfaz APENAS o vinculo - os valores atuais dos
                    // campos permanecem intactos na entidade, so
                    // param de ser uma "view" do arquivo (equivalente
                    // a "Make Unique" na Unity ou desconectar um
                    // recurso herdado na Godot). Undo simples (nao um
                    // Command dedicado): reverter isto e so voltar a
                    // marcar LinkedAsset, o mesmo custo de reabrir o
                    // asset - nao justifica um Command so para isto.
                    material.LinkedAsset = {};
                    if (m_Ctx.MaterialLinks.IsPendingFor(m_Ctx.SelectedEntity))
                        m_Ctx.MaterialLinks.ForgetPending(); // descarta qualquer edicao pendente deste material - nao ha mais arquivo vinculado para gravar
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Transforma este material numa copia independente. As texturas e valores atuais sao mantidos, mas deixam de acompanhar o arquivo.");
                ImGui::Separator();
            }

            if (ImGui::Button("Salvar como Asset...")) {
                m_ShowSaveMaterialPopup = true;
                std::string suggested = m_Ctx.SelectedEntity.HasComponent<Prism::TagComponent>()
                    ? m_Ctx.SelectedEntity.GetComponent<Prism::TagComponent>().Tag : std::string("Material");
                strncpy(m_SaveMaterialNameBuffer, suggested.c_str(), sizeof(m_SaveMaterialNameBuffer) - 1);
                m_SaveMaterialNameBuffer[sizeof(m_SaveMaterialNameBuffer) - 1] = '\0';
            }
            ImGui::SameLine();
            ImGui::TextDisabled("ou arraste um .prismmat aqui:");

            ImGui::Button("Carregar de Asset (arraste aqui)", ImVec2(-1, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MATERIAL_PATH")) {
                    std::string pathString((const char*)payload->Data, payload->DataSize - 1);
                    // Uma edicao pendente do vinculo ANTERIOR (se
                    // havia um) precisa ser gravada antes de trocar
                    // de arquivo - senao FlushMaterialLinkSave, no
                    // proximo frame, gravaria os valores NOVOS
                    // (recem-carregados) no asset ANTIGO por engano.
                    if (m_Ctx.MaterialLinks.IsPendingFor(m_Ctx.SelectedEntity))
                        m_Ctx.MaterialLinks.FlushSave();
                    m_Ctx.History.Execute(Prism::CreateScope<LoadMaterialAssetCommand>(m_Ctx.SelectedEntity, pathString));
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Separator();

            // Cada slot de textura vira um "cartao": preview GRANDE
            // (96x96, arrastavel/solta-vel) a esquerda, nome do
            // arquivo + botoes (limpar/editar path manualmente) a
            // direita - layout em duas colunas fixas, nao um
            // InputText inteiro em cima do preview como na versao
            // anterior (mais dificil de escanear com 3 slots
            // seguidos). O path completo (nem sempre curto) fica so
            // no tooltip, nao ocupando espaco de tela permanente.
            //
            // 'isSRGB' passado para GetOrLoadTexture deve bater
            // EXATAMENTE com o que Renderer::DrawMesh usa para o
            // mesmo campo (Albedo=true, Normal/RoughnessMetallic=
            // false) - ver comentario grande em Texture.h sobre por
            // que isso importa para PBR correto.
            constexpr float previewSize = 96.0f;

            auto renderTextureSlot = [&](const char* label, const char* hint, std::string& path, bool isSRGB) {
                ImGui::PushID(label);

                // Detecta mudanca em 'path' (drop, "Limpar" ou edicao
                // manual, todos abaixo) comparando com o valor de
                // ENTRADA desta chamada - mais simples e mais
                // confiavel que instrumentar cada um dos 3 pontos que
                // escrevem em 'path' individualmente (e novos pontos
                // futuros ja ficam cobertos de graca). MarkMaterialLinkDirty
                // e um no-op se este material nao estiver vinculado
                // (ver comentario la), entao chamar sempre aqui embaixo
                // e seguro mesmo fora do caso vinculado.
                std::string pathBefore = path;

                Prism::Texture2D* texture = path.empty() ? nullptr : Prism::Renderer::GetOrLoadTexture(path, isSRGB);
                bool hasValidTexture = texture && texture->IsValid();
                bool hasBrokenPath = !path.empty() && !hasValidTexture;

                // --- Preview / drop target (coluna esquerda) --------
                ImGui::BeginGroup();
                if (hasValidTexture) {
                    ImGui::Image((ImTextureID)(uintptr_t)texture->GetRendererID(), ImVec2(previewSize, previewSize));
                }
                else {
                    // Sem textura (ou path quebrado): um botao vazio
                    // do mesmo tamanho do preview, so para servir de
                    // area de drop e dar feedback visual claro de
                    // "solte uma imagem aqui" - cor vermelha se o
                    // path atual esta quebrado, cinza neutro se
                    // realmente vazio.
                    ImVec4 emptyColor = hasBrokenPath ? ImVec4(0.35f, 0.18f, 0.18f, 1.0f) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, emptyColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, emptyColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, emptyColor);
                    ImGui::Button(hasBrokenPath ? "Path\nquebrado" : "Arraste uma\nimagem aqui", ImVec2(previewSize, previewSize));
                    ImGui::PopStyleColor(3);
                }

                // Drop target: aceita CONTENT_BROWSER_IMAGE_PATH (ver
                // ContentBrowserPanel::RenderGrid) sobre o preview
                // INTEIRO (funciona igual solte numa textura ja
                // carregada ou no botao vazio acima).
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_IMAGE_PATH")) {
                        std::filesystem::path droppedPath((const char*)payload->Data);
                        // ContentBrowserPanel manda o path ABSOLUTO
                        // (ver comentario la) - convertemos de volta
                        // para relativo a pasta do projeto antes de
                        // salvar em MaterialComponent, mesma
                        // convencao que o campo de texto manual usa
                        // (ver comentario grande em Renderer.h sobre
                        // GetOrLoadTexture resolvendo o inverso).
                        if (auto project = Prism::Project::GetActive()) {
                            std::error_code ec;
                            auto relativePath = std::filesystem::relative(droppedPath, project->GetProjectDirectory(), ec);
                            path = !ec ? relativePath.generic_string() : droppedPath.generic_string();
                        }
                        else {
                            path = droppedPath.generic_string();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", path.empty() ? "Arraste uma imagem do painel Conteudo do Projeto, ou edite o caminho ao lado." : path.c_str());
                ImGui::EndGroup();

                // --- Nome + status + acoes (coluna direita) ---------
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(label);
                ImGui::TextDisabled("%s", hint);

                std::string fileName = path.empty() ? "(nenhuma)" : std::filesystem::path(path).filename().string();
                ImGui::TextWrapped("%s", fileName.c_str());

                if (hasValidTexture)
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%u x %u", texture->GetWidth(), texture->GetHeight());
                else if (hasBrokenPath)
                    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "Nao encontrada");

                if (!path.empty() && ImGui::SmallButton("Limpar"))
                    path.clear();

                // Edicao manual do path continua disponivel (colapsada
                // atras de um CollapsingHeader pequeno) - drag & drop
                // e o fluxo principal agora, mas digitar/colar ainda
                // e util (ex: corrigir um path quebrado sem precisar
                // achar o arquivo de novo no Content Browser).
                if (ImGui::TreeNodeEx("Editar caminho manualmente", ImGuiTreeNodeFlags_None)) {
                    char buffer[256];
                    strncpy(buffer, path.c_str(), sizeof(buffer) - 1);
                    buffer[sizeof(buffer) - 1] = '\0';
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##Path", buffer, sizeof(buffer)))
                        path = buffer;
                    ImGui::TreePop();
                }

                if (path != pathBefore)
                    m_Ctx.MaterialLinks.MarkDirty(m_Ctx.SelectedEntity);

                ImGui::EndGroup();

                ImGui::PopID();
                };

            renderTextureSlot("Albedo", "Cor base (RGB)", material.AlbedoPath, /*isSRGB*/ true);
            if (ImGui::ColorEdit3("Tint de Albedo", glm::value_ptr(material.AlbedoTint)))
                m_Ctx.MaterialLinks.MarkDirty(m_Ctx.SelectedEntity);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Multiplica a textura de Albedo (ou serve como cor solida, se nenhuma textura estiver configurada acima).");

            ImGui::Separator();
            renderTextureSlot("Normal Map", "Tangent-space", material.NormalPath, /*isSRGB*/ false);

            ImGui::Separator();
            renderTextureSlot("Roughness/Metallic", "G=roughness, B=metallic (glTF)", material.RoughnessMetallicPath, /*isSRGB*/ false);
            if (ImGui::SliderFloat("Roughness Factor", &material.RoughnessFactor, 0.0f, 1.0f))
                m_Ctx.MaterialLinks.MarkDirty(m_Ctx.SelectedEntity);
            if (ImGui::SliderFloat("Metallic Factor", &material.MetallicFactor, 0.0f, 1.0f))
                m_Ctx.MaterialLinks.MarkDirty(m_Ctx.SelectedEntity);
            ImGui::TextDisabled("(?) Roughness: 0 = espelhado, 1 = fosco. Metallic: 0 = plastico/madeira/pedra, 1 = metal. Com um mapa carregado, o fator multiplica o mapa (G = roughness, B = metallic).");
        }
        if (!keepOpen)
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::MaterialComponent>>(m_Ctx.SelectedEntity, "Material"));
    }

}
