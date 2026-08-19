#pragma once

// ============================================================================
// Components.h
// Components sao structs "burros": so dados, sem logica. Uma Entity e so um
// ID que aponta para um conjunto de components dentro do registro EnTT da
// Scene dona dela (ver Scene.h/Entity.h). Isso e a essencia de um ECS:
// comportamento vive em sistemas (funcoes/metodos de Scene) que iteram sobre
// components, nao em metodos dos proprios components.
// ============================================================================

#define GLM_ENABLE_EXPERIMENTAL // necessario para incluir headers gtx/* (yawPitchRoll vive em gtx/euler_angles.hpp)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp> // glm::yawPitchRoll (extensao GTX, header separado do gtc/quaternion)
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <algorithm>

namespace Prism {

    // Todo Entity criada por Scene::CreateEntity ja recebe este component -
    // e o que da um nome legivel a entidade na Hierarchy panel do editor.
    struct TagComponent {
        std::string Tag;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    // Todo Entity criada por Scene::CreateEntity tambem ja recebe este
    // component - posicao/rotacao/escala no espaco da cena. Rotacao em
    // graus (Euler XYZ) por simplicidade de UI no editor; convertida para
    // quaternion/matriz so na hora de montar GetTransform().
    struct TransformComponent {
        glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // graus
        glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& translation) : Translation(translation) {}

        glm::mat4 GetTransform() const {
            glm::mat4 rotation = glm::yawPitchRoll(
                glm::radians(Rotation.y), glm::radians(Rotation.x), glm::radians(Rotation.z));

            return glm::translate(glm::mat4(1.0f), Translation)
                 * rotation
                 * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    // Primitivas de mesh embutidas na engine (sem importacao de assets
    // ainda - ver guia do prototipo: FBX/OBJ/glTF ficam para uma fase
    // futura). O Renderer gera a geometria de cada uma proceduralmente em
    // Renderer::Init() - ver Renderer.cpp.
    enum class PrimitiveMesh {
        Cube,
        Sphere,
        Capsule,
        Cylinder,
        Plane
    };

    // Marca a entidade como algo visivel na viewport 3D. Ainda nao tem
    // Material de verdade (so uma cor solida) - isso e o proximo ponto de
    // extensao natural quando o sistema de assets/materiais existir.
    struct MeshRendererComponent {
        PrimitiveMesh Mesh = PrimitiveMesh::Cube;
        glm::vec3 Color = { 0.85f, 0.55f, 0.2f };

        MeshRendererComponent() = default;
        MeshRendererComponent(const MeshRendererComponent&) = default;
    };

    // Tipo de projecao da camera. Perspective e o padrao (FPS/TPS - ver
    // guia do prototipo); Orthographic fica reservado para usos futuros
    // (ex: uma camera 2D/isometrica, ou uma vista "top-down" de edicao) -
    // ja suportado no dado e no calculo de projecao (ver
    // CameraComponent::GetProjection) para nao exigir migracao depois.
    enum class CameraProjectionType {
        Perspective,
        Orthographic
    };

    // Camera de cena (distinta da camera de editor em EditorLayer, que so
    // existe para navegar a viewport). Usada pelo modo "Play" (janela
    // separada - ver README) a partir da entidade com Primary=true; tambem
    // usada pela propria viewport do editor quando a cena tem uma camera
    // Primary (ver EditorLayer::RenderScene) - a camera de orbita do editor
    // vira so o fallback para cenas sem nenhuma CameraComponent ainda.
    struct CameraComponent {
        CameraProjectionType ProjectionType = CameraProjectionType::Perspective;

        float FOV = 45.0f;              // graus, so usado se Perspective
        float OrthoSize = 10.0f;        // altura vertical do volume ortografico, so usado se Orthographic
        float NearClip = 0.1f;
        float FarClip = 1000.0f;

        // Apenas UMA entidade por Scene deve ter Primary=true por vez - e
        // essa que o modo Play (e a viewport do editor, como fallback da
        // camera de orbita) usa para renderizar. A UI (Properties panel)
        // e responsavel por impor essa regra ao marcar uma nova Primary
        // (ver EditorLayer::RenderPropertiesPanel) - o component em si nao
        // valida isso, pois nao tem acesso a Scene/outras entidades.
        bool Primary = true;

        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;

        // Monta a matriz de projecao a partir dos campos acima e do aspect
        // ratio do viewport atual (largura/altura em pixels do
        // Framebuffer/janela que vai exibir esta camera - nao guardado no
        // component, pois muda com o tamanho da janela/painel, nao com a
        // camera em si).
        glm::mat4 GetProjection(float aspectRatio) const {
            if (ProjectionType == CameraProjectionType::Orthographic) {
                float halfHeight = OrthoSize * 0.5f;
                float halfWidth = halfHeight * aspectRatio;
                return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, NearClip, FarClip);
            }
            return glm::perspective(glm::radians(FOV), aspectRatio, NearClip, FarClip);
        }
    };

