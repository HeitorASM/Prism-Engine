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

namespace PrismEditor {

    void PropertiesPanel::OnImGuiRender() {
        ImGui::Begin("Propriedades");

        if (!m_Ctx.SelectedEntity) {
            ImGui::TextDisabled("Nada selecionado.");
            ImGui::End();
            return;
        }

        auto& tag = m_Ctx.SelectedEntity.GetComponent<Prism::TagComponent>();
        char nameBuffer[256];
        strncpy(nameBuffer, tag.Tag.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        if (ImGui::InputText("Nome", nameBuffer, sizeof(nameBuffer)))
            tag.Tag = nameBuffer;

        ImGui::Separator();

        RenderPrefabInstanceSection();

        if (m_Ctx.SelectedEntity.HasComponent<Prism::TransformComponent>()) {
            auto& transform = m_Ctx.SelectedEntity.GetComponent<Prism::TransformComponent>();
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Cada DragFloat3 e verificado individualmente logo apos ser
                // desenhado - IsItemActivated()/IsItemDeactivatedAfterEdit()
                // sempre se referem ao ULTIMO item desenhado, entao nao da
                // para checar os tres so no final (so pegaria o de Escala).
                ImGui::DragFloat3("Posicao", glm::value_ptr(transform.Translation), 0.05f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));

                ImGui::DragFloat3("Rotacao", glm::value_ptr(transform.Rotation), 0.5f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));

                ImGui::DragFloat3("Escala", glm::value_ptr(transform.Scale), 0.05f, 0.01f, 100.0f);
                if (ImGui::IsItemActivated())
                    m_TransformBeforeEdit = transform;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_Ctx.History.Execute(Prism::CreateScope<TransformCommand>(m_Ctx.SelectedEntity, m_TransformBeforeEdit, transform));
            }
            // Transform nao tem botao de remover - toda entidade tem um por
            // definicao (ver Scene::CreateEntity) e o resto da engine
            // assume isso (ex: RenderScene le GetTransform() sem checar
            // HasComponent primeiro).
        }

        if (m_Ctx.SelectedEntity.HasComponent<Prism::MeshRendererComponent>()) {
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

        if (m_Ctx.SelectedEntity.HasComponent<Prism::MaterialComponent>()) {
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

        if (m_Ctx.SelectedEntity.HasComponent<Prism::LightComponent>()) {
            auto& light = m_Ctx.SelectedEntity.GetComponent<Prism::LightComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Light", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* typeNames[] = { "Point (Omni)", "Spot", "Directional" };
                int typeIndex = (int)light.Type;
                if (ImGui::Combo("Tipo", &typeIndex, typeNames, IM_ARRAYSIZE(typeNames)))
                    light.Type = (Prism::LightType)typeIndex;

                ImGui::ColorEdit3("Cor##Light", glm::value_ptr(light.Color));
                ImGui::DragFloat("Intensidade", &light.Intensity, 0.05f, 0.0f, 100.0f);

                if (light.Type != Prism::LightType::Directional)
                    ImGui::DragFloat("Alcance", &light.Range, 0.1f, 0.0f, 1000.0f);

                if (light.Type == Prism::LightType::Spot) {
                    // Angulo externo primeiro: se o usuario reduzir o
                    // externo abaixo do interno atual, arrasta o interno
                    // junto (evita um estado "invalido" visualmente
                    // confuso, mesmo que Renderer::CollectGPULights ja
                    // clampe isso ao montar o GPULight).
                    if (ImGui::DragFloat("Angulo do Cone (Externo)", &light.SpotAngle, 0.5f, 1.0f, 90.0f)) {
                        if (light.InnerSpotAngle > light.SpotAngle)
                            light.InnerSpotAngle = light.SpotAngle;
                    }
                    ImGui::DragFloat("Angulo do Cone (Interno)", &light.InnerSpotAngle, 0.5f, 0.0f, light.SpotAngle);
                    ImGui::TextDisabled("(?) Entre os dois angulos a luz cai suavemente ate a borda.");
                }

                ImGui::Checkbox("Projetar Sombras", &light.CastShadows);
                if (light.Type != Prism::LightType::Directional && light.CastShadows && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Shadow mapping hoje so suporta luzes Directional - marcar aqui nao tem efeito visual para Point/Spot ainda.");
            }
            if (!keepOpen)
                m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::LightComponent>>(m_Ctx.SelectedEntity, "Light"));
        }

        if (m_Ctx.SelectedEntity.HasComponent<Prism::ColliderComponent>()) {
            auto& collider = m_Ctx.SelectedEntity.GetComponent<Prism::ColliderComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Collider", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* shapeNames[] = { "Caixa", "Esfera", "Capsula" };
                int shapeIndex = (int)collider.Shape;
                if (ImGui::Combo("Forma", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames)))
                    collider.Shape = (Prism::ColliderShape)shapeIndex;

                switch (collider.Shape) {
                case Prism::ColliderShape::Box:
                    ImGui::DragFloat3("Half-Extents", glm::value_ptr(collider.Size), 0.05f, 0.01f, 100.0f);
                    break;
                case Prism::ColliderShape::Sphere:
                    ImGui::DragFloat("Raio", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                    break;
                case Prism::ColliderShape::Capsule:
                    ImGui::DragFloat("Raio##Capsule", &collider.Size.x, 0.05f, 0.01f, 100.0f);
                    ImGui::DragFloat("Altura##Capsule", &collider.Size.y, 0.05f, 0.01f, 100.0f);
                    break;
                }

                ImGui::Checkbox("E um Trigger", &collider.IsTrigger);
                ImGui::TextDisabled("Precisa de um Rigid Body para participar da fisica.");
            }
            if (!keepOpen)
                m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ColliderComponent>>(m_Ctx.SelectedEntity, "Collider"));
        }

        if (m_Ctx.SelectedEntity.HasComponent<Prism::RigidBodyComponent>()) {
            auto& rigidBody = m_Ctx.SelectedEntity.GetComponent<Prism::RigidBodyComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Rigid Body", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                if (!m_Ctx.SelectedEntity.HasComponent<Prism::ColliderComponent>())
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Sem Collider - adicione um para a fisica funcionar.");

                const char* bodyTypeNames[] = { "Static", "Kinematic", "Dynamic" };
                int bodyTypeIndex = (int)rigidBody.Type;
                if (ImGui::Combo("Tipo##RigidBody", &bodyTypeIndex, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames)))
                    rigidBody.Type = (Prism::BodyType)bodyTypeIndex;

                bool dynamicOnly = (rigidBody.Type == Prism::BodyType::Dynamic);
                ImGui::BeginDisabled(!dynamicOnly);
                ImGui::DragFloat("Massa (kg)", &rigidBody.Mass, 0.1f, 0.01f, 10000.0f);
                ImGui::Checkbox("Usa Gravidade", &rigidBody.UseGravity);
                ImGui::EndDisabled();

                ImGui::Checkbox("Colisao Continua (CCD)", &rigidBody.ContinuousCollisionDetection);

                // So faz sentido fisicamente para Kinematic/Dynamic (Static
                // nunca gira de qualquer jeito, ja que nunca se move - ver
                // BodyType, Components.h) - mas nao ha necessidade de
                // desabilitar o checkbox para Static: um valor "true" nele
                // e simplesmente ignorado nesse caso (CreateBodyForEntity
                // ainda passa fixedRotation para o Jolt independente do
                // tipo, e um corpo Static ja tem rotacao fixa por natureza).
                ImGui::Checkbox("Rotacao Fixa (nao tomba/gira por fisica)", &rigidBody.FixedRotation);
                if (rigidBody.FixedRotation)
                    ImGui::TextDisabled("Corpo ainda translada normalmente - so a ROTACAO fica travada. Use para camera/player controlado por script.");

                ImGui::Separator();
                ImGui::TextDisabled("Material fisico");
                // Friction/Restitution valem para qualquer BodyType (uma
                // rampa Static com atrito baixo ainda afeta o que desliza
                // nela) - ver comentario em RigidBodyComponent, Components.h.
                ImGui::SliderFloat("Atrito", &rigidBody.Friction, 0.0f, 1.0f);
                ImGui::SliderFloat("Restituicao (quique)", &rigidBody.Restitution, 0.0f, 1.0f);

                // Damping so tem efeito em corpos Dynamic (o Jolt integra
                // isso a cada step da simulacao - Static/Kinematic nao sao
                // integrados de qualquer forma).
                ImGui::BeginDisabled(!dynamicOnly);
                ImGui::SliderFloat("Amortecimento Linear", &rigidBody.LinearDamping, 0.0f, 1.0f);
                ImGui::SliderFloat("Amortecimento Angular", &rigidBody.AngularDamping, 0.0f, 1.0f);
                ImGui::EndDisabled();
            }
            if (!keepOpen)
                m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::RigidBodyComponent>>(m_Ctx.SelectedEntity, "Rigid Body"));
        }

        if (m_Ctx.SelectedEntity.HasComponent<Prism::RaycastComponent>()) {
            auto& raycast = m_Ctx.SelectedEntity.GetComponent<Prism::RaycastComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Raycast", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Ativo", &raycast.Enabled);

                // Mesmo padrao Godot: um PONTO local, nao um vetor
                // direcao + distancia separados (ver comentario grande em
                // RaycastComponent, Components.h). DragFloat3 comum, mesmo
                // widget usado por TransformComponent::Translation na
                // Properties panel - o usuario ja conhece essa UI.
                ImGui::DragFloat3("Alvo (espaco local)", glm::value_ptr(raycast.TargetPosition), 0.05f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Ponto ate onde o raio vai, em espaco LOCAL da entidade (gira/translada junto com ela).\nEx: (0,0,-3) = para frente, 3 unidades. (0,-2,0) = para baixo, 2 unidades (sensor de chao).");

                ImGui::Checkbox("Ignorar Pai/Irmas", &raycast.IgnoreParentAndSiblings);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Se marcado, o raio ignora a entidade PAI (se houver) e todas as entidades IRMAS (que compartilham o mesmo pai) - util para um sensor filho do corpo do personagem nao acertar o proprio corpo/outros colliders do mesmo personagem.");

                ImGui::Separator();
                ImGui::TextDisabled("Resultado (fisico - so atualiza durante o modo Play):");
                if (!m_Ctx.ActiveScene->IsRunning()) {
                    ImGui::TextDisabled("(fora do modo Play - sem resultado ainda)");
                }
                else if (raycast.Hit) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Acertou algo");
                    std::string hitName = "(entidade invalida)";
                    if (m_Ctx.ActiveScene->GetRegistry().valid(raycast.HitEntity)) {
                        Prism::Entity hitEntity(raycast.HitEntity, m_Ctx.ActiveScene.get());
                        if (hitEntity.HasComponent<Prism::TagComponent>())
                            hitName = hitEntity.GetComponent<Prism::TagComponent>().Tag;
                    }
                    ImGui::Text("Entidade: %s", hitName.c_str());
                    ImGui::Text("Distancia: %.2f", raycast.HitDistance);
                    ImGui::Text("Ponto: (%.2f, %.2f, %.2f)", raycast.HitPoint.x, raycast.HitPoint.y, raycast.HitPoint.z);
                }
                else {
                    ImGui::TextDisabled("Sem acerto");
                }
            }
            if (!keepOpen)
                m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::RaycastComponent>>(m_Ctx.SelectedEntity, "Raycast"));
        }

        // ==================== SEÇÃO CAMERA (CORRIGIDA) ====================
        if (m_Ctx.SelectedEntity.HasComponent<Prism::CameraComponent>()) {
            auto& camera = m_Ctx.SelectedEntity.GetComponent<Prism::CameraComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Camera", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* projectionNames[] = { "Perspectiva", "Ortografica" };
                int projectionIndex = (int)camera.ProjectionType;
                if (ImGui::Combo("Projecao", &projectionIndex, projectionNames, IM_ARRAYSIZE(projectionNames)))
                    camera.ProjectionType = (Prism::CameraProjectionType)projectionIndex;

                if (camera.ProjectionType == Prism::CameraProjectionType::Perspective)
                    ImGui::DragFloat("FOV", &camera.FOV, 0.5f, 1.0f, 179.0f);
                else
                    ImGui::DragFloat("Tamanho Ortografico", &camera.OrthoSize, 0.1f, 0.01f, 1000.0f);

                ImGui::DragFloat("Near Clip", &camera.NearClip, 0.01f, 0.001f, camera.FarClip - 0.01f);
                ImGui::DragFloat("Far Clip", &camera.FarClip, 1.0f, camera.NearClip + 0.01f, 100000.0f);

                bool isPrimary = camera.Primary;
                if (ImGui::Checkbox("Primary", &isPrimary)) {
                    if (isPrimary)
                        EntityOps::SetPrimaryCamera(m_Ctx, m_Ctx.SelectedEntity);
                    else
                        camera.Primary = false;
                }
                if (!isPrimary)
                    ImGui::TextDisabled("Nao e a camera principal - o modo Play/viewport nao vai usar esta.");

                // --- Pre-visualizacao da camera (integrada) ---
                ImGui::Separator();
                ImGui::TextDisabled("Pre-visualizacao");

                // Reserva uma area com altura fixa (ex: 200px) para o preview
                float previewHeight = 200.0f;
                float previewWidth = ImGui::GetContentRegionAvail().x;
                // Mantem proporção 16:9, mas respeita a largura
                float aspect = 16.0f / 9.0f;
                if (previewWidth / aspect < previewHeight)
                    previewHeight = previewWidth / aspect;
                else
                    previewWidth = previewHeight * aspect;
                previewWidth = std::max(1.0f, previewWidth);
                previewHeight = std::max(1.0f, previewHeight);

                // Cria um child para delimitar a area e evitar vazamento
                ImGui::BeginChild("CameraPreviewContainer", ImVec2(previewWidth, previewHeight), false);
                {
                    // Centraliza a imagem dentro do child
                    ImVec2 availChild = ImGui::GetContentRegionAvail();
                    float offsetX = (availChild.x - previewWidth) * 0.5f;
                    float offsetY = (availChild.y - previewHeight) * 0.5f;
                    if (offsetX > 0) ImGui::SetCursorPosX(offsetX);
                    if (offsetY > 0) ImGui::SetCursorPosY(offsetY);

                    uint32_t texID = RenderCameraPreview(m_Ctx.SelectedEntity, previewWidth, previewHeight);
                    if (texID != 0) {
                        ImGui::Image((ImTextureID)(uintptr_t)texID, ImVec2(previewWidth, previewHeight),
                            ImVec2(0, 1), ImVec2(1, 0));
                    }
                    else {
                        ImGui::TextDisabled("Falha ao renderizar preview");
                    }
                }
                ImGui::EndChild();
            }
            if (!keepOpen)
                m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::CameraComponent>>(m_Ctx.SelectedEntity, "Camera"));
        }
        // ==================== FIM SEÇÃO CAMERA ==========================

        if (m_Ctx.SelectedEntity.HasComponent<Prism::ScriptComponent>()) {
            auto& script = m_Ctx.SelectedEntity.GetComponent<Prism::ScriptComponent>();
            bool keepOpen = true;
            if (ImGui::CollapsingHeader("Script", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                // Combo com os .lua ja existentes em Scripts/ - evita ter
                // que saber/digitar o nome de cor (ver ListProjectScripts).
                // "(nenhum)" e sempre a primeira opcao, para poder limpar
                // ScriptPath sem sair do combo.
                std::vector<std::string> availableScripts = ListProjectScripts();

                std::string previewLabel = script.ScriptPath.empty() ? "(nenhum)" : script.ScriptPath;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##ScriptSelect", previewLabel.c_str())) {
                    bool noneSelected = script.ScriptPath.empty();
                    if (ImGui::Selectable("(nenhum)", noneSelected))
                        script.ScriptPath.clear();

                    for (const auto& scriptFile : availableScripts) {
                        bool selected = (script.ScriptPath == scriptFile);
                        if (ImGui::Selectable(scriptFile.c_str(), selected))
                            script.ScriptPath = scriptFile;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }

                    if (availableScripts.empty())
                        ImGui::TextDisabled("Nenhum .lua em Scripts/ ainda.");

                    ImGui::EndCombo();
                }

                // "Novo..." abre o popup que cria o arquivo (ver
                // RenderNewScriptPopup/CreateNewScript) e ja atribui a esta
                // entidade. "Editar" so aparece com um script ja escolhido -
                // abre o ScriptEditorPanel nele (ver Panels/ScriptEditorPanel.h).
                if (ImGui::Button("Novo...")) {
                    m_NewScriptNameBuffer[0] = '\0';
                    m_ShowNewScriptPopup = true;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(script.ScriptPath.empty());
                if (ImGui::Button("Editar")) {
                    auto project = Prism::Project::GetActive();
                    if (project)
                        m_Ctx.ScriptEditor->Open(project->GetScriptDirectory() / script.ScriptPath);
                }
                ImGui::EndDisabled();

                if (script.ScriptPath.empty()) {
                    ImGui::TextDisabled("Nenhum arquivo escolhido ainda.");
                }
                else if (m_Ctx.Play->IsOpen()) {
                    // A PlayWindow roda uma COPIA clonada da Scene (ver
                    // Play/PlayWindow.h) - EditorContext::SelectedEntity pertence a
                    // Scene de EDICAO, uma entidade DIFERENTE (ainda que
                    // correspondente) da que esta rodando de verdade la
                    // dentro. Nao ha como "recarregar" um script individual
                    // remotamente na PlayWindow a partir daqui - o jeito de
                    // aplicar uma mudanca no arquivo .lua e Parar e apertar
                    // Play de novo (que clona a Scene do zero, incluindo o
                    // arquivo .lua atualizado do disco).
                    ImGui::TextDisabled("Play em andamento - Pare e aperte Play de novo para recarregar.");
                }
                else {
                    ImGui::TextDisabled("Aperte Play (menu bar) para rodar este script.");
                }
            }
            if (!keepOpen)
                m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::ScriptComponent>>(m_Ctx.SelectedEntity, "Script"));
        }

        ImGui::Dummy(ImVec2(0, 8));
        RenderAddComponentButton();

        ImGui::Separator();
        ImGui::TextDisabled("Camera do editor (livre)");
        ImGui::Text("Posicao: (%.2f, %.2f, %.2f)", m_Ctx.Camera.Position.x, m_Ctx.Camera.Position.y, m_Ctx.Camera.Position.z);
        ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_Ctx.Camera.Yaw, m_Ctx.Camera.Pitch);
        ImGui::Text("Velocidade: %.2f u/s", m_Ctx.Camera.MoveSpeed);
        if (m_Ctx.Camera.LookActive)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "MODO VOAR ATIVO (solte o RMB para sair)");
        else
            ImGui::TextDisabled("RMB: modo voar (WASD/QE, Shift=boost, Alt=slow, scroll=velocidade)");
        ImGui::TextDisabled("Fora do voo: scroll sobre a viewport = dolly (avanca/recua)");

        ImGui::End();
    }

    void PropertiesPanel::RenderAddComponentButton() {
        // Garante que o registro esta populado - ver comentario
        // equivalente em SceneSerializer::Serialize/Deserialize
        // (ComponentRegistry::RegisterAll e idempotente).
        Prism::ComponentRegistry::RegisterAll();
        const auto& registry = Prism::ComponentRegistry::GetAll();

        // So mostra o botao se sobrar pelo menos um component que a
        // entidade ainda nao tem - evita um popup vazio (a entidade ja
        // tem TransformComponent sempre, que nunca esta neste registro -
        // ver comentario em ComponentRegistration.cpp sobre o motivo).
        bool hasAnyMissing = false;
        for (auto& info : registry) {
            if (!info.Has(m_Ctx.SelectedEntity)) {
                hasAnyMissing = true;
                break;
            }
        }

        if (!hasAnyMissing) {
            ImGui::TextDisabled("(todos os components ja adicionados)");
            return;
        }

        if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup")) {
            // Loop generico sobre TODO Component registrado (ver
            // ComponentRegistry.h/ComponentRegistration.cpp). O Command real (com Undo/Redo, ver
            // AddComponentCommand<T>/EditorCommands.h) ainda e criado por
            // TIPO (nao generico) via AddComponentByRegistryName abaixo,
            // ja que o registro em si (Prism::ComponentRegistry) nao
            // conhece EditorCommands (que vive em PrismEditor, nao em
            // Prism) - ver comentario la sobre essa separacao proposital.
            for (auto& info : registry) {
                if (!info.Has(m_Ctx.SelectedEntity) && ImGui::MenuItem(info.DisplayName.c_str())) {
                    AddComponentByRegistryName(info.DisplayName);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // Instancia o AddComponentCommand<T> certo a partir do DisplayName
    // registrado em ComponentRegistry (ver ComponentRegistration.cpp) e
    // executa via EditorContext::History (com Undo/Redo).
    //
    // POR QUE ISTO NAO VIVE DENTRO DE ComponentRegistry (Prism::): porque
    // AddComponentCommand<T> e Command (Prism::Core::Command) sao dois
    // conceitos DIFERENTES - Command fica em Prism (o motor), mas
    // AddComponentCommand/RemoveComponentCommand especificamente ficam em
    // PrismEditor (EditorCommands.h), ja que "ter undo/redo ao adicionar
    // um component" e uma preocupacao do EDITOR, nao do motor em si (um
    // jogo em modo Runtime, sem editor, nunca precisaria disso). Colocar
    // esta comparacao de string aqui (em vez de um std::function dentro
    // de ComponentTypeInfo) evita que Prism::ComponentRegistry precise
    // #include EditorCommands.h - Prism nunca deveria depender de codigo
    // do Editor.
    //
    // Isto tambem e o UNICO lugar que ainda precisa saber, um por um,
    // quais Components existem - mas apenas para a etapa de "criar o
    // Command certo", nao mais para decidir SE o component deve aparecer
    // no menu ou como serializa-lo (isso already vem do registro). Um
    // Component novo que nao seja adicionado aqui simplesmente nao tera
    // Undo ao ser adicionado - ainda funciona (AddDefault do registro e
    // usado como fallback abaixo), so sem desfazer.
    void PropertiesPanel::AddComponentByRegistryName(const std::string& displayName) {
        Prism::Entity entity = m_Ctx.SelectedEntity;

        if (displayName == "Mesh Renderer")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::MeshRendererComponent>>(entity, displayName));
        else if (displayName == "Light")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::LightComponent>>(entity, displayName));
        else if (displayName == "Collider")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::ColliderComponent>>(entity, displayName));
        else if (displayName == "Rigid Body")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::RigidBodyComponent>>(entity, displayName));
        else if (displayName == "Raycast")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::RaycastComponent>>(entity, displayName));
        else if (displayName == "Script")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::ScriptComponent>>(entity, displayName));
        else if (displayName == "Camera")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::CameraComponent>>(entity, displayName));
        else if (displayName == "Material")
            m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::MaterialComponent>>(entity, displayName));
        else {
            // Fallback generico SEM Undo - usado so se um Component for
            // registrado em ComponentRegistration.cpp mas esquecido aqui
            // (ver comentario grande acima) - melhor funcionar sem
            // desfazer do que nao adicionar nada.
            PRISM_CORE_WARN("PropertiesPanel::AddComponentByRegistryName: '", displayName, "' nao tem um AddComponentCommand mapeado - adicionando sem Undo.");
            for (auto& info : Prism::ComponentRegistry::GetAll()) {
                if (info.DisplayName == displayName) {
                    info.AddDefault(entity);
                    break;
                }
            }
        }

        // OnAfterAddInEditor (ver ComponentRegistry.h) - cobre ajustes de
        // consistencia que dependem do RESTO da cena, como o caso de
        // CameraComponent::Primary (ver ComponentRegistration.cpp) - FORA
        // do historico de Undo.
        for (auto& info : Prism::ComponentRegistry::GetAll()) {
            if (info.DisplayName == displayName && info.OnAfterAddInEditor) {
                info.OnAfterAddInEditor(entity, *m_Ctx.ActiveScene);
                break;
            }
        }
    }

    uint32_t PropertiesPanel::RenderCameraPreview(Prism::Entity cameraEntity, float width, float height) {
        if (!cameraEntity || !cameraEntity.HasComponent<Prism::CameraComponent>())
            return 0;

        // Cria o framebuffer sob demanda
        if (!m_CameraPreviewFramebuffer) {
            Prism::FramebufferSpecification fbSpec;
            fbSpec.Width = (uint32_t)std::max(width, 1.0f);
            fbSpec.Height = (uint32_t)std::max(height, 1.0f);
            m_CameraPreviewFramebuffer = Prism::Framebuffer::Create(fbSpec);
        }

        // Redimensiona se necessário
        const auto& spec = m_CameraPreviewFramebuffer->GetSpecification();
        uint32_t w = (uint32_t)std::max(width, 1.0f);
        uint32_t h = (uint32_t)std::max(height, 1.0f);
        if (spec.Width != w || spec.Height != h) {
            m_CameraPreviewFramebuffer->Resize(w, h);
        }

        m_CameraPreviewFramebuffer->Bind();
        Prism::Renderer::Clear(0.05f, 0.05f, 0.07f, 1.0f);

        float aspect = h > 0 ? (float)w / (float)h : 1.0f;
        auto& camera = cameraEntity.GetComponent<Prism::CameraComponent>();
        glm::mat4 worldTransform = m_Ctx.ActiveScene->GetWorldTransform(cameraEntity);
        glm::mat4 view = glm::inverse(worldTransform);
        glm::mat4 projection = camera.GetProjection(aspect);
        glm::vec3 worldPos = glm::vec3(worldTransform[3]);

        Prism::Renderer::DrawScene(*m_Ctx.ActiveScene, glm::value_ptr(view), glm::value_ptr(projection), glm::value_ptr(worldPos));

        m_CameraPreviewFramebuffer->Unbind();
        return m_CameraPreviewFramebuffer->GetColorAttachmentID();
    }

    void PropertiesPanel::RenderSaveMaterialPopup() {
        if (m_ShowSaveMaterialPopup) {
            ImGui::OpenPopup(kSaveMaterialPopupId);
            m_ShowSaveMaterialPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kSaveMaterialPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!m_Ctx.SelectedEntity || !m_Ctx.SelectedEntity.HasComponent<Prism::MaterialComponent>()) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Nenhuma entidade com Material selecionada.");
                ImGui::Dummy(ImVec2(0, 8));
                if (ImGui::Button("Fechar", ImVec2(120, 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            ImGui::TextWrapped("Nome do material:");
            ImGui::SetNextItemWidth(-1);
            bool confirmedByEnter = ImGui::InputText("##SaveMaterialName", m_SaveMaterialNameBuffer, sizeof(m_SaveMaterialNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            auto project = Prism::Project::GetActive();
            std::string name = m_SaveMaterialNameBuffer;

            static const std::string kForbiddenChars = "/\\:*?\"<>|";
            for (auto& c : name)
                if (kForbiddenChars.find(c) != std::string::npos)
                    c = '_';

            bool nameEmpty = name.empty();
            std::filesystem::path previewPath = project->GetMaterialDirectory() / (name + ".prismmat");
            bool wouldOverwrite = !nameEmpty && std::filesystem::exists(previewPath);

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty)
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o material.");
            else if (wouldOverwrite)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "Ja existe um material com este nome - sera sobrescrito.");
            else
                ImGui::TextDisabled("%s", previewPath.filename().string().c_str());

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Salvar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty;

            if (confirmed) {
                auto& material = m_Ctx.SelectedEntity.GetComponent<Prism::MaterialComponent>();
                if (!Prism::MaterialSerializer::Serialize(material, previewPath))
                    PRISM_ERROR("Falha ao salvar material '", name, "' - ver console para detalhes.");
                else if (project) {
                    // O arquivo recem-criado (ou sobrescrito) ainda nao
                    // tem AssetID nenhum atribuido enquanto o
                    // AssetRegistry nao varrer de novo (ver
                    // Assets/AssetRegistry.h) - sem isto, o usuario
                    // arrastaria este .prismmat para vincular a outra
                    // entidade (fluxo natural logo apos salvar) e
                    // LoadMaterialAssetCommand::ResolveAssetID falharia
                    // silenciosamente (o material carregaria os campos
                    // mas SEM vinculo), ate um "Atualizar" manual no
                    // Content Browser. Refresh() e idempotente e barato
                    // (ver AssetRegistry::Refresh) - seguro de chamar
                    // aqui toda vez.
                    project->GetAssetRegistry().Refresh();
                    m_Ctx.ContentBrowser->RefreshEntries();
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void PropertiesPanel::RenderNewScriptPopup() {
        if (m_ShowNewScriptPopup) {
            ImGui::OpenPopup(kNewScriptPopupId);
            m_ShowNewScriptPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kNewScriptPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Nome do script (sem extensao):");
            ImGui::SetNextItemWidth(-1);

            bool confirmedByEnter = ImGui::InputText("##NewScriptName", m_NewScriptNameBuffer, sizeof(m_NewScriptNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            std::string name = m_NewScriptNameBuffer;
            bool nameEmpty = name.empty();

            auto project = Prism::Project::GetActive();
            std::filesystem::path previewPath = project ? (project->GetScriptDirectory() / (name + ".lua")) : std::filesystem::path{};
            bool wouldOverwrite = !nameEmpty && project && std::filesystem::exists(previewPath);

            ImGui::Dummy(ImVec2(0, 4));
            if (nameEmpty) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Digite um nome para o script.");
            }
            else if (wouldOverwrite) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Ja existe um script com este nome.");
            }
            else {
                ImGui::TextDisabled("%s", (name + ".lua").c_str());
            }

            ImGui::Dummy(ImVec2(0, 8));

            bool confirmedByButton = ImGui::Button("Criar", ImVec2(120, 0));
            ImGui::SameLine();
            bool cancelled = ImGui::Button("Cancelar", ImVec2(120, 0));

            bool confirmed = (confirmedByEnter || confirmedByButton) && !nameEmpty && !wouldOverwrite;

            if (confirmed) {
                std::filesystem::path relativePath = CreateNewScript(name);
                if (!relativePath.empty() && m_Ctx.SelectedEntity && m_Ctx.SelectedEntity.HasComponent<Prism::ScriptComponent>()) {
                    // Atribui o script recem-criado diretamente ao
                    // ScriptComponent da entidade selecionada (a mesma que
                    // tinha o botao "Novo..." clicado - ver
                    // RenderPropertiesPanel) e ja abre no editor, poupando
                    // o usuario de digitar o nome de novo no combo.
                    m_Ctx.SelectedEntity.GetComponent<Prism::ScriptComponent>().ScriptPath = relativePath.string();
                    m_Ctx.ScriptEditor->Open(project->GetScriptDirectory() / relativePath);
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    std::filesystem::path PropertiesPanel::CreateNewScript(const std::string& name) {
        auto project = Prism::Project::GetActive();
        if (!project || name.empty())
            return {};

        // Mesma sanitizacao ja usada em RenderSaveAsPopup - nomes de
        // arquivo nao devem conter caracteres proibidos pelo SO.
        std::string safeName = name;
        static const std::string kForbiddenChars = "/\\:*?\"<>|";
        for (auto& c : safeName)
            if (kForbiddenChars.find(c) != std::string::npos)
                c = '_';

        std::filesystem::path relativePath = safeName + ".lua";
        std::filesystem::path absolutePath = project->GetScriptDirectory() / relativePath;

        if (std::filesystem::exists(absolutePath)) {
            PRISM_CORE_ERROR("CreateNewScript: ja existe um script chamado '", relativePath.string(), "'.");
            return {};
        }

        // Template minimo - os tres callbacks especiais comentados, mesmo
        // vocabulario/formato dos exemplos em
        // PrismEditor/assets/ScriptExamples/ (ver example_spin.lua) - assim
        // um script novo ja mostra a API basica sem o usuario precisar ir
        // procurar um exemplo em outro lugar.
        std::ofstream file(absolutePath);
        if (!file.is_open()) {
            PRISM_CORE_ERROR("CreateNewScript: falha ao criar '", absolutePath.string(), "'.");
            return {};
        }

        file <<
            "-- " << relativePath.string() << "\n"
            "-- Tres funcoes especiais, TODAS opcionais (defina so as que precisar):\n"
            "--   OnCreate()            -- chamada uma vez, quando o Play comeca\n"
            "--   OnUpdate(deltaTime)   -- chamada toda frame enquanto o Play estiver ligado\n"
            "--   OnDestroy()           -- chamada uma vez quando o Play para\n"
            "--\n"
            "-- A variavel global `entity` ja existe dentro do script - e a PROPRIA\n"
            "-- entidade dona deste ScriptComponent.\n"
            "\n"
            "function OnCreate()\n"
            "    log(entity:GetName() .. \": OnCreate\")\n"
            "end\n"
            "\n"
            "function OnUpdate(deltaTime)\n"
            "\n"
            "end\n"
            "\n"
            "function OnDestroy()\n"
            "\n"
            "end\n";
        file.close();

        PRISM_CORE_INFO("CreateNewScript: criado '", absolutePath.string(), "'.");
        return relativePath;
    }

    std::vector<std::string> PropertiesPanel::ListProjectScripts() const {
        std::vector<std::string> result;

        auto project = Prism::Project::GetActive();
        if (!project)
            return result;

        std::error_code ec;
        std::filesystem::path scriptDir = project->GetScriptDirectory();
        if (!std::filesystem::exists(scriptDir, ec))
            return result;

        // NAO recursivo (ver comentario no header) - so arquivos .lua
        // diretamente dentro de Scripts/, comparado case-insensitive para
        // aceitar ".LUA"/".Lua" tambem (extensao pode vir de qualquer jeito
        // dependendo de como o arquivo foi criado fora do editor).
        for (const auto& entry : std::filesystem::directory_iterator(scriptDir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file())
                continue;

            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".lua")
                continue;

            result.push_back(entry.path().filename().string());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

}
