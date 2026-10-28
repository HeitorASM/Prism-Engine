// SceneTests.cpp - serializacao de cena, ComponentRegistry e PrefabSyncer.
// Diferente dos outros testes, este LINKA a biblioteca Prism inteira (Scene
// depende de Fisica, Audio, Scripts...), entao so roda num build completo:
//   cmake --build build --target PrismSceneTests && ctest --test-dir build -R SceneTests
// Nao abre janela nem usa GPU (so ECS, memoria e arquivos temporarios).
#include <Prism/Scene/Scene.h>
#include <Prism/Scene/Entity.h>
#include <Prism/Scene/Components.h>
#include <Prism/Scene/ComponentRegistry.h>
#include <Prism/Scene/SceneSerializer.h>
#include <Prism/Scene/MaterialSerializer.h>
#include <Prism/Scene/PrefabSerializer.h>
#include <Prism/Scene/PrefabSyncer.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Prism;
namespace fs = std::filesystem;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string ReadFileBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
static void WriteFileBytes(const fs::path& p, const std::string& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(b.data(), (std::streamsize)b.size());
}

// Monta a stream de UMA entidade com um unico bloco, como ReadComponentBlocks espera.
static std::string MakeBlockStream(const std::string& name, uint32_t version, const std::string& bytes) {
    std::ostringstream out(std::ios::out | std::ios::binary);
    ComponentRegistry::WriteRaw(out, (uint32_t)1);
    ComponentRegistry::WriteString(out, name);
    ComponentRegistry::WriteRaw(out, version);
    ComponentRegistry::WriteRaw(out, (uint32_t)bytes.size());
    out.write(bytes.data(), (std::streamsize)bytes.size());
    return out.str();
}

static const ComponentTypeInfo* FindInfo(const std::string& name) {
    for (auto& i : ComponentRegistry::GetAll()) if (i.DisplayName == name) return &i;
    return nullptr;
}

static void TestRegistryBasics() {
    ComponentRegistry::RegisterAll();
    ComponentRegistry::RegisterAll(); // idempotente
    const auto& all = ComponentRegistry::GetAll();
    CHECK(!all.empty());
    for (size_t i = 0; i < all.size(); i++) {
        CHECK(!all[i].DisplayName.empty());
        CHECK(all[i].Version >= 1);
        CHECK(all[i].AddedInSceneFormat <= 64); // sanidade: nenhum valor absurdo
        for (size_t j = i + 1; j < all.size(); j++)
            CHECK(all[i].DisplayName != all[j].DisplayName); // nome duplicado quebraria a leitura
    }
    // os componentes da fisica (passo 5) estao registrados
    CHECK(FindInfo("Joint") != nullptr);
    CHECK(FindInfo("Character Controller") != nullptr);
    CHECK(FindInfo("Collider") != nullptr);

    // helpers de string
    std::ostringstream out(std::ios::out | std::ios::binary);
    ComponentRegistry::WriteString(out, "");
    ComponentRegistry::WriteString(out, "acentuação ç");
    std::istringstream in(out.str(), std::ios::in | std::ios::binary);
    std::string a = "x", b;
    CHECK(ComponentRegistry::ReadString(in, a) && a.empty());
    CHECK(ComponentRegistry::ReadString(in, b) && b == "acentuação ç");
    CHECK(!ComponentRegistry::ReadString(in, a)); // fim da stream

    // tamanho absurdo e recusado sem alocar
    std::ostringstream big(std::ios::out | std::ios::binary);
    ComponentRegistry::WriteRaw(big, (uint32_t)0x7FFFFFFF);
    std::istringstream bigIn(big.str(), std::ios::in | std::ios::binary);
    CHECK(!ComponentRegistry::ReadString(bigIn, a));
}

