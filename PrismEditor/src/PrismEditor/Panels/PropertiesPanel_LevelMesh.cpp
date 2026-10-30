#include "PropertiesPanel.h"
#include <imgui.h>
#include "../Commands/EditorCommands.h"
#include "../Core/LevelEditing.h"
#include "ComponentEditUtils.h"
#include "LayerMaskWidget.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

// ============================================================================
// Secao "Level Mesh" do painel Propriedades (Level Builder): estado da malha, botoes de edicao
// (extrudar, inset, subdividir...), propriedades das faces selecionadas (slot de material, UV,
// suavizacao), editor dos materiais POR SLOT, camadas/sombra e atalho para adicionar colisao.
// Cada acao e UM passo de undo. A interacao na viewport esta em LevelEditTool.cpp.
// ============================================================================

namespace PrismEditor {

    namespace {
        // "Antes" do gesto de um widget continuo (arrastar/escrever): capturado na ativacao.
        Prism::LevelMeshComponent s_BeforeGesture;
    }

    void PropertiesPanel::DrawLevelMeshUI() {
        using namespace LevelEditing;
        auto& level = m_Ctx.SelectedEntity.GetComponent<Prism::LevelMeshComponent>();
        LevelEditState& state = m_Ctx.Level;
        bool keepOpen = true;

        if (ImGui::CollapsingHeader("Level Mesh", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            Prism::Level::EditableMesh& mesh = level.Mesh;
            std::string invalidReason;
            const bool valid = mesh.IsValid(&invalidReason);
            ImGui::TextDisabled("%zu vertices, %zu faces, %u slot(s) de material - %s",
                                mesh.Vertices.size(), mesh.Faces.size(), level.SlotCountUsed(),
                                mesh.IsClosed() ? "malha fechada" : "malha aberta");
            if (!valid)
                ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "Malha invalida: %s", invalidReason.c_str());

            // Gesto de widget continuo sobre a GEOMETRIA (UV): o widget edita uma variavel local, entao 'level'
            // ainda esta intacto na ativacao. Um undo por gesto (ao soltar).
            auto track = [&](bool changed, const char* undoName, auto&& apply) {
                if (ImGui::IsItemActivated())
                    s_BeforeGesture = level;
                if (changed) {
                    apply();
                    level.Touch();
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    CommitComponentEdit(m_Ctx, s_BeforeGesture, level, undoName);
            };
            // Mesmo para widgets que editam o MATERIAL no proprio lugar (ja mudou no quadro da ativacao,
            // por isso o "antes" vem da copia dos materiais tirada no inicio do quadro). Nao mexe na
            // geometria, entao nao invalida o cache de GPU.
            const std::vector<Prism::MaterialComponent> materialsAtFrameStart = level.Materials;
            auto trackMaterial = [&](const char* undoName) {
                if (ImGui::IsItemActivated()) {
                    s_BeforeGesture = level;
                    s_BeforeGesture.Materials = materialsAtFrameStart;
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    CommitComponentEdit(m_Ctx, s_BeforeGesture, level, undoName);
            };
            // Mudanca discreta (combo, botao, soltar arquivo): um passo de undo na hora.
            auto discrete = [&](const char* undoName, auto&& apply) {
                const Prism::LevelMeshComponent before = level;
                apply();
                level.Touch();
                CommitComponentEdit(m_Ctx, before, level, undoName);
            };

            const bool editing = Sync(m_Ctx) != nullptr;
            if (ImGui::Button(editing ? "Sair da edicao (Tab)" : "Editar geometria (Tab)", ImVec2(-1, 0)))
                SetActive(m_Ctx, !editing);

            if (editing) {
                ImGui::SeparatorText("Selecao");
                const struct { const char* Label; LevelEditState::Mode M; } modes[] = {
                    { "Vertice", LevelEditState::Mode::Vertex }, { "Aresta", LevelEditState::Mode::Edge }, { "Face", LevelEditState::Mode::Face } };
                for (int i = 0; i < 3; i++) {
                    if (i) ImGui::SameLine();
                    const bool on = state.SelectionMode == modes[i].M;
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(modes[i].Label)) SetMode(m_Ctx, modes[i].M);
                    if (on) ImGui::PopStyleColor();
                }
                if (ImGui::Button("Todos")) SelectAll(m_Ctx);
                ImGui::SameLine();
                if (ImGui::Button("Nenhum")) ClearSelection(m_Ctx);
                ImGui::SameLine();
                if (ImGui::Button("Inverter selecao")) InvertSelection(m_Ctx);

                ImGui::Checkbox("Snap de grade (Ctrl inverte)", &state.SnapEnabled);
                ImGui::SetNextItemWidth(100.0f);
                ImGui::DragFloat("Tamanho da grade", &state.SnapSize, 0.01f, 0.01f, 100.0f, "%.2f");

                const bool hasFaces = !EffectiveFaces(level.Mesh, state).empty();
                const bool hasVertices = !EffectiveVertices(level.Mesh, state).empty();
                ImGui::TextDisabled("Arraste o gizmo para mover a selecao%s.", hasVertices ? "" : " (selecione algo na viewport)");

                ImGui::SeparatorText("Operacoes em faces");
                ImGui::BeginDisabled(!hasFaces);
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("##ExtrudeDistance", &state.ExtrudeDistance, 0.01f, -100.0f, 100.0f, "%.2f");
                ImGui::SameLine();
                if (ImGui::Button("Extrudir (Ctrl+E)")) Extrude(m_Ctx);
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("##InsetAmount", &state.InsetAmount, 0.005f, 0.001f, 100.0f, "%.3f");
                ImGui::SameLine();
                if (ImGui::Button("Inset (Ctrl+I)")) Inset(m_Ctx);
                if (ImGui::Button("Subdividir")) Subdivide(m_Ctx);
                ImGui::SameLine();
                if (ImGui::Button("Destacar")) Detach(m_Ctx);
                ImGui::SameLine();
                if (ImGui::Button("Inverter")) FlipSelectedFaces(m_Ctx);
                ImGui::SameLine();
                if (ImGui::Button("Excluir (Del)")) DeleteSelectedFaces(m_Ctx);
                ImGui::EndDisabled();
                if (!hasFaces && state.SelectionMode != LevelEditState::Mode::Face && hasVertices)
                    ImGui::TextDisabled("Nos modos Vertice/Aresta as operacoes de face usam as faces com TODOS os vertices selecionados.");

                ImGui::SeparatorText("Malha inteira");
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("##MergeEps", &state.MergeEpsilon, 0.0001f, 0.000001f, 1.0f, "%.4f");
                ImGui::SameLine();
                if (ImGui::Button("Mesclar vertices proximos")) MergeVertices(m_Ctx);
                ImGui::TextUnformatted("Espelhar:");
                for (int axis = 0; axis < 3; axis++) {
                    ImGui::SameLine();
                    char label[8];
                    std::snprintf(label, sizeof(label), "%c##Mirror", "XYZ"[axis]);
                    if (ImGui::Button(label)) MirrorWholeMesh(m_Ctx, axis);
                }
                if (ImGui::Button("Centralizar pivo")) RecenterPivot(m_Ctx);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Move a origem do objeto para o centro da malha sem mover a geometria no mundo.");

                // Os botoes acima podem ter mudado a malha neste quadro: refaz a selecao antes de ler faces.
                Sync(m_Ctx);
                const std::vector<uint32_t> selectedFaces = EffectiveFaces(level.Mesh, state);
                if (!selectedFaces.empty()) {
                    const std::vector<uint32_t>& faces = selectedFaces;
                    ImGui::SeparatorText("Faces selecionadas");
                    // Mostra os valores da primeira face (copia: os comandos abaixo reatribuem o componente);
                    // editar aplica a TODAS as selecionadas.
                    const Prism::Level::Face first = level.Mesh.Faces[faces[0]];
                    ImGui::TextDisabled("%zu face(s); valores da primeira", faces.size());

                    char slotPreview[24];
                    std::snprintf(slotPreview, sizeof(slotPreview), "Slot %u", first.MaterialSlot);
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::BeginCombo("Slot de material", slotPreview)) {
                        for (size_t slot = 0; slot < level.Materials.size(); slot++) {
                            char item[24];
                            std::snprintf(item, sizeof(item), "Slot %zu", slot);
                            if (ImGui::Selectable(item, slot == first.MaterialSlot))
                                AssignSlotToSelection(m_Ctx, (uint32_t)slot);
                        }
                        ImGui::EndCombo();
                    }

                    int group = (int)first.SmoothingGroup;
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::InputInt("Grupo de suavizacao", &group, 1, 1) && group >= 0)
                        SetSmoothingOnSelection(m_Ctx, (uint32_t)group);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("0 = faceta (sombreamento chapado). Faces vizinhas com o mesmo numero != 0 ficam suaves.");

                    Prism::Level::FaceUV uv = first.UV;
                    auto applyUv = [&](auto&& change) {
                        for (uint32_t f : faces) change(level.Mesh.Faces[f].UV);
                    };
                    bool changed = ImGui::DragFloat2("Deslocamento UV", &uv.Offset.x, 0.01f);
                    track(changed, "UV da Face", [&] { applyUv([&](Prism::Level::FaceUV& t) { t.Offset = uv.Offset; }); });
                    changed = ImGui::DragFloat2("Repeticao UV", &uv.Tiling.x, 0.01f, -100.0f, 100.0f);
                    track(changed, "UV da Face", [&] { applyUv([&](Prism::Level::FaceUV& t) { t.Tiling = uv.Tiling; }); });
                    changed = ImGui::DragFloat("Rotacao UV", &uv.RotationDeg, 0.5f, -360.0f, 360.0f, "%.1f graus");
                    track(changed, "UV da Face", [&] { applyUv([&](Prism::Level::FaceUV& t) { t.RotationDeg = uv.RotationDeg; }); });
                    changed = ImGui::Checkbox("Inverter U", &uv.FlipU);
                    track(changed, "UV da Face", [&] { applyUv([&](Prism::Level::FaceUV& t) { t.FlipU = uv.FlipU; }); });
                    ImGui::SameLine();
                    changed = ImGui::Checkbox("Inverter V", &uv.FlipV);
                    track(changed, "UV da Face", [&] { applyUv([&](Prism::Level::FaceUV& t) { t.FlipV = uv.FlipV; }); });
                    if (ImGui::SmallButton("Resetar UV")) {
                        discrete("Resetar UV", [&] { applyUv([](Prism::Level::FaceUV& t) { t = Prism::Level::FaceUV{}; }); });
                    }
                }
            }

            ImGui::SeparatorText("Renderizacao");
            {
                const Prism::LevelMeshComponent before = level;
                if (DrawLayerMask("Camadas##LevelMesh", level.Layers))
                    CommitComponentEdit(m_Ctx, before, level, "Level Mesh");
            }
            {
                bool cast = level.CastShadows;
                if (ImGui::Checkbox("Projeta sombra", &cast))
                    discrete("Level Mesh", [&] { level.CastShadows = cast; });
            }

            ImGui::SeparatorText("Materiais por slot");
            ImGui::TextDisabled("O Material da propria entidade e ignorado: os materiais ficam aqui, um por slot.");
            std::vector<uint32_t> facesPerSlot(level.Materials.size() + 1, 0);
            for (const auto& face : level.Mesh.Faces)
                facesPerSlot[std::min<size_t>(face.MaterialSlot, level.Materials.size())]++;

            auto textureField = [&](const char* label, std::string& path) {
                ImGui::PushID(label);
                ImGui::TextUnformatted(label);
                ImGui::SameLine(110.0f);
                const std::string shown = path.empty() ? std::string("(arraste uma imagem)") : std::filesystem::path(path).filename().string();
                ImGui::Button(shown.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 28.0f, 0));
                if (ImGui::IsItemHovered() && !path.empty())
                    ImGui::SetTooltip("%s", path.c_str());
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_IMAGE_PATH")) {
                        std::filesystem::path dropped((const char*)payload->Data);
                        std::string stored = dropped.generic_string();
                        if (auto project = Prism::Project::GetActive()) {
                            std::error_code ec;
                            auto relative = std::filesystem::relative(dropped, project->GetProjectDirectory(), ec);
                            if (!ec) stored = relative.generic_string();
                        }
                        discrete("Textura do Slot", [&] { path = stored; });
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x") && !path.empty())
                    discrete("Textura do Slot", [&] { path.clear(); });
                ImGui::PopID();
            };