    // Tipos de luz suportados - nomes escolhidos para bater com o
    // vocabulario do guia do prototipo ("spotlight", "omnilight"/point
    // light) em vez de nomes tecnicos de shader.
    //
    // EXTENSIBILIDADE: este enum foi projetado para crescer sem quebrar
    // nada que ja existe. Point/Spot/Directional cobrem o pedido original
    // do guia; Area e IES ja estao aqui como proximos candidatos naturais
    // (comuns em engines "grandes" - Unreal, Unity, Godot) para quando
    // fizerem falta:
    //   - Area: luz emitida por uma superficie retangular/disco (ex:
    //     softbox, janela, painel de LED) - mais realista que Point para
    //     luz "de estudio". Usaria Size (novo campo abaixo) como
    //     dimensoes da superficie.
    //   - IES: luz Point/Spot cujo formato do feixe vem de um perfil
    //     fotometrico real (arquivo .ies, usado por luminarias de
    //     verdade) em vez de um cone matematico perfeito.
    // Adicionar um valor aqui NAO EXIGE mudar a assinatura de nenhuma
    // funcao publica - so exige: 1) um novo case em
    // Renderer::CollectGPULights (traducao LightComponent -> GPULight,
    // ver Renderer.cpp) e 2) o shader saber interpretar o novo valor de
    // u_Lights[i].Type. Nenhum client code (editor, scripts) quebra.
    enum class LightType {
        Point,       // omnilight - brilha em todas as direcoes a partir de um ponto
        Spot,        // cone de luz, como uma lanterna
        Directional, // luz paralela vindo de uma direcao (ex: sol) - posicao da entidade e ignorada, so a rotacao importa

        // --- Candidatos futuros (ver comentario acima) - ainda NAO
        // implementados no Renderer/shader. Deixados aqui, comentados,
        // para documentar o caminho de extensao sem oferecer no editor
        // algo que ainda nao funciona (ver LightComponent::Type combo no
        // Properties panel, que so lista os 3 tipos ja suportados):
        // Area,
        // IES,
    };

    // Component de luz. Cada entidade com este component + TransformComponent
    // e uma fonte de luz de verdade na cena - o Renderer coleta todas via
    // Renderer::CollectGPULights() uma vez por frame e envia como um array
    // de uniforms para o shader (ver Renderer.cpp - suporte a multiplas
    // luzes simultaneas, nao so uma).
    //
    // O DESIGN AQUI E DELIBERADAMENTE "GORDO": em vez de um struct/union
    // por LightType (o que forcaria o shader e qualquer codigo C++ a saber
    // о layout de cada tipo separadamente), todo LightComponent tem TODOS
    // os campos, e cada tipo so usa o subconjunto que faz sentido pra ele
    // (documentado em cada campo). O preco e alguns floats "ociosos" por
    // luz (irrelevante em memoria); o ganho e que Renderer::CollectGPULights
    // e o shader nunca precisam de um layout diferente por tipo - so
    // preenchem/ignoram campos. Isso e o que torna adicionar um tipo novo
    // (Area, IES, ...) uma mudanca pequena e localizada, em vez de uma
    // migracao de dados.
    struct LightComponent {
        LightType Type = LightType::Point;
        glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;

