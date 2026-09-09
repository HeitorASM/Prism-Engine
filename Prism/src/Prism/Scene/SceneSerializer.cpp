#include "SceneSerializer.h"
#include "Scene.h"
#include "Entity.h"
#include "ComponentRegistry.h"
#include "../Core/Log.h"

#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Prism {

    // Incrementar sempre que o layout binario mudar de forma incompativel
    // (novo component obrigatorio, campo removido, etc). Deserialize()
    // recusa arquivos com versao diferente desta - ver comentario no .h.
    //
    // v1 -> v2: adicionados LightComponent, ColliderComponent,
    // RigidBodyComponent, ScriptComponent (todos opcionais, mesmo padrao de
    // flag-de-presenca que MeshRendererComponent ja usava). Arquivos v1 nao
    // sao lidos por este parser (ver Deserialize) - projetos criados antes
    // desta mudanca precisam ser resalvos uma vez.
    // v1 -> v2: adicionados LightComponent, ColliderComponent,
    // RigidBodyComponent, ScriptComponent (todos opcionais, mesmo padrao de
    // flag-de-presenca que MeshRendererComponent ja usava). Arquivos v1 nao
    // sao lidos por este parser (ver Deserialize) - projetos criados antes
    // desta mudanca precisam ser resalvos uma vez.
    //
    // v2 -> v3: adicionado CameraComponent (opcional, mesmo padrao de flag
    // de presenca). Arquivos v2 nao sao lidos por este parser - mapas
    // salvos antes desta mudanca precisam ser resalvos uma vez.
    //
    // v3 -> v4: adicionado RelationshipComponent (parenting). Diferente
    // dos outros components opcionais, NAO usa flag de presenca + campos -
    // usa so um int32_t por entidade (indice do pai na ordem de escrita
    // deste arquivo, -1 = sem pai/raiz). entt::entity bruto NUNCA e salvo
    // (o handle e so valido durante a sessao atual - ao recarregar, o
    // EnTT pode reciclar/reordenar handles livremente); o indice posicional
    // e estavel porque Serialize()/Deserialize() sempre iteram/criam
    // entidades na MESMA ordem (ver ForEachEntity). Children nao e salvo
    // separado - Deserialize() reconstroi Children chamando
    // Scene::SetParent() para cada entidade que tem um Parent valido,
    // depois que TODAS as entidades ja foram criadas (precisa dos handles
    // novos de ambos os lados existirem antes de ligar o parentesco).
    //
    // v4 -> v5: LightComponent ganhou InnerSpotAngle (soft edge do cone
    // do Spot) e CastShadows (reservado - ver Components.h/Renderer, o
    // Renderer ainda nao produz sombra nenhuma). Size (reservado para o
    // futuro tipo Area) DELIBERADAMENTE nao e salvo ainda - nenhum
    // LightType usa esse campo hoje, entao gravar seria so ruido no
    // arquivo; sera adicionado ao formato quando Area for implementado.
    // Arquivos v4 nao sao lidos por este parser - mapas salvos antes
    // desta mudanca precisam ser resalvos uma vez (mesmo padrao de todo
    // bump anterior, ver comentarios acima).
    //
    // v5 -> v6: RigidBodyComponent ganhou FixedRotation (trava as 3
    // rotacoes fisicas do corpo - ver Components.h e o comentario de
    // PhysicsEngine::Simulate sobre por que isto e necessario para
    // camera/player controlados por script). Arquivos v5 nao sao lidos
    // por este parser - mapas salvos antes desta mudanca precisam ser
    // resalvos uma vez (mesmo padrao de todo bump anterior).
    //
    // v6 -> v7: adicionado RaycastComponent (opcional, mesmo padrao de
    // flag de presenca). So TargetPosition/Enabled sao gravados - os
    // campos de resultado (Hit/HitEntity/HitPoint/HitNormal/HitDistance)
    // sao transientes de runtime e nunca vao para o arquivo (ver
    // comentario no bloco de Serialize/Deserialize). Arquivos v6 nao sao
    // lidos por este parser - mapas salvos antes desta mudanca precisam
    // ser resalvos uma vez (mesmo padrao de todo bump anterior).
    //
    // v7 -> v8: RigidBodyComponent ganhou Friction/Restitution/
    // LinearDamping/AngularDamping - ate aqui esses 4 valores existiam
    // (com defaults fixos) mas nunca chegavam ao Jolt nem ao arquivo;
    // agora alimentam a simulacao de verdade (ver
    // PhysicsEngine::CreateBodyForEntity) e por isso precisam ser salvos,
    // ou um mapa recarregado perderia o ajuste fino do usuario e voltaria
    // silenciosamente para os defaults. Arquivos v7 nao sao lidos por
    // este parser - mapas salvos antes desta mudanca precisam ser
    // resalvos uma vez (mesmo padrao de todo bump anterior).
    static constexpr uint32_t kSceneFormatVersion = 8;
    static constexpr char kMagic[4] = { 'P', 'R', 'S', 'M' };

    SceneSerializer::SceneSerializer(Ref<Scene> scene) : m_Scene(scene) {}

    // --- helpers de escrita/leitura binaria -----------------------------
    // Migrados para ComponentRegistry (ver ComponentRegistry.h) - agora
    // compartilhados com os blocos de Serialize/Deserialize de cada
    // Component (ComponentRegistration.cpp), que precisam exatamente das
    // mesmas rotinas. ComponentRegistry::WriteRaw/ReadRaw/WriteString/ReadString
    // sao METODOS ESTATICOS DE CLASSE, nao funcoes livres em namespace -
    // 'using Class::member' so e valido dentro de outra classe (para
    // herança), nao em escopo de namespace solto como este arquivo - por
    // isso usamos wrappers finos (chamando o metodo da classe) em vez de
    // 'using', para nao precisar prefixar ComponentRegistry:: em toda
    // chamada existente abaixo.
    static void WriteString(std::ofstream& out, const std::string& str) {
        ComponentRegistry::WriteString(out, str);
    }
    static bool ReadString(std::ifstream& in, std::string& outStr) {
        return ComponentRegistry::ReadString(in, outStr);
    }
    template<typename T>
    static void WriteRaw(std::ofstream& out, const T& value) {
        ComponentRegistry::WriteRaw(out, value);
    }
    template<typename T>
    static bool ReadRaw(std::ifstream& in, T& value) {
        return ComponentRegistry::ReadRaw(in, value);
    }

    // --- Serialize --------------------------------------------------------

    bool SceneSerializer::Serialize(const std::filesystem::path& filepath) {
        // Garante que ComponentRegistry::GetAll() (usado abaixo, ver
        // ComponentRegistry.h/ComponentRegistration.cpp) ja esta populado
        // - chamado aqui, e nao so uma vez no bootstrap do editor, para
        // que Serialize/Deserialize funcionem corretamente mesmo se
        // chamados antes de qualquer inicializacao explicita do editor
        // rodar (ex: um futuro teste automatizado que so cria uma Scene e
        // salva, sem passar por EditorLayer::OnAttach) - RegisterAll() e
        // idempotente (ver comentario la), entao chamar aqui em toda
        // Serialize/Deserialize e barato e seguro.
        ComponentRegistry::RegisterAll();

        std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            PRISM_CORE_ERROR("SceneSerializer: nao foi possivel criar o arquivo de cena: ", filepath.string());
            return false;
        }

        out.write(kMagic, sizeof(kMagic));
        WriteRaw(out, kSceneFormatVersion);
        WriteString(out, m_Scene->GetName());

        // Conta entidades primeiro (ForEachEntity nao expoe o total
        // diretamente) para escrever o cabecalho antes dos dados. Ao mesmo
        // tempo, monta o mapa handle -> indice posicional (0, 1, 2...) na
        // MESMA ordem que o loop de escrita abaixo vai seguir - e esse
        // indice, nao o entt::entity bruto, que vira o "id do pai" no
        // arquivo (ver comentario de v3->v4 em kSceneFormatVersion acima).
        uint32_t entityCount = 0;
        std::unordered_map<entt::entity, int32_t> handleToIndex;
        m_Scene->ForEachEntity([&](entt::entity handle, TagComponent&) {
            handleToIndex[handle] = (int32_t)entityCount;
            entityCount++;
        });
        WriteRaw(out, entityCount);

        bool writeFailed = false;
        m_Scene->ForEachEntity([&](entt::entity handle, TagComponent& tag) {
            Entity entity(handle, m_Scene.get());

            WriteString(out, tag.Tag);

            // TransformComponent: toda entidade tem (garantido por
            // Scene::CreateEntity) - nao precisa de flag de presenca.
            auto& transform = entity.GetComponent<TransformComponent>();
            WriteRaw(out, transform.Translation);
            WriteRaw(out, transform.Rotation);
            WriteRaw(out, transform.Scale);

            // MeshRendererComponent, LightComponent, ColliderComponent,
            // RigidBodyComponent, ScriptComponent, CameraComponent,
            // RaycastComponent - todos "Components opcionais" com o mesmo
            // padrao (flag de presenca de 1 byte + campos), agora
            // escritos via ComponentRegistry em vez de um bloco manual
            // por Component aqui (ver comentario grande em
            // ComponentRegistry.h/ComponentRegistration.cpp sobre o
            // motivo desta mudanca - o FORMATO BINARIO GERADO E IDENTICO
            // ao de antes, byte a byte, so o CODIGO que gera cada bloco
            // mudou de lugar).
            for (auto& info : ComponentRegistry::GetAll()) {
                bool has = info.Has(entity);
                WriteRaw(out, has);
                if (has)
                    info.Serialize(out, entity);
            }

            // RelationshipComponent - adicionado na v4 do formato. So o
            // indice do PAI e gravado (ver comentario de kSceneFormatVersion);
            // -1 quando a entidade nao tem RelationshipComponent ou nao tem
            // pai (e raiz). Um pai que aponta para fora deste mapa (nao
            // deveria acontecer, ja que so entidades da propria Scene podem
            // virar pai via Scene::SetParent) tambem vira -1 em vez de
            // gravar lixo.
            int32_t parentIndex = -1;
            if (auto* rel = m_Scene->GetRegistry().try_get<RelationshipComponent>(handle)) {
                if (rel->Parent != entt::null) {
                    auto it = handleToIndex.find(rel->Parent);
                    if (it != handleToIndex.end())
                        parentIndex = it->second;
                }
            }
            WriteRaw(out, parentIndex);

            if (!out) writeFailed = true;
        });

        if (writeFailed) {
            PRISM_CORE_ERROR("SceneSerializer: falha ao escrever dados da cena em: ", filepath.string());
            return false;
        }

        PRISM_CORE_INFO("Cena '", m_Scene->GetName(), "' salva em: ", filepath.string());
        return true;
    }

    // --- Deserialize --------------------------------------------------------

    bool SceneSerializer::Deserialize(const std::filesystem::path& filepath) {
        ComponentRegistry::RegisterAll(); // ver comentario equivalente em Serialize()

        std::ifstream in(filepath, std::ios::binary);
        if (!in.is_open()) {
            PRISM_CORE_ERROR("SceneSerializer: nao foi possivel abrir o arquivo de cena: ", filepath.string());
            return false;
        }

        char magic[4];
        in.read(magic, sizeof(magic));
        if (!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
            PRISM_CORE_ERROR("SceneSerializer: '", filepath.string(), "' nao e um arquivo de cena Prism valido (magic incorreto).");
            return false;
        }

        uint32_t version = 0;
        if (!ReadRaw(in, version)) {
            PRISM_CORE_ERROR("SceneSerializer: falha ao ler versao do arquivo: ", filepath.string());
            return false;
        }
        if (version != kSceneFormatVersion) {
            // Sem migracao entre versoes por enquanto (ver comentario no
            // .h) - melhor recusar claramente do que interpretar bytes com
            // o layout errado e corromper a cena silenciosamente.
            PRISM_CORE_ERROR("SceneSerializer: versao de formato incompativel em '", filepath.string(),
                "' (arquivo=v", version, ", esperado=v", kSceneFormatVersion, ").");
            return false;
        }

        std::string sceneName;
        if (!ReadString(in, sceneName)) {
            PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (nome): ", filepath.string());
            return false;
        }

        uint32_t entityCount = 0;
        if (!ReadRaw(in, entityCount)) {
            PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (contagem de entidades): ", filepath.string());
            return false;
        }

        // Sanidade: um arquivo truncado/corrompido pode fazer este campo
        // vir com um valor absurdo (bytes de lixo interpretados como
        // uint32_t). Sem este teto, o loop abaixo tentaria criar milhoes
        // de entidades e cada ReadString() dentro dele tentaria alocar
        // memoria para dados que nao existem no arquivo - e exatamente
        // esse tipo de leitura descontrolada que causa abort() por
        // corrupcao de heap no CRT em modo Debug.
        constexpr uint32_t kMaxReasonableEntityCount = 1'000'000;
        if (entityCount > kMaxReasonableEntityCount) {
            PRISM_CORE_ERROR("SceneSerializer: contagem de entidades absurda (", entityCount,
                ") em '", filepath.string(), "' - arquivo provavelmente corrompido/truncado.");
            return false;
        }

        // So substituimos o estado da Scene depois de ler tudo com sucesso
        // (ver loop abaixo) - assim uma leitura que falha no meio nao deixa
        // a Scene ativa pela metade.
        Ref<Scene> loaded = Scene::Create(sceneName);

        // parentIndices[i] = indice do pai da entidade i no arquivo (-1 =
        // sem pai). Resolvido para handles/SetParent DEPOIS que todas as
        // entidades existirem (ver loop separado apos este) - nao da para
        // chamar SetParent no meio deste loop porque o pai de uma entidade
        // pode aparecer DEPOIS dela no arquivo (ordem de escrita nao
        // garante pai-antes-do-filho).
        std::vector<int32_t> parentIndices;
        parentIndices.reserve(entityCount);
        std::vector<Entity> createdEntities;
        createdEntities.reserve(entityCount);

        for (uint32_t i = 0; i < entityCount; i++) {
            std::string tag;
            if (!ReadString(in, tag)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (tag da entidade ", i, "): ", filepath.string());
                return false;
            }

            Entity entity = loaded->CreateEntity(tag);
            auto& transform = entity.GetComponent<TransformComponent>();

            bool ok = ReadRaw(in, transform.Translation)
                   && ReadRaw(in, transform.Rotation)
                   && ReadRaw(in, transform.Scale);
            if (!ok) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (transform da entidade ", i, "): ", filepath.string());
                return false;
            }

            // MeshRendererComponent, LightComponent, ColliderComponent,
            // RigidBodyComponent, ScriptComponent, CameraComponent,
            // RaycastComponent - lidos via ComponentRegistry, na MESMA
            // ordem em que Serialize() os escreveu acima (ComponentRegistry::GetAll()
            // retorna sempre a mesma ordem de registro, ver
            // ComponentRegistration.cpp) - ver comentario grande no bloco
            // correspondente de Serialize() sobre o motivo desta mudanca.
            for (auto& info : ComponentRegistry::GetAll()) {
                bool has = false;
                if (!ReadRaw(in, has)) {
                    PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (flag de '", info.DisplayName, "' da entidade ", i, "): ", filepath.string());
                    return false;
                }
                if (has && !info.Deserialize(in, entity, i))
                    return false; // mensagem de erro especifica ja foi logada dentro do proprio Deserialize (ver ComponentRegistration.cpp)
            }

            // RelationshipComponent - so o indice do pai (ver comentario
            // de kSceneFormatVersion/v3->v4 no topo do arquivo). Guardado
            // aqui, resolvido em SetParent() so depois que TODAS as
            // entidades desta cena existirem (loop logo abaixo).
            int32_t parentIndex = -1;
            if (!ReadRaw(in, parentIndex)) {
                PRISM_CORE_ERROR("SceneSerializer: arquivo de cena corrompido (indice de pai da entidade ", i, "): ", filepath.string());
                return false;
            }
            parentIndices.push_back(parentIndex);
            createdEntities.push_back(entity);
        }

        // Segunda passada: resolve os indices de pai em chamadas reais de
        // SetParent, agora que createdEntities[i] existe para qualquer i
        // (inclusive indices que aparecem "a frente" no arquivo). Indices
        // fora do range [0, entityCount) sao tratados como -1 (sem pai) -
        // protecao extra contra um arquivo corrompido/de origem duvidosa,
        // ja que aceitar um indice invalido aqui acessaria
        // createdEntities fora dos limites.
        for (uint32_t i = 0; i < entityCount; i++) {
            int32_t parentIndex = parentIndices[i];
            if (parentIndex < 0 || (uint32_t)parentIndex >= entityCount)
                continue;
            loaded->SetParent(createdEntities[i], createdEntities[(uint32_t)parentIndex]);
        }

        m_Scene = loaded;
        PRISM_CORE_INFO("Cena '", m_Scene->GetName(), "' carregada de: ", filepath.string(), " (", entityCount, " entidades)");
        return true;
    }

}