            for (size_t i = 0; i < level.Materials.size(); i++) {
                Prism::MaterialComponent& material = level.Materials[i];
                ImGui::PushID((int)i);
                char header[64];
                std::snprintf(header, sizeof(header), "Slot %zu  (%u faces)###slot", i, facesPerSlot[i]);
                if (ImGui::TreeNodeEx(header, i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                    ImGui::ColorEdit3("Cor", &material.AlbedoTint.x);
                    trackMaterial("Material do Slot");
                    textureField("Albedo", material.AlbedoPath);
                    textureField("Normal", material.NormalPath);
                    textureField("Rugosidade/Metal", material.RoughnessMetallicPath);
                    ImGui::SliderFloat("Rugosidade", &material.RoughnessFactor, 0.0f, 1.0f);
                    trackMaterial("Material do Slot");
                    ImGui::SliderFloat("Metalico", &material.MetallicFactor, 0.0f, 1.0f);
                    trackMaterial("Material do Slot");
                    ImGui::DragFloat2("Repeticao (material)", &material.UVTiling.x, 0.01f, -1000.0f, 1000.0f);
                    trackMaterial("Material do Slot");
                    ImGui::ColorEdit3("Emissao", &material.EmissiveColor.x);
                    trackMaterial("Material do Slot");
                    ImGui::DragFloat("Intensidade emissiva", &material.EmissiveIntensity, 0.05f, 0.0f, 100.0f);
                    trackMaterial("Material do Slot");
                    const char* blendNames[] = { "Opaco", "Recorte (Mask)", "Translucido (Blend)" };
                    int blend = (int)material.BlendMode;
                    if (ImGui::Combo("Transparencia", &blend, blendNames, 3))
                        discrete("Material do Slot", [&] { material.BlendMode = (Prism::MaterialBlendMode)blend; });
                    if (material.BlendMode != Prism::MaterialBlendMode::Opaque) {
                        ImGui::SliderFloat("Opacidade", &material.Opacity, 0.0f, 1.0f);
                        trackMaterial("Material do Slot");
                    }
                    bool doubleSided = material.DoubleSided;
                    if (ImGui::Checkbox("Dupla face", &doubleSided))
                        discrete("Material do Slot", [&] { material.DoubleSided = doubleSided; });
                    if (editing && !EffectiveFaces(level.Mesh, state).empty() && ImGui::Button("Atribuir as faces selecionadas"))
                        AssignSlotToSelection(m_Ctx, (uint32_t)i);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (level.Materials.size() < 64 && ImGui::Button("+ Novo slot de material")) {
                if (editing)
                    AddMaterialSlot(m_Ctx);
                else
                    discrete("Novo Slot de Material", [&] { level.Materials.push_back(level.Materials.empty() ? Prism::MaterialComponent{} : level.Materials.back()); });
            }

            ImGui::SeparatorText("Colisao");
            const bool hasCollider = m_Ctx.SelectedEntity.HasComponent<Prism::ColliderComponent>();
            if (!hasCollider) {
                if (ImGui::Button("Adicionar colisao (malha exata, estatica)", ImVec2(-1, 0))) {
                    Prism::Entity entity = m_Ctx.SelectedEntity;
                    m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::ColliderComponent>>(entity, "Collider"));
                    Prism::ColliderComponent colliderBefore = entity.GetComponent<Prism::ColliderComponent>();
                    Prism::ColliderComponent colliderAfter = colliderBefore;
                    colliderAfter.Shape = Prism::ColliderShape::TriangleMesh;
                    m_Ctx.History.Execute(Prism::CreateScope<EditComponentCommand<Prism::ColliderComponent>>(entity, colliderBefore, colliderAfter, "Collider"));
                    if (!entity.HasComponent<Prism::RigidBodyComponent>()) {
                        m_Ctx.History.Execute(Prism::CreateScope<AddComponentCommand<Prism::RigidBodyComponent>>(entity, "Rigid Body"));
                        Prism::RigidBodyComponent bodyBefore = entity.GetComponent<Prism::RigidBodyComponent>();
                        Prism::RigidBodyComponent bodyAfter = bodyBefore;
                        bodyAfter.Type = Prism::BodyType::Static;
                        m_Ctx.History.Execute(Prism::CreateScope<EditComponentCommand<Prism::RigidBodyComponent>>(entity, bodyBefore, bodyAfter, "Rigid Body"));
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Adiciona Collider (TriangleMesh) + Rigid Body Estatico. A colisao usa a malha como ela esta ao entrar no Play.");
            }
            else {
                ImGui::TextDisabled("Collider presente: use Shape = TriangleMesh (exata, so corpos Static/Kinematic) ou ConvexHull.");
            }
        }

        if (!keepOpen) {
            SetActive(m_Ctx, false);
            m_Ctx.History.Execute(Prism::CreateScope<RemoveComponentCommand<Prism::LevelMeshComponent>>(m_Ctx.SelectedEntity, "Level Mesh"));
        }
    }

}