        // Relevante para Point/Spot (e Area no futuro) - alcance da luz
        // antes de se tornar imperceptivel. Directional ignora isso (luz
        // "infinita", ja que representa algo como o sol).
        float Range = 10.0f;

        // Relevante so para Spot - metade do angulo EXTERNO do cone, em
        // graus (0-90): além deste angulo, a luz nao chega. Point/
        // Directional ignoram isso.
        float SpotAngle = 45.0f;

        // Relevante so para Spot - metade do angulo INTERNO do cone, em
        // graus. Entre InnerSpotAngle e SpotAngle a intensidade cai
        // suavemente ate zero (soft edge) em vez de um corte abrupto -
        // sem isso, todo spotlight teria uma borda serrilhada visivel
        // (aliasing) onde o cone termina. Deve ser <= SpotAngle; o editor
        // e responsavel por impor isso (ver Properties panel), o
        // component em si so guarda o valor.
        float InnerSpotAngle = 30.0f;

        // Reservado para quando o Renderer ganhar shadow mapping (fora do
        // escopo desta etapa - ver README). Ja exposto no dado/UI agora
        // para nao exigir migracao de cenas salvas depois: hoje o
        // Renderer LE este campo mas ainda nao produz sombra nenhuma
        // (comportamento identico a CastShadows=false, sempre).
        bool CastShadows = false;

        // Reservado para o futuro tipo Area (ver comentario em LightType)
        // - dimensoes (largura, altura) da superficie emissora. Ignorado
        // por todos os tipos implementados hoje (Point/Spot/Directional).
        glm::vec2 Size = { 1.0f, 1.0f };

        LightComponent() = default;
        LightComponent(const LightComponent&) = default;
    };

    // Formas de colisao suportadas. Nomeado "Collider" (nao "Physics") de
    // proposito: um Collider pode existir SEM RigidBodyComponent (colisao
    // estatica, ex: as paredes de um mapa) - a combinacao dos dois e que
    // define o comportamento fisico completo (ver RigidBodyComponent).
    enum class ColliderShape {
        Box,
        Sphere,
        Capsule
    };

    // Define a FORMA de colisao de uma entidade, consumida pelo
    // PhysicsEngine (Jolt Physics - ver Physics/PhysicsEngine.h) quando a
    // Scene esta rodando (Play). Uma entidade com so Collider (sem
    // RigidBodyComponent) e um obstaculo estatico - nao se move, mas
    // outras coisas colidem com ela (ex: o chao, paredes).
    struct ColliderComponent {
        ColliderShape Shape = ColliderShape::Box;

        // Interpretacao depende de Shape: Box usa Size como half-extents
        // (x,y,z); Sphere usa so Size.x como raio; Capsule usa Size.x como
        // raio e Size.y como altura (Size.z ignorado nesse caso).
        //
        // IMPORTANTE: Size e uma medida em UNIDADES ABSOLUTAS DE MUNDO,
        // totalmente INDEPENDENTE do TransformComponent::Scale da mesma
        // entidade e do tamanho do mesh visual (MeshRendererComponent) -
        // ajustar o Scale (ex: esticar um mesh Plane para virar um chao
        // grande) NAO redimensiona o collider junto. Isso e proposital:
        // O motor de fisica (PhysicsEngine::CreateBodyForEntity, Jolt) usa
        // Size diretamente,
        // sem multiplicar pela Scale da entidade - entao o collider so
        // fica do tamanho certo quando Size e ajustado manualmente na
        // Properties panel para bater com o mesh visual. Esquecer disso e
        // uma fonte comum de bug: o objeto PARECE do tamanho certo (o mesh
        // escalado), mas a colisao de verdade continua no Size default
        // (0.5 em cada eixo) ate ser ajustada a mao.
        glm::vec3 Size = { 0.5f, 0.5f, 0.5f };

