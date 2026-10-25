#include "PropertiesPanel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "ContentBrowserPanel.h"
#include "ScriptEditorPanel.h"
#include "../Play/PlayWindow.h"
#include "../Commands/EditorCommands.h"
#include "../Core/EntityOps.h"
#include "ComponentEditUtils.h"
#include "LayerMaskWidget.h"
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

// PropertiesPanel_Camera.cpp
// UI do CameraComponent no painel Propriedades. Extraido de PropertiesPanel.cpp sem
// mudar comportamento: DrawCameraUI() e chamada por OnImGuiRender() quando a entidade
// selecionada tem o component.

namespace PrismEditor {

    void PropertiesPanel::DrawCameraUI() {
        auto& camera = m_Ctx.SelectedEntity.GetComponent<Prism::CameraComponent>();
        bool keepOpen = true;
        if (ImGui::CollapsingHeader("Camera", &keepOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* projectionNames[] = { "Perspectiva", "Ortografica" };
            const Prism::CameraComponent before = camera; // ver ComponentEditUtils.h
            int projectionIndex = (int)camera.ProjectionType;
            if (ImGui::Combo("Projecao", &projectionIndex, projectionNames, IM_ARRAYSIZE(projectionNames))) {
                camera.ProjectionType = (Prism::CameraProjectionType)projectionIndex;
                CommitComponentEdit(m_Ctx, before, camera, "Camera");
            }

            if (camera.ProjectionType == Prism::CameraProjectionType::Perspective) {
                ImGui::DragFloat("FOV", &camera.FOV, 0.5f, 1.0f, 179.0f);
                TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
            }
            else {
                ImGui::DragFloat("Tamanho Ortografico", &camera.OrthoSize, 0.1f, 0.01f, 1000.0f);
                TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
            }

            ImGui::DragFloat("Near Clip", &camera.NearClip, 0.01f, 0.001f, camera.FarClip - 0.01f);
            TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
            ImGui::DragFloat("Far Clip", &camera.FarClip, 1.0f, camera.NearClip + 0.01f, 100000.0f);
            TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");

            bool isPrimary = camera.Primary;
            if (ImGui::Checkbox("Primary", &isPrimary)) {
                if (isPrimary) {
                    // Desmarca as outras cameras tambem - o comando guarda o
                    // Primary de todas para o Undo.
                    m_Ctx.History.Execute(Prism::CreateScope<SetPrimaryCameraCommand>(m_Ctx.ActiveScene.get(), m_Ctx.SelectedEntity));
                }
                else {
                    camera.Primary = false;
                    CommitComponentEdit(m_Ctx, before, camera, "Camera");
                }
            }
            if (!isPrimary)
                ImGui::TextDisabled("Nao e a camera principal - o modo Play/viewport nao vai usar esta.");

            ImGui::SeparatorText("Render");
            const char* clearNames[] = { "Skybox da cena", "Cor chapada" };
            int clearIndex = (int)camera.ClearMode;
            if (ImGui::Combo("Fundo##Cam", &clearIndex, clearNames, IM_ARRAYSIZE(clearNames))) {
                camera.ClearMode = (Prism::CameraClearMode)clearIndex;
                CommitComponentEdit(m_Ctx, before, camera, "Camera");
            }
            if (camera.ClearMode == Prism::CameraClearMode::SolidColor) {
                ImGui::ColorEdit3("Cor de fundo##Cam", glm::value_ptr(camera.ClearColor));
                TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
            }
            ImGui::DragFloat("Exposicao##Cam", &camera.Exposure, 0.01f, 0.05f, 16.0f);
            TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
            bool ssao = camera.SSAO;
            if (ImGui::Checkbox("Oclusao de ambiente (SSAO)##Cam", &ssao)) {
                camera.SSAO = ssao;
                CommitComponentEdit(m_Ctx, before, camera, "Camera");
            }

            ImGui::SeparatorText("Pos-processamento");
            bool bloom = camera.Bloom;
            if (ImGui::Checkbox("Bloom##Cam", &bloom)) {
                camera.Bloom = bloom;
                CommitComponentEdit(m_Ctx, before, camera, "Camera");
            }
            if (camera.Bloom) {
                ImGui::DragFloat("Limiar do bloom##Cam", &camera.BloomThreshold, 0.01f, 0.0f, 32.0f);
                TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
                ImGui::DragFloat("Intensidade do bloom##Cam", &camera.BloomIntensity, 0.005f, 0.0f, 4.0f);
                TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
            }
            bool fxaa = camera.FXAA;
            if (ImGui::Checkbox("FXAA (suavizar bordas)##Cam", &fxaa)) {
                camera.FXAA = fxaa;
                CommitComponentEdit(m_Ctx, before, camera, "Camera");
            }
            ImGui::DragFloat("Saturacao##Cam", &camera.Saturation, 0.01f, 0.0f, 3.0f);
            TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");
            ImGui::DragFloat("Contraste##Cam", &camera.Contrast, 0.01f, 0.1f, 3.0f);
            TrackContinuousEdit(m_Ctx, m_CameraBeforeEdit, camera, "Camera");

            // Camadas que esta camera enxerga (Mesh Renderer > Camadas).
            {
                auto beforeMask = camera;
                if (DrawLayerMask("Enxerga camadas##Cam", camera.CullMask))
                    CommitComponentEdit(m_Ctx, beforeMask, camera, "Camera");
            }

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

}