// Para cada componente: padrao -> bytes -> outra entidade -> bytes iguais.
static void TestEveryComponentRoundTrips() {
    auto scene = Scene::Create("RoundTrip");
    for (auto& info : ComponentRegistry::GetAll()) {
        Entity src = scene->CreateEntity("src");
        info.AddDefault(src);
        if (!info.Has(src)) { std::printf("(aviso) AddDefault nao adicionou '%s'\n", info.DisplayName.c_str()); continue; }

        std::ostringstream first(std::ios::out | std::ios::binary);
        info.Serialize(first, src);

        Entity dst = scene->CreateEntity("dst");
        std::istringstream in(first.str(), std::ios::in | std::ios::binary);
        bool ok = info.Deserialize(in, dst, 0);
        CHECK(ok);
        CHECK(info.Has(dst));

        std::ostringstream second(std::ios::out | std::ios::binary);
        if (info.Has(dst)) info.Serialize(second, dst);
        if (first.str() != second.str()) std::printf("  round-trip difere em '%s'\n", info.DisplayName.c_str());
        CHECK(first.str() == second.str());

        // Copy (usado por Duplicar/Prefab) tambem preserva os bytes
        Entity copy = scene->CreateEntity("copy");
        info.Copy(src, copy);
        std::ostringstream third(std::ios::out | std::ios::binary);
        if (info.Has(copy)) info.Serialize(third, copy);
        CHECK(first.str() == third.str());
    }
}

// Nenhum prefixo truncado dos bytes de um componente pode travar/crashar:
// ou le (campos opcionais no fim) ou devolve false.
static void TestTruncatedBlocksNeverCrash() {
    auto scene = Scene::Create("Trunc");
    for (auto& info : ComponentRegistry::GetAll()) {
        Entity src = scene->CreateEntity("src");
        info.AddDefault(src);
        if (!info.Has(src)) continue;
        std::ostringstream out(std::ios::out | std::ios::binary);
        info.Serialize(out, src);
        const std::string bytes = out.str();
        for (size_t n = 0; n < bytes.size(); n++) {
            Entity dst = scene->CreateEntity("dst");
            std::istringstream in(bytes.substr(0, n), std::ios::in | std::ios::binary);
            (void)info.Deserialize(in, dst, 0); // so nao pode crashar
            CHECK(true);
        }
    }
}

static void TestBlockStreamFraming() {
    auto scene = Scene::Create("Blocks");
    Entity e = scene->CreateEntity("e");
    e.AddComponent<LightComponent>().Intensity = 3.5f;
    e.AddComponent<ColliderComponent>().Layer = 7;

    std::ostringstream out(std::ios::out | std::ios::binary);
    ComponentRegistry::WriteComponentBlocks(out, e);

    Entity f = scene->CreateEntity("f");
    std::istringstream in(out.str(), std::ios::in | std::ios::binary);
    CHECK(ComponentRegistry::ReadComponentBlocks(in, f, 0));
    CHECK(f.HasComponent<LightComponent>() && f.GetComponent<LightComponent>().Intensity == 3.5f);
    CHECK(f.HasComponent<ColliderComponent>() && f.GetComponent<ColliderComponent>().Layer == 7);

    // componente desconhecido: ignorado, o resto da stream segue valido
    std::ostringstream mixed(std::ios::out | std::ios::binary);
    ComponentRegistry::WriteRaw(mixed, (uint32_t)2);
    ComponentRegistry::WriteString(mixed, "ComponenteDoFuturo");
    ComponentRegistry::WriteRaw(mixed, (uint32_t)9);
    ComponentRegistry::WriteRaw(mixed, (uint32_t)3);
    mixed.write("abc", 3);
    {
        std::ostringstream lightBytes(std::ios::out | std::ios::binary);
        FindInfo("Light")->Serialize(lightBytes, e);
        const std::string lb = lightBytes.str();
        ComponentRegistry::WriteString(mixed, "Light");
        ComponentRegistry::WriteRaw(mixed, FindInfo("Light")->Version);
        ComponentRegistry::WriteRaw(mixed, (uint32_t)lb.size());
        mixed.write(lb.data(), (std::streamsize)lb.size());
    }
    Entity g = scene->CreateEntity("g");
    std::istringstream mixedIn(mixed.str(), std::ios::in | std::ios::binary);
    CHECK(ComponentRegistry::ReadComponentBlocks(mixedIn, g, 0));
    CHECK(g.HasComponent<LightComponent>());

    // truncada / contagem absurda: false, sem crash
    std::string cut = out.str().substr(0, out.str().size() - 3);
    Entity h = scene->CreateEntity("h");
    std::istringstream cutIn(cut, std::ios::in | std::ios::binary);
    CHECK(!ComponentRegistry::ReadComponentBlocks(cutIn, h, 0));
    std::ostringstream absurd(std::ios::out | std::ios::binary);
    ComponentRegistry::WriteRaw(absurd, (uint32_t)100000);
    Entity k = scene->CreateEntity("k");
    std::istringstream absurdIn(absurd.str(), std::ios::in | std::ios::binary);
    CHECK(!ComponentRegistry::ReadComponentBlocks(absurdIn, k, 0));
    std::istringstream emptyIn("", std::ios::in | std::ios::binary);
    CHECK(!ComponentRegistry::ReadComponentBlocks(emptyIn, k, 0));
}