        // Trigger = detecta sobreposicao mas nao gera resposta fisica
        // (nao empurra nada) - usado para zonas de gatilho (ex: area que
        // dispara um script quando o player entra), nao para colisao solida.
        bool IsTrigger = false;

        ColliderComponent() = default;
        ColliderComponent(const ColliderComponent&) = default;
    };

    // Como o corpo fisico se comporta dentro da simulacao - nomes que
    // batem com o vocabulario padrao de motores de fisica (Jolt incluido):
    //   Static    - nunca se move, simulacao nao gasta tempo com ele (paredes, chao)
    //   Kinematic - se move, mas so via codigo/script, nunca empurrado pela fisica (plataformas, portas)
    //   Dynamic   - totalmente simulado (gravidade, colisoes empurram) (caixas, o player com fisica)
    enum class BodyType {
        Static,
        Kinematic,
        Dynamic
    };

    // Define o comportamento fisico de uma entidade DENTRO da simulacao -
    // exige um ColliderComponent na mesma entidade para fazer sentido (uma
    // entidade so pode colidir/ser simulada se tiver uma forma definida).
    // Consumido pelo PhysicsEngine (Jolt Physics) quando a Scene esta
    // rodando (Play).
    struct RigidBodyComponent {
        BodyType Type = BodyType::Dynamic;
        float Mass = 1.0f;              // kg - so relevante para Dynamic
        bool UseGravity = true;         // so relevante para Dynamic
        bool ContinuousCollisionDetection = false; // CCD - para objetos rapidos nao atravessarem paredes (ver guia do prototipo)

        // Trava as 3 rotacoes fisicas do corpo (no Jolt, via
        // BodyCreationSettings::mAllowedDOFs - ver PhysicsEngine::CreateBodyForEntity) -
        // o corpo ainda translada normalmente (cai, e empurrado, colide),
        // mas NUNCA tomba/gira sozinho por torque/atrito/colisao. Esencial
        // para qualquer entidade Dynamic cuja rotacao e controlada por
        // script em vez de fisica (o caso mais comum: uma camera FPS/TPS
        // ou o corpo do player, que giram conforme o mouse, nunca conforme
        // uma caixa tombando bateria neles) - sem isto, QUALQUER contato
        // fisico (esbarrar numa parede, cair de uma pequena altura) aplica
        // torque ao corpo e o Simulate() sincroniza essa rotacao de volta
        // para TransformComponent (ver PhysicsEngine::Simulate), brigando
        // visualmente com a rotacao que o script esta tentando manter (o
        // classico "camera girando sozinha/descontrolada ao esbarrar em
        // algo"). Objetos que DEVEM tombar naturalmente (caixas, destroços,
        // qualquer prop fisico comum) devem deixar isto desligado.
        bool FixedRotation = false;

        RigidBodyComponent() = default;
        RigidBodyComponent(const RigidBodyComponent&) = default;
    };

    // Relacao pai/filho entre entidades - opcional (uma entidade sem este
    // component e "raiz", ou seja, filha implicita da propria Scene). Usa
    // entt::entity puro (nao Prism::Entity) de proposito: components nao
    // devem depender de Scene* (ver comentario no topo do arquivo) - quem
    // resolve o handle de volta para uma Entity utilizavel e sempre quem
    // tem acesso a Scene (ver Scene::GetWorldTransform/SetParent).
    //
    // Guardamos Parent + a lista de Children ao mesmo tempo (em vez de so
    // Parent, e reconstruir os filhos varrendo a Scene toda) porque a
    // Hierarchy panel do editor precisa desenhar a arvore todo frame -
    // reconstruir isso a cada frame seria um scan O(n) desnecessario. O
    // preco e manter as duas pontas em sincronia manualmente - toda
    // mudanca de parentesco DEVE passar por Scene::SetParent (nunca mexer
    // em Parent/Children direto), que cuida dos dois lados de uma vez.
    struct RelationshipComponent {
        entt::entity Parent = entt::null;
        std::vector<entt::entity> Children;

