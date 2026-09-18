#include "ContentBrowserPanel.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>

namespace PrismEditor {

    void ContentBrowserPanel::ResetToProjectRoot() {
        auto project = Prism::Project::GetActive();
        if (!project) {
            m_CurrentDirectory.clear();
            m_CachedEntries.clear();
            return;
        }
        NavigateTo(project->GetProjectDirectory());
    }

    void ContentBrowserPanel::NavigateTo(const std::filesystem::path& directory) {
        m_CurrentDirectory = directory;
        m_SelectedPath.clear();
        RefreshEntries();
    }

    void ContentBrowserPanel::RefreshEntries() {
        m_CachedEntries.clear();

        std::error_code ec;
        if (m_CurrentDirectory.empty() || !std::filesystem::exists(m_CurrentDirectory, ec))
            return;

        for (const auto& dirEntry : std::filesystem::directory_iterator(m_CurrentDirectory, ec)) {
            if (ec) break;

            Entry entry;
            entry.Path = dirEntry.path();
            entry.Name = dirEntry.path().filename().string();
            entry.IsDirectory = dirEntry.is_directory();
            m_CachedEntries.push_back(std::move(entry));
        }

        // Pastas primeiro, depois arquivos - dentro de cada grupo, ordem
        // alfabetica (case-insensitive nao vale a pena aqui, nomes de
        // pasta/arquivo do projeto ja seguem uma convencao previsivel).
        std::sort(m_CachedEntries.begin(), m_CachedEntries.end(), [](const Entry& a, const Entry& b) {
            if (a.IsDirectory != b.IsDirectory)
                return a.IsDirectory > b.IsDirectory;
            return a.Name < b.Name;
            });
    }

    void ContentBrowserPanel::OnImGuiRender() {
        ImGui::Begin("Conteudo do Projeto");

        auto project = Prism::Project::GetActive();
        if (!project) {
            ImGui::TextDisabled("Nenhum projeto ativo.");
            ImGui::End();
            return;
        }

        // Primeira vez que este painel roda com um projeto ativo (ou depois
        // de ResetToProjectRoot nao ter sido chamado ainda por algum motivo)
        // - navega para a raiz automaticamente em vez de mostrar vazio.
        if (m_CurrentDirectory.empty())
            NavigateTo(project->GetProjectDirectory());

        RenderToolbar();
        ImGui::Separator();
        RenderGrid();

        ImGui::End();
    }

    void ContentBrowserPanel::RenderToolbar() {
        auto project = Prism::Project::GetActive();

        // Comparacao via caminhos canonicalizados (resolve diferenças de
        // barra/trailing slash entre o que veio de directory_iterator e o
        // que Project::GetProjectDirectory() retorna) - comparar
        // std::filesystem::path diretamente com == e sensivel a essas
        // diferencas de forma textual mesmo quando os caminhos apontam para
        // o mesmo lugar no disco.
        std::error_code ec;
        bool atRoot = std::filesystem::equivalent(m_CurrentDirectory, project->GetProjectDirectory(), ec) && !ec;

        ImGui::BeginDisabled(atRoot);
        if (ImGui::Button("< Voltar")) {
            NavigateTo(m_CurrentDirectory.parent_path());
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Atualizar")) {
            RefreshEntries();
        }

        ImGui::SameLine();

        // Breadcrumb: caminho relativo a raiz do projeto, com "/" como
        // separador visual - mais curto e legivel que o caminho absoluto
        // inteiro, que pode ser bem longo (C:\dev\...\MeuProjeto\Assets\Models).
        std::filesystem::path relative = std::filesystem::relative(m_CurrentDirectory, project->GetProjectDirectory());
        std::string breadcrumb = (relative == ".") ? project->GetConfig().Name : (project->GetConfig().Name + " / " + relative.generic_string());
        ImGui::TextDisabled("%s", breadcrumb.c_str());
    }