// Cenas antigas: blocos v1 (sem os campos acrescentados no fim) continuam abrindo.
static void TestOldBlockVersionsStillLoad() {
    auto scene = Scene::Create("Compat");

    { // Collider v1 = v2 sem o Layer final (4 bytes)
        Entity src = scene->CreateEntity("src");
        auto& c = src.AddComponent<ColliderComponent>();
        c.Layer = 9; c.IsTrigger = true;
        std::ostringstream out(std::ios::out | std::ios::binary);
        FindInfo("Collider")->Serialize(out, src);
        std::string bytes = out.str();
        bytes.resize(bytes.size() - sizeof(int32_t));
        Entity dst = scene->CreateEntity("dst");
        std::istringstream in(MakeBlockStream("Collider", 1, bytes), std::ios::in | std::ios::binary);
        CHECK(ComponentRegistry::ReadComponentBlocks(in, dst, 0));
        CHECK(dst.HasComponent<ColliderComponent>());
        CHECK(dst.GetComponent<ColliderComponent>().Layer == 0);
        CHECK(dst.GetComponent<ColliderComponent>().IsTrigger);
        CHECK(ComponentRegistry::CurrentBlockVersion() == FindInfo("Collider")->Version); // restaurado
    }
    { // Raycast v1 = v2 sem o LayerMask final
        Entity src = scene->CreateEntity("src");
        src.AddComponent<RaycastComponent>().LayerMask = 0x5u;
        std::ostringstream out(std::ios::out | std::ios::binary);
        FindInfo("Raycast")->Serialize(out, src);
        std::string bytes = out.str();
        bytes.resize(bytes.size() - sizeof(uint32_t));
        Entity dst = scene->CreateEntity("dst");
        std::istringstream in(MakeBlockStream("Raycast", 1, bytes), std::ios::in | std::ios::binary);
        CHECK(ComponentRegistry::ReadComponentBlocks(in, dst, 0));
        CHECK(dst.HasComponent<RaycastComponent>());
        CHECK(dst.GetComponent<RaycastComponent>().LayerMask == 0xFFFFFFFFu);
    }
    { // valores fora do intervalo sao saneados
        Entity src = scene->CreateEntity("src");
        src.AddComponent<ColliderComponent>().Layer = 500;
        std::ostringstream out(std::ios::out | std::ios::binary);
        FindInfo("Collider")->Serialize(out, src);
        Entity dst = scene->CreateEntity("dst");
        std::istringstream in(MakeBlockStream("Collider", FindInfo("Collider")->Version, out.str()), std::ios::in | std::ios::binary);
        CHECK(ComponentRegistry::ReadComponentBlocks(in, dst, 0));
        CHECK(!dst.HasComponent<ColliderComponent>() || (dst.GetComponent<ColliderComponent>().Layer >= 0 && dst.GetComponent<ColliderComponent>().Layer < 32));
    }
}