        RelationshipComponent() = default;
        RelationshipComponent(const RelationshipComponent&) = default;
    };

    // Slot de script anexado a uma entidade - ainda NAO executa nada (Lua
    // ainda nao esta embutido na engine, ver guia do prototipo). Isto so
    // guarda QUAL arquivo .lua esta associado a entidade, para que:
    //   1) o editor ja tenha uma UI para o fluxo completo antes do Lua
    //      existir (evita re-projetar a UI depois)
    //   2) cenas salvas agora ja carreguem esse dado quando scripting for
    //      ligado, sem precisar migrar arquivos .prismmap antigos
    struct ScriptComponent {
        // Caminho RELATIVO a Project::GetScriptDirectory() (ex:
        // "player_controller.lua") - nao absoluto, para o projeto
        // continuar portavel entre maquinas/pastas.
        std::string ScriptPath;

        ScriptComponent() = default;
        ScriptComponent(const ScriptComponent&) = default;
        ScriptComponent(const std::string& scriptPath) : ScriptPath(scriptPath) {}
    };

    // Um raio FISICO (contra ColliderComponent, via Jolt - mesmo raycast
    // que Physics.Raycast expoe ao Lua, ver PhysicsEngine::Raycast) que
    // dispara automaticamente TODO FRAME enquanto a Scene esta rodando
    // (Play), sem exigir nenhum script - util para sensores reutilizaveis
    // configurados so pela Properties panel: "esta entidade tocando o
    // chao?", visao de um NPC, gatilho de arma, etc.
    //
    // Mesmo espirito de design do RayCast3D da Godot: TargetPosition e um
    // ponto em ESPACO LOCAL da propria entidade (nao um vetor direcao +
    // distancia separados) - o raio vai de (0,0,0) local ate TargetPosition
    // local, e ambos os pontos sao levados para o espaco de MUNDO usando a
    // TransformComponent (ver Scene::UpdateRaycastComponents) antes do
    // teste fisico de verdade. Isso significa que o raio automaticamente
    // gira/translada junto com a entidade (rotacionar o objeto rotaciona
    // o raio junto) sem exigir nenhuma trigonometria manual - so escolher
    // o vetor local certo:
    //   (0, 0, -1) * distancia -> "para frente" (mesma convencao de
    //                             Entity:GetForward() no Lua)
    //   (0, -1, 0) * distancia -> "para baixo" (sensor de chao)
    //   (1, 0, 0)  * distancia -> "para a direita local"
    // etc - qualquer combinacao X/Y/Z e valida, exatamente como no Godot.
    struct RaycastComponent {
        glm::vec3 TargetPosition = { 0.0f, 0.0f, -3.0f }; // espaco local - default "para frente", 3 unidades

        // Se false, o raio nao e testado neste frame (resultado abaixo
        // congela no ultimo valor) - equivalente ao "Enabled" do Godot.
        // Util para ligar/desligar um sensor por script sem precisar
        // remover/recriar o component inteiro.
        bool Enabled = true;

        // --- Resultado do ultimo teste (somente leitura pela UI/scripts;
        // escrito exclusivamente por Scene::UpdateRaycastComponents) ---
        // Mesmos campos de PhysicsEngine::RaycastHit, copiados aqui para
        // a Properties panel poder mostrar o resultado ao vivo sem
        // precisar guardar um RaycastHit inteiro (que tem um entt::entity
        // cru, mais dificil de exibir como "nome da entidade" sem acesso
        // a Scene de dentro do proprio component - ver
        // Scene::UpdateRaycastComponents, que resolve o nome ali mesmo).
        bool Hit = false;
        entt::entity HitEntity = entt::null;
        glm::vec3 HitPoint = { 0.0f, 0.0f, 0.0f };
        glm::vec3 HitNormal = { 0.0f, 0.0f, 0.0f };
        float HitDistance = 0.0f;

        RaycastComponent() = default;
        RaycastComponent(const RaycastComponent&) = default;
    };

}
