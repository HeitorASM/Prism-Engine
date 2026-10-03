#include "EditorGizmos.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace PrismEditor::EditorGizmos {

    void RenderCameraGizmos(EditorContext& ctx, const glm::mat4& viewProjection) {
        // Desenha um frustum simples (piramide com base retangular, apice
        // na posicao da camera) para toda entidade com CameraComponent -
        // sem isso, uma camera seria invisivel na viewport (ela nao tem
        // MeshRendererComponent). So Perspective desenha um frustum de
        // verdade (leque abrindo do apice); Orthographic desenha uma caixa
        // (os planos near/far tem o mesmo tamanho, sem convergencia).
        auto view = ctx.ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::CameraComponent>();
        for (auto entityHandle : view) {
            auto& camera = view.get<Prism::CameraComponent>(entityHandle);

            // Tamanho fixo de exibicao (nao o Far real da camera, que pode
            // ser gigante e tornar o gizmo inutilizavel visualmente) - so
            // near/far "curtos" para dar a nocao de direcao/abertura.
            constexpr float kGizmoNear = 0.15f;
            constexpr float kGizmoFar = 0.6f;

            float nearHalfHeight, nearHalfWidth, farHalfHeight, farHalfWidth;
            // Aspect fixo 16:9 para o gizmo, independente do aspect real do
            // viewport de destino - o gizmo e so uma indicacao visual de
            // "aqui existe uma camera olhando nesta direcao", nao uma
            // preview exata do frustum (isso ficaria caro/complexo de
            // manter em sincronia com o Framebuffer real).
            constexpr float kGizmoAspect = 16.0f / 9.0f;

            if (camera.ProjectionType == Prism::CameraProjectionType::Orthographic) {
                float halfHeight = camera.OrthoSize * 0.5f * 0.15f; // escalado para o mesmo tamanho visual do frustum perspective
                nearHalfHeight = farHalfHeight = halfHeight;
                nearHalfWidth = farHalfWidth = halfHeight * kGizmoAspect;
            }
            else {
                float tanHalfFov = tanf(glm::radians(camera.FOV) * 0.5f);
                nearHalfHeight = kGizmoNear * tanHalfFov;
                nearHalfWidth = nearHalfHeight * kGizmoAspect;
                farHalfHeight = kGizmoFar * tanHalfFov;
                farHalfWidth = farHalfHeight * kGizmoAspect;
            }

            glm::mat4 model = ctx.ActiveScene->GetWorldTransform(Prism::Entity(entityHandle, ctx.ActiveScene.get()));
            // Camera olha para -Z local (convencao padrao de camera em
            // OpenGL/glm, igual view = glm::inverse(model) em ViewportPanel::RenderScene()
            // assume implicitamente).
            auto toWorld = [&](float x, float y, float z) {
                return glm::vec3(model * glm::vec4(x, y, -z, 1.0f));
                };

            glm::vec3 apex = toWorld(0, 0, 0);
            glm::vec3 nearTL = toWorld(-nearHalfWidth, nearHalfHeight, kGizmoNear);
            glm::vec3 nearTR = toWorld(nearHalfWidth, nearHalfHeight, kGizmoNear);
            glm::vec3 nearBL = toWorld(-nearHalfWidth, -nearHalfHeight, kGizmoNear);
            glm::vec3 nearBR = toWorld(nearHalfWidth, -nearHalfHeight, kGizmoNear);
            glm::vec3 farTL = toWorld(-farHalfWidth, farHalfHeight, kGizmoFar);
            glm::vec3 farTR = toWorld(farHalfWidth, farHalfHeight, kGizmoFar);
            glm::vec3 farBL = toWorld(-farHalfWidth, -farHalfHeight, kGizmoFar);
            glm::vec3 farBR = toWorld(farHalfWidth, -farHalfHeight, kGizmoFar);

            // 8 segmentos: retangulo near, retangulo far, 4 arestas
            // conectando near->far (para Perspective isso converge para o
            // apice se near for pequeno o bastante - aqui so desenhamos o
            // retangulo near normalmente, ja que kGizmoNear > 0).
            std::vector<glm::vec3> points = {
                nearTL, nearTR,  nearTR, nearBR,  nearBR, nearBL,  nearBL, nearTL, // retangulo near
                farTL, farTR,    farTR, farBR,    farBR, farBL,    farBL, farTL,   // retangulo far
                nearTL, farTL,   nearTR, farTR,   nearBR, farBR,   nearBL, farBL,  // arestas conectando
                apex, nearTL,    apex, nearTR,    apex, nearBR,    apex, nearBL,   // apice ao retangulo near (indica a origem/posicao da camera)
            };

            // Primary usa uma cor diferente (ciano) das demais (cinza) -
            // ajuda a identificar de relance qual camera o modo Play vai
            // usar quando ha mais de uma na cena.
            glm::vec3 color = camera.Primary ? glm::vec3(0.25f, 0.85f, 0.95f) : glm::vec3(0.6f, 0.6f, 0.6f);

            Prism::Renderer::DrawLines(glm::value_ptr(points[0]), (uint32_t)points.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
        }
    }

    void RenderSelectedColliderGizmo(EditorContext& ctx, const glm::mat4& viewProjection) {
        // So desenha se a entidade selecionada tiver Transform + Collider -
        // sem selecao (EditorContext::SelectedEntity invalida) ou sem ColliderComponent,
        // nao ha nada a fazer.
        if (!ctx.SelectedEntity || !ctx.SelectedEntity.HasComponent<Prism::ColliderComponent>())
            return;

        auto& collider = ctx.SelectedEntity.GetComponent<Prism::ColliderComponent>();

        // Gizmo do collider usa a posicao/rotacao de MUNDO da entidade
        // (ancestrais inclusos, via GetWorldTransform - ver parenting em
        // Scene.h), mas DELIBERADAMENTE SEM a Scale de mundo - Collider::Size
        // (half-extents/raio) e uma medida em unidades ABSOLUTAS que o
        // usuario ajusta manualmente na Properties panel, independente do
        // tamanho do mesh visual (ver comentario em ColliderComponent,
        // Components.h) - e assim que PhysicsEngine::CreateBodyForEntity
        // tambem trata (usa Size diretamente, nunca multiplica pela Scale
        // da entidade). Usar GetWorldTransform() completo aqui (incluindo
        // Scale) aplicava a escala DUAS vezes de fato (uma no proprio
        // Size, que o usuario ja pensa em unidades finais, e outra via
        // matriz) sempre que a entidade tinha Scale != {1,1,1} - por
        // exemplo, um "chao" com mesh Plane escalado para {10,1,10}
        // deixava o gizmo do collider gigantesco e desalinhado do que a
        // fisica de fato usava (que ignorava a Scale). Corrigido extraindo
        // so posicao+rotacao da matriz de mundo, sem escala.
        glm::mat4 worldMatrixWithScale = ctx.ActiveScene->GetWorldTransform(ctx.SelectedEntity);
        glm::vec3 worldPosition = glm::vec3(worldMatrixWithScale[3]);
        glm::vec3 col0 = glm::vec3(worldMatrixWithScale[0]);
        glm::vec3 col1 = glm::vec3(worldMatrixWithScale[1]);
        glm::vec3 col2 = glm::vec3(worldMatrixWithScale[2]);
        glm::mat3 worldRotationOnly(
            glm::length(col0) > 0.00001f ? col0 / glm::length(col0) : glm::vec3(1, 0, 0),
            glm::length(col1) > 0.00001f ? col1 / glm::length(col1) : glm::vec3(0, 1, 0),
            glm::length(col2) > 0.00001f ? col2 / glm::length(col2) : glm::vec3(0, 0, 1)
        );
        auto toWorld = [&](const glm::vec3& local) {
            return worldPosition + worldRotationOnly * local;
            };

        // Amarelo: convencao comum de "gizmo de colisao selecionado" (Unity
        // usa verde-claro, Unreal usa laranja/vermelho, Godot usa um roxo
        // claro - amarelo aqui so para ficar bem distinto do ciano/cinza ja
        // usados pelas cameras, ver RenderCameraGizmos acima).
        glm::vec3 color(0.95f, 0.85f, 0.2f);

        // Gera os pontos (pares consecutivos = segmentos, ver DrawLines) de
        // um circulo de raio 'radius' no plano perpendicular a 'axis' (0=X,
        // 1=Y, 2=Z), centrado em 'center' (espaco local, antes de toWorld),
        // com 'segments' segmentos - usado tanto para Sphere (3 circulos
        // ortogonais) quanto para as tampas da Capsule.
        auto appendCircle = [&](std::vector<glm::vec3>& points, glm::vec3 center, float radius, int axis, int segments) {
            glm::vec3 prev;
            for (int i = 0; i <= segments; i++) {
                float t = (float)i / (float)segments * 2.0f * 3.14159265f;
                float c = radius * cosf(t);
                float s = radius * sinf(t);
                glm::vec3 p = center;
                if (axis == 0)      p += glm::vec3(0.0f, c, s); // circulo no plano YZ (perpendicular a X)
                else if (axis == 1) p += glm::vec3(c, 0.0f, s); // circulo no plano XZ (perpendicular a Y)
                else                p += glm::vec3(c, s, 0.0f); // circulo no plano XY (perpendicular a Z)

                if (i > 0) { points.push_back(prev); points.push_back(p); }
                prev = p;
            }
            };

        // Gera um arco de 180 graus (meio-circulo) de raio 'radius',
        // centrado em 'center', comecando na direcao 'startAxis' e
        // terminando na direcao 'endAxis' (dois eixos ortogonais entre si -
        // ex: startAxis=(1,0,0), endAxis=(0,1,0) desenha o quarto de volta
        // de +X ate +Y, e o proximo quarto de +Y ate -X, completando meia
        // volta). Usado para as calotas hemisfericas da Capsule (2 arcos
        // por calota = uma "cruz" de meridianos, dando a nocao de cupula
        // sem precisar de uma malha completa).
        auto appendArc = [&](std::vector<glm::vec3>& points, glm::vec3 center, float radius, glm::vec3 startAxis, glm::vec3 endAxis, int segments) {
            glm::vec3 prev;
            for (int i = 0; i <= segments; i++) {
                float t = (float)i / (float)segments * 3.14159265f; // 0 .. PI (meia volta)
                glm::vec3 p = center + radius * (startAxis * cosf(t) + endAxis * sinf(t));
                if (i > 0) { points.push_back(prev); points.push_back(p); }
                prev = p;
            }
            };

        std::vector<glm::vec3> localPoints;
        constexpr int kCircleSegments = 24;
        constexpr int kArcSegments = 12;

        switch (collider.Shape) {
        case Prism::ColliderShape::Box: {
            // Size e ja meio-extensao (half-extents) - ver comentario em
            // ColliderComponent (Components.h).
            glm::vec3 e = collider.Size;
            glm::vec3 c[8] = {
                { -e.x,-e.y,-e.z }, {  e.x,-e.y,-e.z }, {  e.x, e.y,-e.z }, { -e.x, e.y,-e.z }, // face -Z
                { -e.x,-e.y, e.z }, {  e.x,-e.y, e.z }, {  e.x, e.y, e.z }, { -e.x, e.y, e.z }, // face +Z
            };
            int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0}, // face -Z
                {4,5},{5,6},{6,7},{7,4}, // face +Z
                {0,4},{1,5},{2,6},{3,7}, // arestas conectando as duas faces
            };
            for (auto& e2 : edges) { localPoints.push_back(c[e2[0]]); localPoints.push_back(c[e2[1]]); }
            break;
        }
        case Prism::ColliderShape::Sphere: {
            float r = collider.Size.x; // so Size.x e usado como raio, ver ColliderComponent
            appendCircle(localPoints, glm::vec3(0.0f), r, 0, kCircleSegments);
            appendCircle(localPoints, glm::vec3(0.0f), r, 1, kCircleSegments);
            appendCircle(localPoints, glm::vec3(0.0f), r, 2, kCircleSegments);
            break;
        }
        case Prism::ColliderShape::Capsule: {
            // Size.x = raio, Size.y = altura so da parte CILINDRICA, sem as
            // duas calotas hemisfericas (Size.z ignorado - ver
            // ColliderComponent). O "cilindro" do meio vai de
            // -halfCylinderHeight a +halfCylinderHeight; cada calota e
            // uma hemisfera de raio 'radius' colada em cada ponta,
            // desenhada com 2 arcos de meridiano (planos XY e ZY) + o
            // equador (reaproveitando appendCircle) - suficiente para
            // ler "isto e uma capsula, nao um cilindro" de relance, sem
            // precisar de uma malha completa de esfera.
            float radius = collider.Size.x;
            float halfCylinderHeight = std::max(collider.Size.y * 0.5f, 0.0f); // metade da parte cilindrica; as calotas ficam POR FORA disso (mesma convencao de PhysicsEngine::CreateBodyForEntity: altura total real = Size.y + 2*raio)

            // Equador do cilindro (topo e base da parte reta).
            appendCircle(localPoints, glm::vec3(0.0f, halfCylinderHeight, 0.0f), radius, 1, kCircleSegments);
            appendCircle(localPoints, glm::vec3(0.0f, -halfCylinderHeight, 0.0f), radius, 1, kCircleSegments);

            // Calota de cima: hemisferio acima de y=halfCylinderHeight,
            // desenhado como 2 meridianos de 180 graus (de +X a +Y, e
            // de +Z a +Y) - a metade "de cima" do arco (de 0 a PI/2 ja
            // cobre o quarto que importa, mas usar o arco completo de
            // +X/+Z ate -X/-Z passando por +Y da a cupula inteira numa
            // linha so por meridiano).
            glm::vec3 topCenter(0.0f, halfCylinderHeight, 0.0f);
            appendArc(localPoints, topCenter, radius, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), kArcSegments);
            appendArc(localPoints, topCenter, radius, glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), kArcSegments);
            appendArc(localPoints, topCenter, radius, glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), kArcSegments);
            appendArc(localPoints, topCenter, radius, glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), kArcSegments);

            // Calota de baixo: espelhada (aponta para -Y em vez de +Y).
            glm::vec3 bottomCenter(0.0f, -halfCylinderHeight, 0.0f);
            appendArc(localPoints, bottomCenter, radius, glm::vec3(1, 0, 0), glm::vec3(0, -1, 0), kArcSegments);
            appendArc(localPoints, bottomCenter, radius, glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0), kArcSegments);
            appendArc(localPoints, bottomCenter, radius, glm::vec3(0, 0, 1), glm::vec3(0, -1, 0), kArcSegments);
            appendArc(localPoints, bottomCenter, radius, glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), kArcSegments);

            // 4 linhas verticais ao redor do cilindro (nas direcoes
            // +X/-X/+Z/-Z) conectando o equador de cima ao de baixo -
            // sem essas, as duas calotas + equadores pareceriam 2
            // esferas soltas em vez de uma capsula conectada.
            glm::vec3 dirs[4] = { {radius,0,0}, {-radius,0,0}, {0,0,radius}, {0,0,-radius} };
            for (auto& d : dirs) {
                localPoints.push_back(topCenter + d);
                localPoints.push_back(bottomCenter + d);
            }
            break;
        }
        case Prism::ColliderShape::ConvexHull:
        case Prism::ColliderShape::TriangleMesh: {
            // A forma real vem da malha (escala de MUNDO aplicada, ao
            // contrario de Size) - desenhamos so a caixa dos bounds dela como
            // referencia de tamanho/posicao, nao a malha inteira.
            const Prism::Mesh* mesh = ctx.SelectedEntity.HasComponent<Prism::MeshRendererComponent>()
                ? Prism::Renderer::ResolveMesh(ctx.SelectedEntity.GetComponent<Prism::MeshRendererComponent>()) : nullptr;
            if (!mesh)
                break;
            glm::vec3 worldScale(glm::length(col0), glm::length(col1), glm::length(col2));
            glm::vec3 lo = mesh->GetLocalBoundsMin() * worldScale;
            glm::vec3 hi = mesh->GetLocalBoundsMax() * worldScale;
            glm::vec3 c[8] = {
                { lo.x,lo.y,lo.z }, { hi.x,lo.y,lo.z }, { hi.x,hi.y,lo.z }, { lo.x,hi.y,lo.z },
                { lo.x,lo.y,hi.z }, { hi.x,lo.y,hi.z }, { hi.x,hi.y,hi.z }, { lo.x,hi.y,hi.z },
            };
            int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7},
            };
            for (auto& e2 : edges) { localPoints.push_back(c[e2[0]]); localPoints.push_back(c[e2[1]]); }
            break;
        }
        }

        std::vector<glm::vec3> worldPoints;
        worldPoints.reserve(localPoints.size());
        for (auto& p : localPoints)
            worldPoints.push_back(toWorld(p));

        if (!worldPoints.empty())
            Prism::Renderer::DrawLines(glm::value_ptr(worldPoints[0]), (uint32_t)worldPoints.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
    }

    void RenderLightGizmos(EditorContext& ctx, const glm::mat4& viewProjection) {
        // Mesmo helper de circulo usado por RenderSelectedColliderGizmo
        // (Sphere/Capsule) - reaproveitado aqui para a esfera de alcance
        // do Point e a base do cone do Spot. Duplicar em vez de extrair
        // para um lugar compartilhado por enquanto: as duas funcoes tem
        // necessidades ligeiramente diferentes (aqui tambem precisamos de
        // "leques" saindo do apice do cone, que o collider nao usa) e o
        // arquivo ja segue esse padrao de helpers locais por funcao (ver
        // appendCircle/appendArc acima) - extrair um Gizmos.h so vale a
        // pena se um terceiro gizmo precisar dos mesmos helpers.
        auto appendCircle = [](std::vector<glm::vec3>& points, glm::vec3 center, glm::vec3 axisU, glm::vec3 axisV, float radius, int segments) {
            glm::vec3 prev;
            for (int i = 0; i <= segments; i++) {
                float t = (float)i / (float)segments * 2.0f * 3.14159265f;
                glm::vec3 p = center + radius * (axisU * cosf(t) + axisV * sinf(t));
                if (i > 0) { points.push_back(prev); points.push_back(p); }
                prev = p;
            }
            };

        constexpr int kCircleSegments = 24;
        constexpr int kConeRaySegments = 8; // quantas linhas do apice ate a borda do cone (leque)

        auto view = ctx.ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::LightComponent>();
        for (auto entityHandle : view) {
            auto& light = view.get<Prism::LightComponent>(entityHandle);
            Prism::Entity entity(entityHandle, ctx.ActiveScene.get());

            // Mesma extracao de posicao+rotacao SEM escala que
            // RenderSelectedColliderGizmo usa (ver comentario grande la) -
            // pelo mesmo motivo: Range/SpotAngle sao medidas absolutas
            // (metros/graus), independentes da Scale da entidade. Uma luz
            // dentro de um objeto escalado nao deve ter seu gizmo (nem o
            // calculo de iluminacao real, ver Renderer::CollectGPULights)
            // deformado por essa escala.
            glm::mat4 worldMatrixWithScale = ctx.ActiveScene->GetWorldTransform(entity);
            glm::vec3 worldPosition = glm::vec3(worldMatrixWithScale[3]);
            glm::vec3 col0 = glm::vec3(worldMatrixWithScale[0]);
            glm::vec3 col1 = glm::vec3(worldMatrixWithScale[1]);
            glm::vec3 col2 = glm::vec3(worldMatrixWithScale[2]);
            glm::mat3 worldRotationOnly(
                glm::length(col0) > 0.00001f ? col0 / glm::length(col0) : glm::vec3(1, 0, 0),
                glm::length(col1) > 0.00001f ? col1 / glm::length(col1) : glm::vec3(0, 1, 0),
                glm::length(col2) > 0.00001f ? col2 / glm::length(col2) : glm::vec3(0, 0, 1)
            );
            auto toWorld = [&](const glm::vec3& local) {
                return worldPosition + worldRotationOnly * local;
                };

            // "Frente" da luz em espaco local - mesma convencao -Z usada
            // por CameraComponent (ver RenderCameraGizmos/RenderScene) e
            // por Renderer::CollectGPULights (Renderer.cpp), para o gizmo
            // sempre apontar exatamente para onde a luz de fato ilumina.
            glm::vec3 forward = worldRotationOnly * glm::vec3(0.0f, 0.0f, -1.0f);

            // Cor da propria luz (ver LightComponent::Color) - assim o
            // gizmo ja da uma pista visual de qual luz e qual sem precisar
            // selecionar cada uma. A entidade selecionada fica em branco
            // (sobrescrevendo a cor) para se destacar de forma inequivoca
            // mesmo quando a cor da luz e escura ou parecida com outras.
            bool isSelected = (ctx.SelectedEntity == entity);
            glm::vec3 color = isSelected ? glm::vec3(1.0f, 1.0f, 1.0f) : light.Color;

            std::vector<glm::vec3> localPoints;

            switch (light.Type) {
            case Prism::LightType::Point: {
                // Omnilight: esfera de raio Range, mesma tecnica de 3
                // circulos ortogonais que RenderSelectedColliderGizmo
                // usa para Sphere - da a nocao de volume sem exigir
                // uma malha completa.
                appendCircle(localPoints, glm::vec3(0.0f), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), light.Range, kCircleSegments); // plano YZ
                appendCircle(localPoints, glm::vec3(0.0f), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), light.Range, kCircleSegments); // plano XZ
                appendCircle(localPoints, glm::vec3(0.0f), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), light.Range, kCircleSegments); // plano XY
                break;
            }
            case Prism::LightType::Spot: {
                // Cone: apice na origem (posicao da luz), abrindo na
                // direcao -Z local ate uma base circular a distancia
                // Range, com raio determinado pelo angulo EXTERNO do
                // cone (SpotAngle - o angulo que efetivamente delimita
                // onde a luz chega a zero, ver LightComponent e o
                // shader em Renderer.cpp). InnerSpotAngle (soft edge)
                // nao ganha um circulo proprio aqui de proposito - um
                // segundo circulo concentrico so adicionaria ruido
                // visual sem ajudar a posicionar a luz, que e o
                // objetivo deste gizmo.
                float coneLength = light.Range;
                float baseRadius = coneLength * tanf(glm::radians(light.SpotAngle));
                glm::vec3 baseCenter(0.0f, 0.0f, -coneLength); // -Z local = "frente" (ver 'forward' acima)

                appendCircle(localPoints, baseCenter, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), baseRadius, kCircleSegments);

                // Leque de linhas do apice (origem) ate a borda do
                // circulo da base - poucas linhas (kConeRaySegments),
                // so para comunicar "isto e um cone solido", nao um
                // anel solto no ar.
                for (int i = 0; i < kConeRaySegments; i++) {
                    float t = (float)i / (float)kConeRaySegments * 2.0f * 3.14159265f;
                    glm::vec3 edge = baseCenter + baseRadius * (glm::vec3(1, 0, 0) * cosf(t) + glm::vec3(0, 1, 0) * sinf(t));
                    localPoints.push_back(glm::vec3(0.0f));
                    localPoints.push_back(edge);
                }
                break;
            }
            case Prism::LightType::Directional: {
                // Sem posicao nem alcance reais (ver LightComponent) -
                // o gizmo e so uma seta curta indicando a DIREcao que
                // a luz viaja, saindo da posicao da entidade (que e
                // arbitraria/so para posicionar o gizmo na viewport,
                // ja que Renderer::CollectGPULights ignora a posicao
                // deste tipo). Tamanho fixo (nao ha Range para
                // escalar) - grande o suficiente para ser visivel sem
                // depender do tamanho da cena.
                constexpr float kArrowLength = 1.5f;
                constexpr float kArrowHeadSize = 0.25f;
                glm::vec3 tip(0.0f, 0.0f, -kArrowLength);

                localPoints.push_back(glm::vec3(0.0f));
                localPoints.push_back(tip);

                // Cabeca da seta: 4 linhas curtas da ponta "voltando"
                // em diagonal - leitura clara de qual ponta e a ponta
                // sem precisar de um cone/malha completa.
                glm::vec3 back = tip + glm::vec3(0, 0, kArrowHeadSize);
                glm::vec3 heads[4] = {
                    back + glm::vec3(kArrowHeadSize, 0, 0), back + glm::vec3(-kArrowHeadSize, 0, 0),
                    back + glm::vec3(0, kArrowHeadSize, 0), back + glm::vec3(0, -kArrowHeadSize, 0),
                };
                for (auto& h : heads) { localPoints.push_back(tip); localPoints.push_back(h); }
                break;
            }
                                              // TODO(Area/IES): quando LightType ganhar esses valores
                                              // (ver Components.h), adicionar o wireframe correspondente
                                              // aqui - ex: Area desenharia um retangulo (Size.x/Size.y)
                                              // em vez de esfera/cone.
            }

            std::vector<glm::vec3> worldPoints;
            worldPoints.reserve(localPoints.size());
            for (auto& p : localPoints)
                worldPoints.push_back(toWorld(p));

            if (!worldPoints.empty())
                Prism::Renderer::DrawLines(glm::value_ptr(worldPoints[0]), (uint32_t)worldPoints.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
        }
    }

    void RenderRaycastGizmos(EditorContext& ctx, const glm::mat4& viewProjection) {
        auto view = ctx.ActiveScene->GetRegistry().view<Prism::TransformComponent, Prism::RaycastComponent>();
        for (auto entityHandle : view) {
            auto& raycast = view.get<Prism::RaycastComponent>(entityHandle);
            Prism::Entity entity(entityHandle, ctx.ActiveScene.get());

            // TargetPosition e um PONTO local (identico em espirito a
            // TransformComponent::Translation, NAO uma medida absoluta
            // como ColliderComponent::Size/LightComponent::Range) - por
            // isso usamos GetWorldTransform() COMPLETO (com Scale
            // inclusa), diferente de RenderSelectedColliderGizmo/
            // RenderLightGizmos acima, que extraem so posicao+rotacao de
            // proposito. Mesma matriz que Scene::UpdateRaycastComponents
            // usa para o teste fisico de verdade - o gizmo sempre bate
            // com o que o raio realmente testou.
            glm::mat4 world = ctx.ActiveScene->GetWorldTransform(entity);
            glm::vec3 worldOrigin = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            glm::vec3 worldTarget = glm::vec3(world * glm::vec4(raycast.TargetPosition, 1.0f));

            // Enquanto a Scene esta rodando E o raio acertou algo, desenha
            // so ate o ponto de impacto (nao ate TargetPosition) - deixa
            // claro visualmente ONDE o raio parou, igual o debug draw de
            // raycast de qualquer engine. Nos demais casos (nao rodando,
            // ou rodando mas sem acerto) desenha ate TargetPosition
            // inteiro - ver comentario no header sobre nao mostrar um
            // HitPoint congelado/desatualizado fora do modo Play.
            bool showingHit = ctx.ActiveScene->IsRunning() && raycast.Hit;
            glm::vec3 lineEnd = showingHit ? raycast.HitPoint : worldTarget;

            // Verde = acertou algo (mesma convencao universal de "hit" em
            // debug draw); cinza = sem acerto ou fora do modo Play (raio
            // "inativo"). Amarelo ja e usado pelo Collider (ver
            // RenderSelectedColliderGizmo) - evitado aqui para os dois
            // gizmos nunca se confundirem quando aparecem juntos na mesma
            // entidade (um RaycastComponent sensor de chao, por exemplo,
            // tipicamente vive numa entidade que TAMBEM tem Collider).
            glm::vec3 color = showingHit ? glm::vec3(0.25f, 0.9f, 0.35f) : glm::vec3(0.55f, 0.55f, 0.55f);

            std::vector<glm::vec3> points = { worldOrigin, lineEnd };
            Prism::Renderer::DrawLines(glm::value_ptr(points[0]), (uint32_t)points.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));

            // Uma pequena cruz no ponto de impacto (3 segmentos curtos
            // cruzando nos eixos) so quando ha um Hit de verdade - ajuda a
            // localizar o ponto exato sem precisar aproximar a camera,
            // mesmo padrao visual usado por editores para marcar um ponto
            // de impacto (diferente de um circulo/esfera, que exigiria
            // saber a normal para orientar).
            if (showingHit) {
                constexpr float kMarkerSize = 0.1f;
                std::vector<glm::vec3> marker = {
                    raycast.HitPoint - glm::vec3(kMarkerSize, 0, 0), raycast.HitPoint + glm::vec3(kMarkerSize, 0, 0),
                    raycast.HitPoint - glm::vec3(0, kMarkerSize, 0), raycast.HitPoint + glm::vec3(0, kMarkerSize, 0),
                    raycast.HitPoint - glm::vec3(0, 0, kMarkerSize), raycast.HitPoint + glm::vec3(0, 0, kMarkerSize),
                };
                Prism::Renderer::DrawLines(glm::value_ptr(marker[0]), (uint32_t)marker.size(), glm::value_ptr(viewProjection), glm::value_ptr(color));
            }
        }
    }

}