static void TestMaterialSurface(const fs::path& dir) {
    auto scene = Scene::Create("Materiais");

    // padrao: opaco, sem emissao, UV neutro
    MaterialComponent def;
    CHECK(def.BlendMode == MaterialBlendMode::Opaque && def.Opacity == 1.0f && def.AlphaCutoff == 0.5f);
    CHECK(def.UVTiling == glm::vec2(1.0f) && def.UVOffset == glm::vec2(0.0f));
    CHECK(def.EmissiveColor == glm::vec3(0.0f) && !def.DoubleSided);

    // bloco do registro: round-trip dos campos novos
    Entity src = scene->CreateEntity("src");
    auto& m = src.AddComponent<MaterialComponent>();
    m.AlbedoPath = "a.png";
    m.BlendMode = MaterialBlendMode::Blend;
    m.Opacity = 0.25f; m.AlphaCutoff = 0.7f;
    m.UVTiling = { 3.0f, 2.0f }; m.UVOffset = { 0.5f, -0.25f };
    m.EmissiveColor = { 1.0f, 0.5f, 0.0f }; m.EmissiveIntensity = 4.0f;
    m.DoubleSided = true;
    std::ostringstream out(std::ios::out | std::ios::binary);
    FindInfo("Material")->Serialize(out, src);
    Entity dst = scene->CreateEntity("dst");
    std::istringstream in(MakeBlockStream("Material", FindInfo("Material")->Version, out.str()), std::ios::in | std::ios::binary);
    CHECK(ComponentRegistry::ReadComponentBlocks(in, dst, 0));
    CHECK(dst.HasComponent<MaterialComponent>());
    if (dst.HasComponent<MaterialComponent>()) {
        const auto& r = dst.GetComponent<MaterialComponent>();
        CHECK(r.AlbedoPath == "a.png" && r.BlendMode == MaterialBlendMode::Blend);
        CHECK(r.Opacity == 0.25f && r.AlphaCutoff == 0.7f);
        CHECK(r.UVTiling == glm::vec2(3.0f, 2.0f) && r.UVOffset == glm::vec2(0.5f, -0.25f));
        CHECK(r.EmissiveColor == glm::vec3(1.0f, 0.5f, 0.0f) && r.EmissiveIntensity == 4.0f && r.DoubleSided);
    }

    // bloco v1 (sem os 45 bytes da superficie): campos novos ficam no padrao
    {
        std::string bytes = out.str();
        bytes.resize(bytes.size() - (sizeof(int) + 4 * sizeof(float) + 2 * sizeof(float) + 3 * sizeof(float) + sizeof(float) + sizeof(bool) - 0));
        Entity old = scene->CreateEntity("old");
        std::istringstream oldIn(MakeBlockStream("Material", 1, bytes), std::ios::in | std::ios::binary);
        CHECK(ComponentRegistry::ReadComponentBlocks(oldIn, old, 0));
        CHECK(old.HasComponent<MaterialComponent>());
        if (old.HasComponent<MaterialComponent>()) {
            const auto& r = old.GetComponent<MaterialComponent>();
            CHECK(r.AlbedoPath == "a.png");
            CHECK(r.BlendMode == MaterialBlendMode::Opaque && r.Opacity == 1.0f && r.UVTiling == glm::vec2(1.0f) && !r.DoubleSided);
        }
    }

    // saneamento: lixo vira valores validos
    {
        MaterialComponent bad;
        bad.BlendMode = (MaterialBlendMode)77;
        bad.Opacity = std::nanf("");
        bad.AlphaCutoff = 9.0f;
        bad.UVTiling = { INFINITY, -5000.0f };
        bad.EmissiveColor = { -1.0f, 2.0f, std::nanf("") };
        bad.EmissiveIntensity = 1e9f;
        SanitizeMaterialSurface(bad);
        CHECK(bad.BlendMode == MaterialBlendMode::Opaque);
        CHECK(bad.Opacity == 1.0f && bad.AlphaCutoff == 1.0f);
        CHECK(bad.UVTiling.x == 1.0f && bad.UVTiling.y == -1000.0f);
        CHECK(bad.EmissiveColor == glm::vec3(0.0f, 1.0f, 0.0f) && bad.EmissiveIntensity == 100.0f);
    }

    // .prismmat v2: grava e le; arquivo truncado e recusado
    {
        const fs::path file = dir / "mat.prismmat";
        MaterialComponent mat;
        mat.AlbedoTint = { 0.5f, 0.6f, 0.7f };
        mat.BlendMode = MaterialBlendMode::Mask; mat.AlphaCutoff = 0.3f;
        mat.UVTiling = { 4.0f, 4.0f }; mat.EmissiveColor = { 0.0f, 1.0f, 0.0f }; mat.DoubleSided = true;
        CHECK(MaterialSerializer::Serialize(mat, file));
        MaterialComponent loaded;
        CHECK(MaterialSerializer::Deserialize(file, loaded));
        CHECK(loaded.BlendMode == MaterialBlendMode::Mask && loaded.AlphaCutoff == 0.3f);
        CHECK(loaded.UVTiling == glm::vec2(4.0f) && loaded.EmissiveColor == glm::vec3(0.0f, 1.0f, 0.0f) && loaded.DoubleSided);
        CHECK(loaded.AlbedoTint == glm::vec3(0.5f, 0.6f, 0.7f));

        const std::string bytes = ReadFileBytes(file);
        WriteFileBytes(dir / "cortado.prismmat", bytes.substr(0, bytes.size() - 5));
        MaterialComponent partial;
        CHECK(!MaterialSerializer::Deserialize(dir / "cortado.prismmat", partial));
    }
}