    void ContentBrowserPanel::RenderGrid() {
        // Grid simples de botoes quadrados com quebra de linha automatica -
        // ImGui nao tem um "flow layout" pronto, entao calculamos manualmente
        // quantas colunas cabem na largura disponivel do painel.
        constexpr float cellSize = 84.0f;
        constexpr float cellPadding = 8.0f;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, (int)(panelWidth / (cellSize + cellPadding)));

        if (m_CachedEntries.empty()) {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::TextDisabled("Esta pasta esta vazia.");
            return;
        }

        ImGui::Columns(columns, nullptr, false);

        //Coreção de um erro em que por algum motivo ao fechar o editor ele crasha a engine sla
        std::filesystem::path navigateToPath;
        std::filesystem::path openMapPath;

        for (const auto& entry : m_CachedEntries) {
            ImGui::PushID(entry.Path.string().c_str());

            bool isSelected = (entry.Path == m_SelectedPath);
            bool isMapFile = !entry.IsDirectory && entry.Path.extension() == ".prismmap";
            bool isProjectFile = !entry.IsDirectory && entry.Path.extension() == ".prismproj";

            // Extensoes que stb_image decodifica (ver Texture.cpp) -
            // calculado ANTES de desenhar o botao (nao so depois, como
            // antes) porque agora decide se o botao vira uma miniatura de
            // verdade (ImageButton) ou o retangulo colorido generico.
            std::string ext = entry.Path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            bool isImage = !entry.IsDirectory && (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr" || ext == ".psd" || ext == ".gif");

            // Para imagens, tenta carregar via o MESMO cache do Renderer
            // usado pelo painel de Material (Renderer::GetOrLoadTexture) -
            // nao um carregamento separado so para thumbnail: a textura
            // fica compartilhada/cacheada de qualquer forma, entao ja
            // pre-carregar aqui so adianta o trabalho que o Material
            // faria de qualquer jeito ao configurar esse path depois.
            // isSRGB=true (mesma convencao de Albedo) e so uma escolha
            // razoavel para PREVIEW - a miniatura nao faz iluminacao/PBR
            // nenhuma, entao a diferenca sRGB-vs-linear aqui e cosmetica
            // (cores um pouco mais claras/escuras que o "real"), nunca
            // incorreta a ponto de importar para so mostrar um icone.
            Prism::Texture2D* thumbnail = isImage ? Prism::Renderer::GetOrLoadTexture(entry.Path.string(), /*isSRGB*/ true) : nullptr;
            bool hasThumbnail = thumbnail && thumbnail->IsValid();

            bool clicked = false;
            if (hasThumbnail) {
                // ImageButton sozinho nao mostra o NOME do arquivo (so a
                // imagem) - desenhamos a textura como botao e o nome
                // como texto quebrado logo abaixo, dentro da mesma
                // celula (BeginGroup para os dois ficarem juntos na
                // grade de colunas).
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.15f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.25f));
                if (isSelected)
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.55f, 0.20f, 1.0f));
                clicked = ImGui::ImageButton("##thumb", (ImTextureID)(uintptr_t)thumbnail->GetRendererID(), ImVec2(cellSize - 8.0f, cellSize - 8.0f));
                if (isSelected)
                    ImGui::PopStyleColor();
                ImGui::PopStyleColor(3);
                ImGui::TextWrapped("%s", entry.Name.c_str());
                ImGui::EndGroup();
            }
            else {
                // Cor do "icone" (retangulo colorido) - usado para tudo
                // que NAO tem thumbnail de verdade ainda (pastas, mapas,
                // projeto, imagens com path quebrado, qualquer outro
                // arquivo) - pasta em azul claro, mapa em laranja (combina
                // com a cor do cubo de teste da viewport), projeto em
                // roxo, resto em cinza.
                ImVec4 iconColor = entry.IsDirectory ? ImVec4(0.45f, 0.65f, 0.90f, 1.0f)
                    : isMapFile ? ImVec4(0.85f, 0.55f, 0.20f, 1.0f)
                    : isProjectFile ? ImVec4(0.65f, 0.45f, 0.85f, 1.0f)
                    : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(iconColor.x, iconColor.y, iconColor.z, isSelected ? 0.55f : 0.25f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(iconColor.x, iconColor.y, iconColor.z, 0.45f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(iconColor.x, iconColor.y, iconColor.z, 0.65f));

                // Imagem com path quebrado (raro - arquivo corrompido ou
                // formato que stb_image nao decodifica apesar da
                // extensao) ganha um icone proprio, para distinguir de
                // "so nao tentamos gerar thumbnail para este tipo".
                const char* icon = entry.IsDirectory ? "[Pasta]" : isMapFile ? "[Mapa]" : isProjectFile ? "[Proj]" : isImage ? "[Img?]" : "[Arq]";
                std::string buttonLabel = std::string(icon) + "\n" + entry.Name;

                clicked = ImGui::Button(buttonLabel.c_str(), ImVec2(cellSize, cellSize));
                ImGui::PopStyleColor(3);
            }

            if (clicked) {
                m_SelectedPath = entry.Path;
            }

            // Fonte de drag & drop: arquivos de IMAGEM (extensoes que
            // stb_image decodifica - ver Texture.cpp) podem ser
            // arrastados para os slots de textura do painel Material
            // (ver EditorLayer::RenderPropertiesPanel, PayloadID
            // "CONTENT_BROWSER_IMAGE_PATH"). Path absoluto no payload
            // (nao relativo) - quem recebe (o slot de Material) e quem
            // decide como/se relativizar para a pasta do projeto antes
            // de salvar, mesmo padrao ja usado por
            // MaterialComponent::AlbedoPath.
            // BeginDragDropSource pode assert se o item atual nao tiver um
            // ID unico (caso raro). Passamos a flag
            // ImGuiDragDropFlags_SourceAllowNullID para permitir operar
            // mesmo quando LastItemData.ID for 0 (fallback seguro).
            if (isImage && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                std::string pathString = entry.Path.string();
                ImGui::SetDragDropPayload("CONTENT_BROWSER_IMAGE_PATH", pathString.c_str(), pathString.size() + 1);
                if (hasThumbnail)
                    ImGui::Image((ImTextureID)(uintptr_t)thumbnail->GetRendererID(), ImVec2(48, 48));
                ImGui::Text("%s", entry.Name.c_str());
                ImGui::EndDragDropSource();
            }

            // Duplo-clique: APENAS registra a INTENCAO de navegar/abrir
            // mapa aqui - a execucao real acontece depois do loop, ver
            // comentario grande no inicio da funcao. Chamar NavigateTo()
            // ou m_OnMapDoubleClicked() diretamente dentro do loop era
            // exatamente a causa do HEAP CORRUPTION (use-after-free no
            // m_CachedEntries).
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (entry.IsDirectory) {
                    navigateToPath = entry.Path;
                }
                else if (isMapFile && m_OnMapDoubleClicked) {
                    openMapPath = entry.Path;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", entry.Path.string().c_str());
            }

            ImGui::PopID();

            ImGui::NextColumn();
        }

        ImGui::Columns(1);

        // Executa a acao adiada (se houver) FORA do loop - a essa altura o
        // iterador do range-based for ja foi destruido, entao NavigateTo()
        // pode mexer em m_CachedEntries sem invalidar nada em uso. So uma
        // das duas pode estar setada (navegar e abrir mapa sao mutuamente
        // exclusivos por tipo de entrada), mas checamos nesta ordem por
        // clareza: navegar tem prioridade conceitual sobre abrir (se algum
        // dia uma unica entrada puder disparar os dois - nao e o caso
        // hoje - faz mais sentido priorizar a navegacao).
        if (!navigateToPath.empty()) {
            NavigateTo(navigateToPath);
        }
        else if (!openMapPath.empty() && m_OnMapDoubleClicked) {
            m_OnMapDoubleClicked(openMapPath);
        }
    }

}