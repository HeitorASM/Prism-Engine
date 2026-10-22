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
#include "../Assets/AssetID.h"
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

    // Primitivas de mesh embutidas na engine (nao ha importacao de modelos
    // FBX/OBJ/glTF ainda). A geometria de cada uma e gerada
    // proceduralmente (ver PrimitiveMeshFactory) e enviada a GPU em
    // Renderer::Init().
    enum class PrimitiveMesh {
        Cube,
        Sphere,
        Capsule,
        Cylinder,
        Plane
    };

    // Marca a entidade como algo visivel na viewport 3D. A COR aqui
    // (Color) e sempre o FALLBACK: se a entidade tiver um
    // MaterialComponent (ver abaixo) com AlbedoPath configurado, a
    // textura de albedo substitui esta cor solida; sem MaterialComponent
    // (ou com AlbedoPath vazio), Color e usada normalmente -
    // MeshRendererComponent funciona sozinho, sem exigir um
    // MaterialComponent em toda entidade.
    //
    // --- Modelo IMPORTADO (.obj/.fbx/.gltf/.glb) em vez de primitiva ---
    // ModelAsset e um AssetID (mesmo mecanismo de
    // MaterialComponent::LinkedAsset, ver AssetID.h) apontando para um
    // asset AssetType::Model dentro da pasta do projeto - ver
    // ModelLoader.h para a importacao em si. Invalido (o default,
    // IsValid()==false) significa "sem modelo importado - usar a
    // primitiva embutida 'Mesh' abaixo", exatamente o comportamento de
    // sempre desta engine antes da importacao de modelos existir.
    //
    // ModelAsset e 'Mesh' (PrimitiveMesh) sao MUTUAMENTE EXCLUSIVOS na
    // pratica (ver Renderer::DrawScene/DrawMesh: ModelAsset valido tem
    // prioridade e 'Mesh' e ignorado nesse caso) mas guardados os DOIS ao
    // mesmo tempo, no MESMO component - nao um enum "Kind" +
    // union/variant - para: 1) o formato de arquivo serializado
    // (ComponentRegistration.cpp) nunca precisar de uma migracao ao
    // adicionar este campo (so mais um AssetID no final, default
    // invalido = comportamento antigo exato para cenas salvas antes desta
    // mudanca), 2) trocar de "modelo X" de volta para "Cubo" no editor
    // continuar simples (so zerar ModelAsset), sem perder/recriar o
    // component inteiro.
    struct MeshRendererComponent {
        PrimitiveMesh Mesh = PrimitiveMesh::Cube;
        glm::vec3 Color = { 0.85f, 0.55f, 0.2f };
        AssetID ModelAsset; // ver comentario acima - invalido = usa 'Mesh' (primitiva)

        // Qual submesh (um por MATERIAL do arquivo, ver ImportedSubmesh em
        // Assets/ModelLoader.h) do modelo desenhar. -1 (padrao) = a malha
        // mesclada de todos os submeshes (comportamento da v1 da importacao).
        // O editor instancia um modelo com varios materiais como uma entidade
        // por submesh, cada uma com o seu MaterialComponent. Ignorado se
        // ModelAsset for invalido; fora do intervalo cai na malha mesclada.
        int32_t SubmeshIndex = -1;

        MeshRendererComponent() = default;
        MeshRendererComponent(const MeshRendererComponent&) = default;
    };

    // Material opcional de uma entidade - ver Renderer::DrawMesh/
    // s_FragmentSrc (Renderer.cpp) para o shader que consome estes dados.
    // O shader usa AlbedoPath (+ AlbedoTint), NormalPath e
    // RoughnessMetallicPath/RoughnessFactor/MetallicFactor num modelo PBR
    // metallic/roughness (Cook-Torrance GGX). Component SEPARADO de MeshRendererComponent
    // (nao um campo a mais nele) de proposito: nem toda entidade visivel
    // precisa de controle fino de material (objetos de bloqueio de nivel
    // ficam bem so com MeshRendererComponent::Color) - manter os dois components
    // independentes deixa o caso simples simples, e o caso avancado
    // opcional.
    //
    // *Path sao caminhos de arquivo RELATIVOS a pasta Assets do projeto
    // (mesma convencao ja usada por ScriptComponent::ScriptPath,
    // Components.h) - vazio significa "sem esta textura", nao um erro
    // (ver Renderer::DrawMesh, que usa os fatores/tint como fallback
    // quando o path correspondente esta vazio ou a textura falhou ao
    // carregar).
    struct MaterialComponent {
        std::string AlbedoPath;              // cor base (RGB) - textura sRGB (ver Texture2D)
        std::string NormalPath;              // normal map em tangent-space - textura LINEAR (nao sRGB)
        std::string RoughnessMetallicPath;   // convencao glTF: canal G = roughness, canal B = metallic - textura LINEAR

        // Multiplicadores/fallbacks - sempre aplicados, MESMO com uma
        // textura carregada (ex: AlbedoTint multiplica o resultado da
        // amostragem de AlbedoPath, permitindo reusar a MESMA textura em
        // varias entidades com tons ligeiramente diferentes, sem
        // duplicar o arquivo de imagem). Sem a textura correspondente
        // (path vazio ou falha ao carregar), o fator/tint sozinho VIRA o
        // valor usado (equivalente a uma "textura" solida daquela cor).
        glm::vec3 AlbedoTint = { 1.0f, 1.0f, 1.0f };
        float RoughnessFactor = 0.5f; // 0 = espelhado, 1 = totalmente difuso/fosco
        float MetallicFactor = 0.0f;  // 0 = dieletrico (plastico/madeira/pedra), 1 = metal puro

        // VINCULO VIVO com um asset .prismmat (ver Assets/AssetID.h e
        // MaterialSerializer.h). Invalido (AssetID{} / IsValid()==false,
        // o default) significa "copia independente" - o comportamento de
        // sempre, sem mudanca nenhuma. Quando valido, os campos acima
        // SAO uma copia local para renderizar, mas o editor trata este
        // MaterialComponent como uma VIEW do arquivo apontado por
        // LinkedAsset: editar qualquer campo aqui grava no arquivo
        // (com debounce, ver MaterialLinkSync::FlushSave), e o
        // arquivo sendo recarregado atualiza esta e QUALQUER OUTRA
        // entidade com o mesmo LinkedAsset (ver
        // MaterialLinkSync::Reconcile). E so um AssetID cru:
        // nao inclui logica de I/O aqui de proposito, para Components.h
        // continuar sem depender de arquivo/disco (mesmo espirito de
        // AlbedoPath/NormalPath sendo string, nao um objeto Texture).
        AssetID LinkedAsset;

        MaterialComponent() = default;
        MaterialComponent(const MaterialComponent&) = default;
    };

    // O que uma camera desenha ao fundo (ver CameraComponent::ClearMode).
    enum class CameraClearMode : int {
        Skybox = 0,      // o fundo do EnvironmentComponent da cena (cor, gradiente ou panorama)
        SolidColor = 1   // CameraComponent::ClearColor, sem skybox
    };

    // Tipo de projecao da camera. Perspective e o padrao (FPS/TPS);
    // Orthographic serve para uma camera 2D/isometrica ou vista "top-down"
    // - ja suportado no dado e no calculo de projecao (ver
    // CameraComponent::GetProjection).
    enum class CameraProjectionType {
        Perspective,
        Orthographic
    };

    // Camera de cena (distinta da camera de editor em EditorLayer, que so
    // existe para navegar a viewport). Usada pelo modo "Play" (janela
    // separada) a partir da entidade com Primary=true, e pela preview de
    // camera do editor. A viewport principal do editor sempre usa a camera
    // livre, nunca esta.
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
        // (ver PropertiesPanel::OnImGuiRender) - o component em si nao
        // valida isso, pois nao tem acesso a Scene/outras entidades.
        bool Primary = true;

        // --- Opcoes de render DESTA camera (v20) ---------------------------
        // O visual de uma camera (o que ela mostra ao fundo, o brilho da
        // lente, o AO) pertence a ela, nao ao projeto: duas cameras da mesma
        // cena podem ter visuais diferentes. O AMBIENTE (luz ambiente e
        // skybox) e da cena - ver EnvironmentComponent. A viewport livre do
        // editor usa os valores padrao abaixo.

        // O que e desenhado ao fundo: o skybox do ambiente da cena ou uma cor chapada.
        CameraClearMode ClearMode = CameraClearMode::Skybox;
        glm::vec3 ClearColor = { 0.05f, 0.05f, 0.07f }; // sRGB (como o color picker); so ClearMode == SolidColor

        // Multiplicador de brilho em espaco linear ANTES do tone mapping (1 = neutro).
        float Exposure = 1.0f;

        // Oclusao de ambiente em espaco de tela (ver SSAO.h). Desligar poupa
        // dois passes de tela cheia.
        bool SSAO = true;

        // Pos-processamento (ver Renderer/PostProcess.h). A cena e desenhada em
        // HDR linear; exposicao + ACES ja eram aplicados antes, entao com tudo
        // desligado a imagem e a mesma.
        bool Bloom = false;
        float BloomThreshold = 1.0f;  // brilho linear a partir do qual brilha (HDR: 1 = branco)
        float BloomIntensity = 0.15f; // quanto do bloom entra na imagem
        bool FXAA = true;             // suaviza serrilhado de bordas
        float Saturation = 1.0f;      // 0 = preto e branco, 1 = neutro
        float Contrast = 1.0f;        // 1 = neutro

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

    // Fundo visivel da cena (ver Renderer::DrawSky e EnvironmentComponent):
    //   Solid     - uma cor chapada (EnvironmentComponent::SkyColor).
    //   Gradient  - o mesmo gradiente ceu/horizonte/chao da luz de ambiente.
    //   Panorama  - imagem equirretangular (.hdr, .png, .jpg...) do projeto.
    // O fundo NAO ilumina a cena: a luz de ambiente vem sempre do gradiente
    // (Zenith/Horizon/Ground), qualquer que seja o modo.
    enum class SkyMode : int {
        Solid = 0,
        Gradient = 1,
        Panorama = 2
    };

    // Ambiente da CENA: luz ambiente (gradiente analitico de 3 cores) e
    // skybox. Fica numa entidade qualquer (convencionalmente "Ambiente") e e
    // gravado no .prismmap junto com o resto da cena. Vale a primeira
    // entidade com este component; sem nenhuma, o Renderer usa os padroes
    // abaixo. Pode ser alterado em runtime por script (ciclo dia/noite).
    //
    // Todas as cores sao sRGB (o que o color picker mostra). Os limites
    // aceitos (aplicados pelo Renderer, que nunca confia no dado) sao:
    // Ambient 0..2, cores 0..1, SkyIntensity 0..16.
    struct EnvironmentComponent {
        // Brilho MEDIO da luz ambiente. 0 = so as luzes iluminam.
        float Ambient = 0.10f;

        // Gradiente de ambiente: ceu (+Y), horizonte e chao (-Y).
        glm::vec3 Zenith  = { 0.36f, 0.55f, 0.95f };
        glm::vec3 Horizon = { 0.80f, 0.85f, 0.90f };
        glm::vec3 Ground  = { 0.22f, 0.20f, 0.18f };

        // Fundo visivel (so aparece em cameras com ClearMode == Skybox).
        SkyMode Sky = SkyMode::Gradient;
        glm::vec3 SkyColor = { 0.05f, 0.05f, 0.07f }; // modo Solid
        AssetID SkyTexture;                           // modo Panorama; invalido = cai para cor chapada
        float SkyRotation = 0.0f;                     // graus em torno de Y (-180..180)
        float SkyIntensity = 1.0f;                    // multiplicador de brilho do panorama

        EnvironmentComponent() = default;
        EnvironmentComponent(const EnvironmentComponent&) = default;
    };

    // Tipos de luz suportados.
    //
    // EXTENSIBILIDADE: este enum foi projetado para crescer sem quebrar
    // nada que ja existe. Area e IES sao candidatos naturais (comuns em
    // engines grandes - Unreal, Unity, Godot) para quando fizerem falta:
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

        // Se esta luz projeta sombra (ver Renderer/ShadowSystem.h).
        // Directional: cascaded shadow maps (3 cascatas). Spot: ate 4 luzes.
        // Point: ate 2 luzes (cubemap). Alem do limite de cada tipo a luz
        // continua acesa, so sem sombra (aviso unico no log).
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
    //
    // Box/Sphere/Capsule usam ColliderComponent::Size (valores em unidades de
    // mundo, independentes da Scale). ConvexHull/TriangleMesh NAO usam Size:
    // a forma vem da malha do MeshRendererComponent da MESMA entidade
    // (modelo importado ou primitiva), multiplicada pela escala de mundo da
    // entidade - o botao "Ajustar a malha" da Properties panel so existe
    // para as formas parametricas.
    //   ConvexHull   - envoltoria convexa da malha. Funciona em qualquer
    //                  BodyType (inclusive Dynamic). Use para props e objetos moveis.
    //   TriangleMesh - a malha exata. Jolt so permite em corpos Static/
    //                  Kinematic: num RigidBody Dynamic a fisica usa
    //                  ConvexHull no lugar (e avisa no log). Use para
    //                  cenario/mapas.
    // Valores NOVOS ficam sempre no FIM do enum: ele e gravado como inteiro
    // no arquivo de cena.
    enum class ColliderShape {
        Box,
        Sphere,
        Capsule,
        ConvexHull,
        TriangleMesh
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
        // raio e Size.y como altura SO DO CILINDRO, sem as calotas (altura
        // total = Size.y + 2*Size.x; Size.z ignorado nesse caso).
        // ConvexHull/TriangleMesh ignoram Size por completo.
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
        float Mass = 1.0f;              // kg - so relevante para Dynamic (ver PhysicsEngine::CreateBodyForEntity, EOverrideMassProperties::CalculateInertia)
        bool UseGravity = true;         // so relevante para Dynamic
        bool ContinuousCollisionDetection = false; // CCD - para objetos rapidos nao atravessarem paredes

        // Atrito de Coulomb (0 = gelo, sem atrito nenhum; 1 = atrito alto,
        // valor tipico de borracha em asfalto). Jolt combina o atrito dos
        // DOIS corpos em contato (por padrao, media geometrica -
        // sqrt(a*b) - ver JPH::PhysicsSystem::SetCombineFriction; a engine
        // nao troca esse combinador, fica no default do Jolt) - entao o
        // atrito EFETIVO de um contato depende dos dois lados, nao so
        // deste. Relevante para Static/Kinematic tambem (uma rampa Static
        // com atrito baixo ainda deixa uma caixa Dynamic escorregar nela).
        float Friction = 0.5f;

        // Restitution ("bounciness"/elasticidade): 0 = nao quica nada
        // (perde toda energia no impacto, ex: uma caixa de papelao), 1 =
        // quicaria mantendo toda a energia (uma bola de borracha ideal -
        // fisicamente irreal na pratica por perdas numericas, mas util
        // como extremo). Assim como Friction, Jolt combina os dois lados
        // do contato (default: pega o MAIOR dos dois - ver
        // SetCombineRestitution) - uma bola bem elastica ainda quica bem
        // mesmo caindo num chao com Restitution 0.
        float Restitution = 0.0f;

        // Damping ("arrasto"/resistencia) aplicado pelo PROPRIO Jolt a
        // cada step de simulacao, simulando perda de energia por atrito
        // com o ar/fluido - diferente de Friction (que so age em
        // CONTATOS entre corpos), Damping desacelera um corpo Dynamic
        // mesmo no ar, sem tocar em nada. LinearDamping reduz a
        // velocidade linear (o corpo "flutua" ate parar de se mover sem
        // forca nenhuma agindo); AngularDamping reduz a velocidade
        // angular do mesmo jeito. Valores pequenos (0.0-0.1) sao o normal
        // para a maioria dos objetos "solidos"; 0 desliga o efeito
        // completamente (comportamento identico ao que a engine tinha
        // antes deste campo existir). So relevante para Dynamic - Jolt
        // ignora damping em corpos Static/Kinematic (eles nao sao
        // integrados pela simulacao de qualquer forma).
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;

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

    // --- Vinculo vivo de PREFAB (ver Scene/PrefabSyncer.h para a logica) ---
    // Igual em espirito ao vinculo vivo de MATERIAL (MaterialComponent::
    // LinkedAsset), mas para uma SUBARVORE inteira de entidades em vez de
    // um struct unico - por isso sao DOIS components, nao um so:
    //
    //   PrefabInstanceRootComponent: so na RAIZ da subarvore instanciada.
    //   "Esta entidade e a raiz de uma instancia do prefab X."
    //
    //   PrefabInstanceMemberComponent: em TODA entidade da subarvore
    //   (raiz inclusive) - "esta entidade corresponde ao indice N dentro
    //   do arquivo .prismprefab de origem". E o que permite ao
    //   PrefabSyncer (ver Scene/PrefabSyncer.h) casar cada entidade da
    //   instancia com a entidade correspondente ao reler o arquivo -
    //   MESMO indice posicional que PrefabSerializer ja usa para o
    //   indice-de-pai (ver CollectSubtree em PrefabSerializer.cpp).
    //
    // NENHUM dos dois entra na comparacao de "este component esta
    // overridado?" (ver PrefabSyncer.h) - sao meta-dados do VINCULO em
    // si, nao conteudo visual/de gameplay do prefab. Por isso ficam de
    // fora de ComponentRegistry::GetAll() tambem (ver comentario grande
    // em ComponentRegistration.cpp sobre o que NAO e registrado la e por
    // que) - SceneSerializer::Serialize/Deserialize os trata como um
    // bloco a parte, do mesmo jeito que ja faz com RelationshipComponent.
    struct PrefabInstanceRootComponent {
        AssetID SourceAsset; // o .prismprefab de origem (invalido = nunca deveria acontecer numa entidade com este component - ver PrefabSyncer)

        PrefabInstanceRootComponent() = default;
        PrefabInstanceRootComponent(const PrefabInstanceRootComponent&) = default;
    };

    struct PrefabInstanceMemberComponent {
        uint32_t IndexInPrefab = 0; // indice posicional dentro do .prismprefab (0 = a propria raiz, por construcao de PrefabSerializer::CollectSubtree)

        PrefabInstanceMemberComponent() = default;
        PrefabInstanceMemberComponent(const PrefabInstanceMemberComponent&) = default;
    };

    // Slot de script anexado a uma entidade. Guarda QUAL arquivo .lua esta
    // associado a ela; quem carrega e executa e o ScriptEngine (ver
    // Scripting/ScriptEngine.h), durante o Play.
    // Tipo de uma variavel publica de script (ver ScriptComponent::Properties).
    enum class ScriptPropertyType : uint8_t {
        Number = 0,
        Bool = 1,
        String = 2
    };

    // Valor de uma variavel publica: o script declara os padroes numa tabela
    // global `Properties = { speed = 5, name = "x", enabled = true }`, e a
    // entidade guarda aqui apenas os valores que o usuario MUDOU no editor
    // (overrides). Aplicados sobre a tabela `Properties` do script antes do
    // OnCreate (ver ScriptEngine::LoadScript).
    struct ScriptPropertyValue {
        std::string Name;
        ScriptPropertyType Type = ScriptPropertyType::Number;
        double Number = 0.0;
        bool Bool = false;
        std::string String;
    };

    struct ScriptComponent {
        // Caminho RELATIVO a Project::GetScriptDirectory() (ex:
        // "player_controller.lua") - nao absoluto, para o projeto
        // continuar portavel entre maquinas/pastas.
        std::string ScriptPath;

        // Overrides das variaveis publicas do script (ver acima).
        std::vector<ScriptPropertyValue> Properties;

        ScriptComponent() = default;
        ScriptComponent(const ScriptComponent&) = default;
        ScriptComponent(const std::string& scriptPath) : ScriptPath(scriptPath) {}
    };

    // Script em C++ (ver Scripting/NativeScript.h): nome da classe registrada
    // com PRISM_REGISTER_SCRIPT no modulo do projeto. A instancia so existe
    // durante o Play; aqui fica apenas o nome (se o modulo nao tiver a classe,
    // o Play avisa no Console e a entidade segue sem script).
    struct NativeScriptComponent {
        std::string ClassName;

        NativeScriptComponent() = default;
        NativeScriptComponent(const NativeScriptComponent&) = default;
        NativeScriptComponent(const std::string& className) : ClassName(className) {}
    };

    // Emissor de particulas (billboards desenhados com blend). A simulacao e
    // na CPU (ver Particles/ParticleSystem.h) e as particulas vivem em espaco
    // de MUNDO: se a entidade se move, as ja emitidas ficam para tras. As
    // particulas saem em um cone ao redor do eixo +Y LOCAL da entidade (gire
    // a entidade para apontar o jato). Cores sao de tela (mesmo espaco dos
    // seletores de cor); alpha vai de ColorStart.a a ColorEnd.a ao longo da vida.
    struct ParticleEmitterComponent {
        bool Enabled = true;
        bool PlayOnStart = true;      // comeca a emitir ao iniciar o Play
        bool Loop = true;             // false: emite so durante Duration
        float Duration = 5.0f;        // segundos (so se Loop == false)

        uint32_t MaxParticles = 500;  // teto de particulas vivas (1..10000)
        float EmissionRate = 40.0f;   // particulas por segundo
        uint32_t BurstCount = 0;      // emitidas de uma vez ao (re)comecar

        float LifetimeMin = 0.8f;
        float LifetimeMax = 1.5f;
        float SpeedMin = 1.0f;
        float SpeedMax = 3.0f;
        float SpreadAngle = 25.0f;    // meio-angulo do cone, graus (0 = reto, 180 = esfera)
        float GravityScale = 0.0f;    // multiplica -9.81 em Y (negativo = sobe)

        float SizeStart = 0.30f;
        float SizeEnd = 0.05f;
        glm::vec4 ColorStart = { 1.0f, 0.70f, 0.20f, 1.0f };
        glm::vec4 ColorEnd = { 1.0f, 0.10f, 0.0f, 0.0f };

        bool Additive = true;         // true: soma luz (fogo, brilho); false: alpha normal (fumaca)
        std::string TexturePath;      // opcional, relativo ao projeto; vazio = circulo suave

        ParticleEmitterComponent() = default;
        ParticleEmitterComponent(const ParticleEmitterComponent&) = default;
    };

    // --- UI in-game -----------------------------------------------------------
    // Elemento de interface em espaco de TELA (pixels), desenhado por cima da
    // cena (ver UI/UISystem.h). Uma entidade com este component NAO precisa de
    // Transform util: o layout vem de Anchor/Offset/Size. Filhos (hierarquia)
    // que tambem sao elementos de UI se ancoram dentro do retangulo do pai, e
    // somem quando o pai some. Sem escala por resolucao: 1 unidade = 1 pixel
    // (da janela de Play; no editor, do viewport).
    enum class UIElementType : int {
        Panel = 0,   // retangulo de cor (ou com textura, se TexturePath)
        Text = 1,    // texto UTF-8 (Latin-1 coberto; \n quebra linha)
        Image = 2,   // textura
        Button = 3   // retangulo clicavel com rotulo (Text)
    };

    enum class UIAnchor : int {
        TopLeft = 0, Top, TopRight,
        Left, Center, Right,
        BottomLeft, Bottom, BottomRight,
        Stretch      // preenche o pai, menos a margem 'Offset' em cada lado
    };

    enum class UITextAlign : int {
        Left = 0,
        Center,
        Right
    };

    struct UIElementComponent {
        UIElementType Type = UIElementType::Panel;
        bool Visible = true;
        int Layer = 0;                 // ordem entre elementos RAIZ: maior = por cima

        UIAnchor Anchor = UIAnchor::TopLeft;
        // Margem para DENTRO a partir das bordas ancoradas (positivo = para dentro):
        // TopLeft(16,16) fica a 16px do topo e da esquerda; BottomRight(16,16) a 16px
        // do fundo e da direita. Center: deslocamento a partir do centro (x direita,
        // y para baixo). Stretch: margem em cada lado (Size e ignorado).
        glm::vec2 Offset = { 16.0f, 16.0f };
        glm::vec2 Size = { 200.0f, 50.0f };

        glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };   // fundo (Panel/Button), tint (Image), cor do texto (Text)
        std::string TexturePath;                        // Image/Panel: relativo ao projeto

        // Text/Button
        std::string Text = "Texto";
        float FontSize = 24.0f;                          // altura da fonte em pixels
        UITextAlign Align = UITextAlign::Left;           // Button: sempre centralizado
        glm::vec4 TextColor = { 1.0f, 1.0f, 1.0f, 1.0f };// rotulo do Button

        // Button
        bool Interactable = true;
        glm::vec4 HoverColor = { 0.85f, 0.85f, 0.85f, 1.0f };   // multiplica Color com o mouse em cima
        glm::vec4 PressedColor = { 0.65f, 0.65f, 0.65f, 1.0f }; // multiplica Color com o botao apertado

        // O elemento barra o mouse para o jogo? (UI.IsPointerOverUI). Texto nao barra.
        bool BlocksPointer = true;

        UIElementComponent() = default;
        UIElementComponent(const UIElementComponent&) = default;
    };

    // Fonte de som anexada a uma entidade. Toca durante o modo Play (ver
    // Audio/AudioEngine.h): comeca sozinha com PlayOnStart, ou por script
    // (entity:PlaySound()). Espacial (3D): volume cai com a distancia ate o
    // AudioListenerComponent (ou a camera primaria, se nao houver listener).
    struct AudioSourceComponent {
        AssetID Clip;                  // asset de audio (.wav/.mp3/.ogg/.flac); invalido = sem som
        float Volume = 1.0f;           // 0..2
        float Pitch = 1.0f;            // 0.1..4 (1 = normal)
        bool Loop = false;
        bool PlayOnStart = true;       // tocar ao iniciar o Play
        bool Spatial = true;           // false = som 2D (ignora posicao, ex: musica/UI)
        float MinDistance = 1.0f;      // ate aqui, volume cheio
        float MaxDistance = 25.0f;     // a partir daqui, mudo (queda linear entre os dois)

        AudioSourceComponent() = default;
        AudioSourceComponent(const AudioSourceComponent&) = default;
    };

    // Marca a entidade cujos "ouvidos" definem o som 3D (posicao/orientacao).
    // Com varios, vale o primeiro Active. Sem nenhum, a camera primaria faz o papel.
    struct AudioListenerComponent {
        bool Active = true;

        AudioListenerComponent() = default;
        AudioListenerComponent(const AudioListenerComponent&) = default;
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

        // Se true (default), o raio IGNORA a entidade PAI desta (se
        // houver) e todas as entidades IRMAS (que compartilham o mesmo
        // pai) - alem da propria entidade dona deste RaycastComponent,
        // que o Jolt ja nunca reporta como hit de si mesma. Existe para
        // o caso classico de sensor de personagem: um RaycastComponent
        // que "olha para frente" a partir de uma entidade FILHA do corpo
        // (ex: a mesma "Camera" usada por
        // example_player_input_raycast.lua) nao deveria acertar o
        // proprio corpo (pai) nem outros colliders filhos do mesmo corpo
        // (ex: um capacete/arma tambem filhos) - sem isso, o sensor
        // ficaria "cego", sempre acertando o proprio personagem a
        // distancia zero. Ver Scene::UpdateRaycastComponents, que monta
        // a lista de entidades a ignorar a partir da hierarquia
        // (Entity::GetParent/RelationshipComponent::Children) toda vez
        // que o raio e testado.
        bool IgnoreParentAndSiblings = true;

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