static void TestSceneFileRoundTrip(const fs::path& dir) {
    auto scene = Scene::Create("Fase 1");
    Entity root = scene->CreateEntity("Raiz");
    Entity child = scene->CreateEntity("Filho");
    Entity other = scene->CreateEntity("Outro");
    CHECK(scene->SetParent(child, root));
    root.GetComponent<TransformComponent>().Translation = glm::vec3(1.0f, 2.0f, 3.0f);
    child.GetComponent<TransformComponent>().Scale = glm::vec3(2.0f);
    child.AddComponent<LightComponent>().Intensity = 4.0f;
    other.AddComponent<ColliderComponent>().Layer = 3;
    other.AddComponent<JointComponent>().ConnectedEntity = "Raiz";
    other.AddComponent<CharacterControllerComponent>().Radius = 0.5f;

    const fs::path file = dir / "teste.prismmap";
    SceneSerializer saver(scene);
    CHECK(saver.Serialize(file, false));
    CHECK(fs::exists(file));
    const uint64_t fp = saver.ComputeFingerprint();
    CHECK(fp != 0);
    CHECK(saver.ComputeFingerprint() == fp); // estavel

    auto loaded = Scene::Create("vazia");
    SceneSerializer loader(loaded);
    CHECK(loader.Deserialize(file));
    Ref<Scene> result = loader.GetScene();
    CHECK(result != nullptr);
    if (result) {
        CHECK(result->GetName() == "Fase 1");
        SceneSerializer again(result);
        CHECK(again.ComputeFingerprint() == fp); // mesma cena => mesmos bytes

        Entity lroot, lchild, lother;
        auto view = result->GetRegistry().view<TagComponent>();
        for (auto h : view) {
            Entity e(h, result.get());
            const std::string& t = e.GetComponent<TagComponent>().Tag;
            if (t == "Raiz") lroot = e; else if (t == "Filho") lchild = e; else if (t == "Outro") lother = e;
        }
        CHECK(lroot && lchild && lother);
        if (lroot && lchild && lother) {
            CHECK(lchild.GetParent() == lroot);
            CHECK(lroot.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
            CHECK(lchild.GetComponent<LightComponent>().Intensity == 4.0f);
            CHECK(lother.GetComponent<ColliderComponent>().Layer == 3);
            CHECK(lother.GetComponent<JointComponent>().ConnectedEntity == "Raiz");
            CHECK(lother.GetComponent<CharacterControllerComponent>().Radius == 0.5f);
        }
    }

    // mudar algo muda a impressao digital
    other.GetComponent<ColliderComponent>().Layer = 4;
    CHECK(SceneSerializer(scene).ComputeFingerprint() != fp);

    // Clone da cena tem a mesma impressao digital
    auto clone = scene->Clone();
    CHECK(clone && SceneSerializer(clone).ComputeFingerprint() == SceneSerializer(scene).ComputeFingerprint());

    // arquivos invalidos sao recusados (sem crash)
    const std::string bytes = ReadFileBytes(file);
    {
        std::string bad = bytes; bad[0] = 'X';
        WriteFileBytes(dir / "magic.prismmap", bad);
        SceneSerializer s(Scene::Create("x"));
        CHECK(!s.Deserialize(dir / "magic.prismmap"));
    }
    {
        WriteFileBytes(dir / "vazio.prismmap", "");
        SceneSerializer s(Scene::Create("x"));
        CHECK(!s.Deserialize(dir / "vazio.prismmap"));
    }
    {
        SceneSerializer s(Scene::Create("x"));
        CHECK(!s.Deserialize(dir / "nao-existe.prismmap"));
    }
    for (size_t n : { size_t(3), size_t(8), bytes.size() / 2, bytes.size() - 1 }) {
        WriteFileBytes(dir / "cortado.prismmap", bytes.substr(0, n));
        SceneSerializer s(Scene::Create("x"));
        CHECK(!s.Deserialize(dir / "cortado.prismmap"));
    }
}

static void TestPrefabSyncer(const fs::path& dir) {
    auto scene = Scene::Create("Prefabs");
    Entity root = scene->CreateEntity("Torre");
    Entity part = scene->CreateEntity("Luz");
    scene->SetParent(part, root);
    part.AddComponent<LightComponent>().Intensity = 2.0f;
    root.AddComponent<ColliderComponent>().Layer = 1;

    const fs::path prefab = dir / "torre.prismprefab";
    CHECK(PrefabSerializer::Serialize(root, prefab));
    CHECK(fs::exists(prefab));

    Entity inst = PrefabSerializer::Instantiate(*scene, prefab, AssetID::Generate());
    CHECK(inst);
    if (!inst) return;
    CHECK(inst.HasComponent<PrefabInstanceRootComponent>());
    CHECK(inst.GetChildCount() == 1);

    PrefabDiffResult d0 = PrefabSyncer::Diff(inst, prefab);
    CHECK(d0.Valid);
    CHECK(d0.StructureMatches);
    CHECK(d0.OverrideCount() == 0);

    // override: muda a intensidade da luz da instancia
    Entity instLight = inst.GetChildAt(0);
    instLight.GetComponent<LightComponent>().Intensity = 9.0f;
    PrefabDiffResult d1 = PrefabSyncer::Diff(inst, prefab);
    CHECK(d1.Valid && d1.OverrideCount() == 1);
    bool lightOverridden = false;
    for (auto& c : d1.Components)
        if (c.Overridden && c.ComponentName == "Light") lightOverridden = true;
    CHECK(lightOverridden);

    // UpdateAll preserva override
    CHECK(PrefabSyncer::UpdateAll(inst, prefab) >= 0);
    CHECK(instLight.GetComponent<LightComponent>().Intensity == 9.0f);

    // Revert volta ao valor do prefab
    uint32_t index = instLight.GetComponent<PrefabInstanceMemberComponent>().IndexInPrefab;
    CHECK(PrefabSyncer::RevertComponent(instLight, prefab, index, "Light"));
    CHECK(instLight.GetComponent<LightComponent>().Intensity == 2.0f);
    CHECK(PrefabSyncer::Diff(inst, prefab).OverrideCount() == 0);

    // Aplicar ao prefab: o arquivo passa a ter o novo valor
    instLight.GetComponent<LightComponent>().Intensity = 7.0f;
    CHECK(PrefabSyncer::ApplyComponentToPrefab(instLight, prefab, "Light"));
    CHECK(PrefabSyncer::Diff(inst, prefab).OverrideCount() == 0);
    Entity inst2 = PrefabSerializer::Instantiate(*scene, prefab, AssetID::Generate());
    CHECK(inst2 && inst2.GetChildCount() == 1 && inst2.GetChildAt(0).GetComponent<LightComponent>().Intensity == 7.0f);

    // estrutura diferente: adicionar filho a mao
    Entity extra = scene->CreateEntity("Extra");
    scene->SetParent(extra, inst);
    PrefabDiffResult d2 = PrefabSyncer::Diff(inst, prefab);
    CHECK(d2.Valid && !d2.StructureMatches);

    // erros: entidade que nao e raiz de instancia, prefab inexistente
    CHECK(!PrefabSyncer::Diff(root, prefab).Valid);
    CHECK(!PrefabSyncer::Diff(inst, dir / "nao-existe.prismprefab").Valid);
}

// Cache de matriz de mundo (Scene::WorldTransformScope): o resultado COM scope
// tem que ser identico ao calculo sem scope, em hierarquias profundas, e o cache
// nao pode vazar de um scope para o seguinte (a cena muda entre quadros).
static bool NearlyEqual(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (std::fabs(a[c][r] - b[c][r]) > 1e-4f) return false;
    return true;
}

static void TestWorldTransformScope() {
    Scene scene("WorldCache");
    std::vector<Entity> chain;
    Entity previous;
    for (int i = 0; i < 12; i++) {
        Entity e = scene.CreateEntity("N" + std::to_string(i));
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = { 1.0f * i, 0.5f, -0.25f * i };
        t.Rotation = { 3.0f * i, 10.0f, 1.5f * i };
        t.Scale = { 1.0f + 0.05f * i, 1.0f, 1.0f + 0.02f * i };
        if (i > 0) scene.SetParent(e, previous);
        chain.push_back(e);
        previous = e;
    }
    // Um irmao solto (sem pai) no meio, para misturar raizes e filhos.
    Entity loose = scene.CreateEntity("Solto");
    loose.GetComponent<TransformComponent>().Translation = { 9, 9, 9 };

    std::vector<glm::mat4> reference;
    for (Entity& e : chain) reference.push_back(scene.GetWorldTransform(e));
    const glm::mat4 looseReference = scene.GetWorldTransform(loose);

    {
        Scene::WorldTransformScope scope(scene);
        for (size_t i = 0; i < chain.size(); i++)
            CHECK(NearlyEqual(scene.GetWorldTransform(chain[i]), reference[i]));
        // consultar de novo (agora vindo do cache) e fora de ordem continua igual
        for (size_t i = chain.size(); i-- > 0;)
            CHECK(NearlyEqual(scene.GetWorldTransform(chain[i]), reference[i]));
        CHECK(NearlyEqual(scene.GetWorldTransform(loose), looseReference));

        // scope aninhado nao invalida nem quebra o externo
        {
            Scene::WorldTransformScope inner(scene);
            CHECK(NearlyEqual(scene.GetWorldTransform(chain.back()), reference.back()));
        }
        CHECK(NearlyEqual(scene.GetWorldTransform(chain.back()), reference.back()));
    }

    // Entre dois scopes a cena muda: o segundo NAO pode devolver valores do primeiro.
    chain[3].GetComponent<TransformComponent>().Translation = { 100.0f, 0.0f, 0.0f };
    {
        Scene::WorldTransformScope scope(scene);
        CHECK(!NearlyEqual(scene.GetWorldTransform(chain.back()), reference.back()));
        CHECK(NearlyEqual(scene.GetWorldTransform(chain[1]), reference[1])); // acima da mudanca: igual
    }
    // Fora de qualquer scope, volta ao calculo direto e tambem enxerga a mudanca.
    CHECK(!NearlyEqual(scene.GetWorldTransform(chain.back()), reference.back()));

    // Entidades criadas depois de um scope (indices maiores que o cache) funcionam.
    for (int i = 0; i < 300; i++) scene.CreateEntity("Extra");
    Entity late = scene.CreateEntity("Tardia");
    late.GetComponent<TransformComponent>().Translation = { 1, 2, 3 };
    {
        Scene::WorldTransformScope scope(scene);
        CHECK(NearlyEqual(scene.GetWorldTransform(late), late.GetComponent<TransformComponent>().GetTransform()));
    }
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "prism_scene_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    TestRegistryBasics();
    TestEveryComponentRoundTrips();
    TestTruncatedBlocksNeverCrash();
    TestBlockStreamFraming();
    TestOldBlockVersionsStillLoad();
    TestMaterialSurface(dir);
    TestSceneFileRoundTrip(dir);
    TestPrefabSyncer(dir);
    TestWorldTransformScope();

    fs::remove_all(dir, ec);
    std::printf("%d checagens, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
