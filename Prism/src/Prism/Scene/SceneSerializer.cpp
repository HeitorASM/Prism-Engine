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
    // (novo component, campo novo/removido, mudanca na ordem de
    // registro no ComponentRegistry). Deserialize() recusa arquivos com
    // versao diferente desta - ver comentario no .h. Mapas de versao
    // anterior precisam ser resalvos por uma versao compativel do editor.
    //
    // Historico do formato:
    //   v2  LightComponent, ColliderComponent, RigidBodyComponent e
    //       ScriptComponent (opcionais, com flag de presenca)
    //   v3  CameraComponent
    //   v4  RelationshipComponent (parenting), ver nota abaixo
    //   v5  LightComponent: InnerSpotAngle e CastShadows. Size (reservado
    //       para o tipo Area) nao e salvo: nenhum LightType o usa ainda
    //   v6  RigidBodyComponent: FixedRotation
    //   v7  RaycastComponent (so TargetPosition/Enabled; os campos de
    //       resultado - Hit, HitEntity, HitPoint etc - sao de runtime e
    //       nunca vao para o arquivo)
    //   v8  RigidBodyComponent: Friction, Restitution, LinearDamping,
    //       AngularDamping
    //   v9  RaycastComponent: IgnoreParentAndSiblings
    //   v10 MaterialComponent, registrado entre MeshRendererComponent e
    //       LightComponent (a ordem de registro no ComponentRegistry,
    //       ver ComponentRegistry::GetAll(), determina a ordem no arquivo)
    //
    // Nota sobre o pai (v4): o RelationshipComponent NAO usa flag de
    // presenca; grava so um int32_t por entidade com o indice do pai na
    // ordem de escrita do arquivo (-1 = raiz). O entt::entity bruto nunca
    // e salvo, pois o handle so vale durante a sessao (o EnTT pode
    // reciclar/reordenar handles ao recarregar). O indice posicional e
    // estavel porque Serialize()/Deserialize() sempre iteram/criam
    // entidades na MESMA ordem (ver ForEachEntity). Children tambem nao e
    // salvo: Deserialize() o reconstroi chamando Scene::SetParent() para
    // cada entidade com pai valido, depois que TODAS as entidades ja
    // foram criadas (os handles novos de ambos os lados precisam existir
    // antes de ligar o parentesco).
    static constexpr uint32_t kSceneFormatVersion = 10;
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

    bool SceneSerializer::Serialize(const std::filesystem::path& filepath, bool logSuccess) {
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

        if (logSuccess)
            PRISM_CORE_INFO("Cena '", m_Scene->GetName(), "' salva em: ", filepath.string());
        return true;
    }

    // FNV-1a de 64 bits: nao criptografico, mas de sobra aqui - so
    // precisamos detectar "mudou / nao mudou" (nao ha adversario), e uma
    // colisao acidental entre dois estados de cena reais e ~1 em 2^64.
    static uint64_t Fnv1a64(const char* data, size_t size, uint64_t hash = 14695981039346656037ull) {
        for (size_t i = 0; i < size; i++) {
            hash ^= (uint64_t)(unsigned char)data[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // Mistura dois hashes de 64 bits num terceiro (a ordem dos argumentos
    // IMPORTA - ver uso abaixo, onde "conteudo + pai" tem que diferir de
    // "pai + conteudo"). Constante de proporcao aurea, mesma ideia de
    // boost::hash_combine.
    static uint64_t HashCombine(uint64_t seed, uint64_t value) {
        return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    }

    uint64_t SceneSerializer::ComputeFingerprint() {
        // POR QUE NAO simplesmente hashear o arquivo que Serialize() gera:
        // ForEachEntity itera o registro do EnTT em ordem de criacao
        // INVERTIDA, e Deserialize() recria as entidades na ordem do
        // arquivo - entao cada ciclo salvar -> carregar INVERTE a ordem
        // das entidades. O mesmo mapa, sem nenhuma edicao, gera bytes
        // diferentes a cada ciclo (e o indice do pai, que e posicional,
        // muda junto). Um hash do arquivo inteiro marcaria "alteracoes nao
        // salvas" logo depois de abrir um mapa, ou de salva-lo.
        //
        // Aqui o hash NAO depende da ordem:
        //  1) cada entidade vira um registro independente (tag, transform e
        //     todos os components, pelos MESMOS serializadores do arquivo -
        //     campo novo de component entra sozinho) e recebe seu hash;
        //  2) o PAI entra no hash do filho pelo hash de CONTEUDO do pai, e
        //     nao pelo indice - assim a hierarquia conta, sem depender de
        //     posicao;
        //  3) os hashes de todas as entidades sao SOMADOS (comutativo).
        //
        // Limite conhecido: duas entidades com conteudo 100% identico
        // (mesmo nome, transform e components) sao indistinguiveis. Trocar
        // um filho de pai entre dois "gemeos" perfeitos nao e detectado.
        // Aceitavel: o efeito visivel e nenhum (as duas arvores sao
        // identicas em conteudo).
        ComponentRegistry::RegisterAll();

        std::error_code ec;
        std::filesystem::path tempPath = std::filesystem::temp_directory_path(ec);
        if (ec)
            return 0;
        tempPath /= "prism_scene_fingerprint.tmp";

        // ComponentRegistry so grava em std::ofstream (o registro inteiro
        // esta amarrado a esse tipo), entao usamos UM arquivo temporario,
        // reescrito por entidade. Apagado ao sair, em qualquer caminho.
        struct TempFileGuard {
            std::filesystem::path Path;
            ~TempFileGuard() { std::error_code e; std::filesystem::remove(Path, e); }
        } guard{ tempPath };

        // Grava o registro de UMA entidade no temporario e devolve o hash
        // dele; false se a escrita/leitura falhar.
        auto hashEntityRecord = [&](Entity entity, const TagComponent& tag, uint64_t& outHash) -> bool {
            {
                std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                    return false;

                WriteString(out, tag.Tag);
                auto& transform = entity.GetComponent<TransformComponent>();
                WriteRaw(out, transform.Translation);
                WriteRaw(out, transform.Rotation);
                WriteRaw(out, transform.Scale);

                for (auto& info : ComponentRegistry::GetAll()) {
                    bool has = info.Has(entity);
                    WriteRaw(out, has);
                    if (has)
                        info.Serialize(out, entity);
                }
                if (!out)
                    return false;
            } // fecha (flush) antes de reler

            std::ifstream in(tempPath, std::ios::binary | std::ios::ate);
            if (!in.is_open())
                return false;
            std::streamsize size = in.tellg();
            in.seekg(0, std::ios::beg);
            std::vector<char> data((size_t)size);
            if (size > 0 && !in.read(data.data(), size))
                return false;

            outHash = Fnv1a64(data.data(), data.size());
            return true;
        };

        // Passe 1: hash de conteudo de cada entidade (sem pai).
        std::unordered_map<entt::entity, uint64_t> contentHash;
        bool failed = false;
        m_Scene->ForEachEntity([&](entt::entity handle, TagComponent& tag) {
            if (failed)
                return;
            uint64_t h = 0;
            if (!hashEntityRecord(Entity(handle, m_Scene.get()), tag, h)) {
                failed = true;
                return;
            }
            contentHash[handle] = h;
        });
        if (failed)
            return 0;

        // Passe 2: hash final = conteudo combinado com o conteudo do pai;
        // soma tudo (independe da ordem).
        uint64_t total = 0;
        for (auto& [handle, own] : contentHash) {
            uint64_t parentPart = 0; // 0 = sem pai
            if (auto* rel = m_Scene->GetRegistry().try_get<RelationshipComponent>(handle)) {
                if (rel->Parent != entt::null) {
                    auto it = contentHash.find(rel->Parent);
                    if (it != contentHash.end())
                        parentPart = it->second;
                }
            }
            total += HashCombine(own, parentPart);
        }

        // Nome da cena e quantidade de entidades tambem contam: renomear a
        // cena ou criar/excluir uma entidade de conteudo repetido muda o
        // total. Combinados no fim (nao somados) - ordem fixa e conhecida.
        const std::string& sceneName = m_Scene->GetName();
        uint64_t hash = Fnv1a64(sceneName.data(), sceneName.size());
        hash = HashCombine(hash, (uint64_t)contentHash.size());
        hash = HashCombine(hash, total);

        // 0 e o valor reservado para "falhou ao calcular"; se o hash real
        // cair em 0 (probabilidade ~2^-64), troca por 1 em vez de deixar um
        // resultado valido ser confundido com uma falha.
        return hash == 0 ? 1 : hash;
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
