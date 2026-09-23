// ============================================================================
// AssetTests.cpp
// Testes do modulo de assets (AssetID, AssetMeta, AssetRegistry). Sem
// framework externo: um CHECK simples que conta falhas. Compila SEM
// OpenGL/GLFW/EnTT - so o proprio modulo Assets/ + Log.h.
// ============================================================================
#include "Prism/Assets/AssetID.h"
#include "Prism/Assets/AssetMeta.h"
#include "Prism/Assets/AssetRegistry.h"
#include "Prism/Project/Project.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <unordered_set>

namespace fs = std::filesystem;
using namespace Prism;

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("  FALHOU  %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void Touch(const fs::path& p, const std::string& content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}
static std::string Slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}
static fs::path FreshDir(const char* name) {
    fs::path d = fs::temp_directory_path() / "prism_asset_tests" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// ---------------------------------------------------------------- AssetID
static void TestAssetID() {
    std::puts("[AssetID]");
    CHECK(!AssetID{}.IsValid());
    CHECK(!AssetID(0).IsValid());
    CHECK(AssetID(1).IsValid());

    // Generate: nunca 0 e (praticamente) sem colisao em 100k.
    std::unordered_set<AssetID> seen;
    bool anyZero = false;
    for (int i = 0; i < 100000; i++) {
        AssetID id = AssetID::Generate();
        if (!id.IsValid()) anyZero = true;
        seen.insert(id);
    }
    CHECK(!anyZero);
    CHECK(seen.size() == 100000);

    // Regra "nunca 0", PROVADA (nao dependente de sorte): forca a fonte a
    // devolver 0 tres vezes seguidas e depois 7. Generate() tem que descartar
    // os zeros e devolver 7. Sem o laco de rejeicao, devolveria 0 (invalido).
    {
        static int calls; calls = 0;
        AssetID::SetRandomSourceForTesting([]() -> uint64_t { return (++calls <= 3) ? 0ull : 7ull; });
        AssetID forced = AssetID::Generate();
        CHECK(forced.IsValid() && forced.Value() == 7);
        CHECK(calls == 4);                       // 3 zeros descartados + 1 valido
        AssetID::SetRandomSourceForTesting(nullptr);
        CHECK(AssetID::Generate().IsValid());    // gerador real restaurado
    }

    // ToString: sempre 16 chars, minusculo, com zeros a esquerda.
    CHECK(AssetID(0xab).ToString() == "00000000000000ab");
    CHECK(AssetID(0xFFFFFFFFFFFFFFFFull).ToString() == "ffffffffffffffff");
    CHECK(AssetID(0x0123456789abcdefull).ToString() == "0123456789abcdef");

    // Round-trip.
    for (int i = 0; i < 1000; i++) {
        AssetID a = AssetID::Generate(), b;
        CHECK(AssetID::TryParse(a.ToString(), b) && a == b);
    }

    // TryParse aceita maiuscula, rejeita o resto - e NAO toca em 'out'.
    AssetID out(42);
    CHECK(AssetID::TryParse("00000000000000AB", out) && out.Value() == 0xab);
    out = AssetID(42);
    CHECK(!AssetID::TryParse("", out));
    CHECK(!AssetID::TryParse("abc", out));
    CHECK(!AssetID::TryParse("00000000000000abc", out));   // 17 chars
    CHECK(!AssetID::TryParse("000000000000000g", out));    // 'g' nao e hex
    CHECK(!AssetID::TryParse("0000000000000000", out));    // zero reservado
    CHECK(!AssetID::TryParse(" 0000000000000ab", out));    // espaco
    CHECK(!AssetID::TryParse("0x0000000000000ab", out));   // prefixo 0x
    CHECK(out.Value() == 42);                              // intacto em TODAS as falhas
}

// -------------------------------------------------------------- AssetMeta
static void TestAssetType() {
    std::puts("[AssetType]");
    CHECK(AssetTypeFromExtension("a/b/Rock.png") == AssetType::Texture);
    CHECK(AssetTypeFromExtension("ROCK.PNG") == AssetType::Texture);
    CHECK(AssetTypeFromExtension("x.JPEG") == AssetType::Texture);
    CHECK(AssetTypeFromExtension("m.prismmat") == AssetType::Material);
    CHECK(AssetTypeFromExtension("p.prismprefab") == AssetType::Prefab);
    CHECK(AssetTypeFromExtension("w.prismmap") == AssetType::Scene);
    CHECK(AssetTypeFromExtension("s.lua") == AssetType::Script);
    CHECK(AssetTypeFromExtension("m.glb") == AssetType::Model);
    CHECK(AssetTypeFromExtension("a.ogg") == AssetType::Audio);
    CHECK(AssetTypeFromExtension("semextensao") == AssetType::Unknown);
    CHECK(AssetTypeFromExtension("x.xyz") == AssetType::Unknown);

    // String round-trip para todos, e desconhecido nunca falha.
    for (AssetType t : { AssetType::Texture, AssetType::Material, AssetType::Prefab,
                         AssetType::Scene, AssetType::Script, AssetType::Model, AssetType::Audio })
        CHECK(AssetTypeFromString(AssetTypeToString(t)) == t);
    CHECK(AssetTypeFromString("TipoDoFuturo") == AssetType::Unknown);
    CHECK(AssetTypeFromString("") == AssetType::Unknown);
}

static void TestAssetMetaFile() {
    std::puts("[AssetMetaFile]");
    fs::path dir = FreshDir("meta");

    CHECK(AssetMetaFile::MetaPathFor("A/Rock.png") == fs::path("A/Rock.png.meta"));
    // png e jpg com o mesmo nome base NAO disputam o mesmo .meta.
    CHECK(AssetMetaFile::MetaPathFor("Rock.png") != AssetMetaFile::MetaPathFor("Rock.jpg"));
    CHECK(AssetMetaFile::IsMetaFile("x.png.meta"));
    CHECK(!AssetMetaFile::IsMetaFile("x.png"));

    // Round-trip.
    AssetMeta w; w.ID = AssetID(0x8f3a1c5d2b7e9041ull); w.Type = AssetType::Texture;
    CHECK(AssetMetaFile::Write(dir / "sub/a.png.meta", w)); // cria a pasta 'sub'
    AssetMeta r;
    CHECK(AssetMetaFile::Read(dir / "sub/a.png.meta", r));
    CHECK(r.ID == w.ID && r.Type == AssetType::Texture && r.Version == AssetMeta::kCurrentVersion);

    // Nao sobra .tmp apos escrita bem sucedida.
    CHECK(!fs::exists(dir / "sub/a.png.meta.tmp"));

    // Conteudo legivel e estavel (o que vai pro Git).
    std::string txt = Slurp(dir / "sub/a.png.meta");
    CHECK(txt.find("ID=8f3a1c5d2b7e9041") != std::string::npos);
    CHECK(txt.find("Type=Texture") != std::string::npos);

    // Recusa gravar ID invalido; nao cria arquivo.
    AssetMeta bad;
    CHECK(!AssetMetaFile::Write(dir / "bad.meta", bad));
    CHECK(!fs::exists(dir / "bad.meta"));

    // Sobrescrever mantem o arquivo consistente.
    AssetMeta w2 = w; w2.ID = AssetID(7);
    CHECK(AssetMetaFile::Write(dir / "sub/a.png.meta", w2));
    CHECK(AssetMetaFile::Read(dir / "sub/a.png.meta", r) && r.ID == AssetID(7));

    // Leitura tolerante: CRLF, comentarios, espacos, chave desconhecida.
    Touch(dir / "crlf.meta",
        "# comentario\r\nVersion = 1\r\n  ID =  00000000000000ff  \r\nChaveFutura=abc\r\nType=Audio\r\n");
    AssetMeta c;
    CHECK(AssetMetaFile::Read(dir / "crlf.meta", c));
    CHECK(c.ID == AssetID(0xff) && c.Type == AssetType::Audio);

    // Falhas NAO tocam no 'out'.
    AssetMeta keep; keep.ID = AssetID(99);
    CHECK(!AssetMetaFile::Read(dir / "naoexiste.meta", keep) && keep.ID == AssetID(99));
    Touch(dir / "vazio.meta", "");
    CHECK(!AssetMetaFile::Read(dir / "vazio.meta", keep) && keep.ID == AssetID(99));
    Touch(dir / "semid.meta", "Version=1\nType=Texture\n");
    CHECK(!AssetMetaFile::Read(dir / "semid.meta", keep) && keep.ID == AssetID(99));
    Touch(dir / "idlixo.meta", "ID=isto-nao-e-hex!!\nType=Texture\n");
    CHECK(!AssetMetaFile::Read(dir / "idlixo.meta", keep) && keep.ID == AssetID(99));
    Touch(dir / "idzero.meta", "ID=0000000000000000\n");
    CHECK(!AssetMetaFile::Read(dir / "idzero.meta", keep) && keep.ID == AssetID(99));

    // Tipo desconhecido no .meta (versao futura) ainda le o ID.
    Touch(dir / "futuro.meta", "ID=0000000000000abc\nType=TipoDoFuturo\n");
    AssetMeta f;
    CHECK(AssetMetaFile::Read(dir / "futuro.meta", f) && f.ID == AssetID(0xabc) && f.Type == AssetType::Unknown);
}

// ----------------------------------------------------------- AssetRegistry
static void TestRegistryBasics() {
    std::puts("[AssetRegistry: basico]");
    fs::path root = FreshDir("reg_basic");
    Touch(root / "Textures/Rock.png");
    Touch(root / "Materials/Stone.prismmat");
    Touch(root / "Prefabs/Tree.prismprefab");
    Touch(root / ".gitkeep", "");          // oculto: ignorado
    Touch(root / "Textures/.DS_Store", "");

    AssetRegistry reg;
    reg.SetRoot(root);
    AssetRefreshReport r = reg.Refresh();

    CHECK(r.Registered == 3);
    CHECK(r.NewlyCreated == 3);
    CHECK(r.Moved == 0 && r.Repaired == 0 && r.OrphanMetas.empty());
    CHECK(reg.Count() == 3);

    // Cada asset ganhou seu .meta.
    CHECK(fs::exists(root / "Textures/Rock.png.meta"));
    CHECK(fs::exists(root / "Materials/Stone.prismmat.meta"));
    CHECK(!fs::exists(root / ".gitkeep.meta"));
    CHECK(!fs::exists(root / "Textures/.DS_Store.meta"));

    // Consultas.
    AssetID rockId = reg.IdForPath("Textures/Rock.png");
    CHECK(rockId.IsValid());
    const AssetRecord* rec = reg.Find(rockId);
    CHECK(rec && rec->Path == "Textures/Rock.png" && rec->Type == AssetType::Texture);
    CHECK(reg.FindByPath("Prefabs/Tree.prismprefab")->Type == AssetType::Prefab);
    CHECK(reg.AbsolutePath(rockId) == root / "Textures/Rock.png");
    CHECK(reg.Find(AssetID(12345)) == nullptr);
    CHECK(!reg.IdForPath("nao/existe.png").IsValid());
    CHECK(reg.AbsolutePath(AssetID(12345)).empty());

    // GetAll ordenado por caminho (deterministico).
    auto all = reg.GetAll();
    CHECK(all.size() == 3 && all[0]->Path == "Materials/Stone.prismmat" && all[2]->Path == "Textures/Rock.png");

    // .meta NAO e listado como asset.
    for (auto* a : all) CHECK(a->Path.find(".meta") == std::string::npos);

    // ToRelative: dentro/fora da raiz.
    CHECK(reg.ToRelative(root / "Textures/Rock.png") == "Textures/Rock.png");
    CHECK(reg.ToRelative(root.parent_path() / "outro.png").empty());
}

static void TestRegistryIdempotent() {
    std::puts("[AssetRegistry: idempotencia / persistencia]");
    fs::path root = FreshDir("reg_idem");
    Touch(root / "a.png"); Touch(root / "b.lua");

    AssetRegistry reg; reg.SetRoot(root); reg.Refresh();
    AssetID a = reg.IdForPath("a.png"), b = reg.IdForPath("b.lua");
    std::string metaBefore = Slurp(root / "a.png.meta");

    // Refresh de novo: nada muda, nada e criado, .meta intacto byte a byte.
    AssetRefreshReport r = reg.Refresh();
    CHECK(r.NewlyCreated == 0 && r.Moved == 0 && r.Repaired == 0 && r.Registered == 2);
    CHECK(reg.IdForPath("a.png") == a && reg.IdForPath("b.lua") == b);
    CHECK(Slurp(root / "a.png.meta") == metaBefore);

    // "Reabrir o projeto": registro NOVO le os mesmos IDs do disco.
    AssetRegistry reg2; reg2.SetRoot(root);
    AssetRefreshReport r2 = reg2.Refresh();
    CHECK(r2.NewlyCreated == 0);
    CHECK(reg2.IdForPath("a.png") == a && reg2.IdForPath("b.lua") == b);
}

static void TestRegistryMove() {
    std::puts("[AssetRegistry: mover/renomear - o ponto central]");
    fs::path root = FreshDir("reg_move");
    Touch(root / "Textures/Rock.png", "conteudo");

    AssetRegistry reg; reg.SetRoot(root); reg.Refresh();
    AssetID id = reg.IdForPath("Textures/Rock.png");

    // Move asset + .meta juntos para outra pasta E renomeia.
    fs::create_directories(root / "Environment");
    fs::rename(root / "Textures/Rock.png",      root / "Environment/BigRock.png");
    fs::rename(root / "Textures/Rock.png.meta", root / "Environment/BigRock.png.meta");

    AssetRefreshReport r = reg.Refresh();
    CHECK(r.Moved == 1);
    CHECK(r.NewlyCreated == 0 && r.Repaired == 0);
    CHECK(r.OrphanMetas.empty());
    CHECK(reg.IdForPath("Environment/BigRock.png") == id);          // MESMO ID
    CHECK(reg.Find(id)->Path == "Environment/BigRock.png");         // caminho novo
    CHECK(!reg.IdForPath("Textures/Rock.png").IsValid());           // caminho antigo sumiu
    CHECK(reg.AbsolutePath(id) == root / "Environment/BigRock.png");

    // Mover SO o asset (sem o .meta): identidade perdida, ID novo, e o .meta
    // antigo vira orfao - comportamento documentado no AssetMeta.h.
    fs::rename(root / "Environment/BigRock.png", root / "Solto.png");
    AssetRefreshReport r2 = reg.Refresh();
    CHECK(r2.NewlyCreated == 1);
    CHECK(reg.IdForPath("Solto.png") != id);
    CHECK(r2.OrphanMetas.size() == 1 && r2.OrphanMetas[0] == "Environment/BigRock.png.meta");
    CHECK(fs::exists(root / "Environment/BigRock.png.meta"));       // orfao NUNCA e apagado sozinho
}

static void TestRegistryDuplicateAndCorrupt() {
    std::puts("[AssetRegistry: duplicados / corrompidos]");
    fs::path root = FreshDir("reg_dup");
    Touch(root / "a.png");

    AssetRegistry reg; reg.SetRoot(root); reg.Refresh();
    AssetID original = reg.IdForPath("a.png");

    // Usuario copia asset + .meta -> dois .meta com o mesmo ID.
    fs::copy_file(root / "a.png", root / "b.png");
    fs::copy_file(root / "a.png.meta", root / "b.png.meta");

    AssetRefreshReport r = reg.Refresh();
    CHECK(r.Repaired == 1);
    CHECK(reg.Count() == 2);
    AssetID ia = reg.IdForPath("a.png"), ib = reg.IdForPath("b.png");
    CHECK(ia.IsValid() && ib.IsValid() && ia != ib);
    CHECK(ia == original);                       // o primeiro (ordem alfabetica) mantem o ID
    AssetMeta mb; CHECK(AssetMetaFile::Read(root / "b.png.meta", mb) && mb.ID == ib); // e o novo foi gravado

    // Determinismo da ordem: com N copias criadas em ordem INVERSA, quem
    // mantem o ID original tem que ser sempre o primeiro em ordem alfabetica,
    // nao o primeiro que o SO listar.
    {
        fs::path root2 = FreshDir("reg_order");
        Touch(root2 / "m.png");
        AssetRegistry r0; r0.SetRoot(root2); r0.Refresh();
        AssetID orig = r0.IdForPath("m.png");
        for (char ch = 'z'; ch >= 'a'; ch--) {       // z, y, x ... a  (inversa)
            if (ch == 'm') continue;
            std::string n(1, ch); n += ".png";
            fs::copy_file(root2 / "m.png", root2 / n);
            fs::copy_file(root2 / "m.png.meta", root2 / (n + ".meta"));
        }
        AssetRegistry r1; r1.SetRoot(root2); r1.Refresh();
        CHECK(r1.Count() == 26);
        CHECK(r1.IdForPath("a.png") == orig);        // 'a' vem primeiro alfabeticamente, entao ganha o ID
        CHECK(r1.IdForPath("m.png") != orig);        // 'm' e copia
        std::unordered_set<AssetID> ids;
        for (auto* rec : r1.GetAll()) ids.insert(rec->ID);
        CHECK(ids.size() == 26);                     // todos unicos
    }

    // Resultado ESTAVEL: um novo Refresh nao repara mais nada.
    AssetRefreshReport r2 = reg.Refresh();
    CHECK(r2.Repaired == 0 && r2.NewlyCreated == 0 && reg.IdForPath("b.png") == ib);

    // .meta corrompido -> ID novo, sem crash.
    Touch(root / "a.png.meta", "isto nao e um meta \x01\x02 ###");
    AssetRefreshReport r3 = reg.Refresh();
    CHECK(r3.Repaired == 1);
    CHECK(reg.IdForPath("a.png").IsValid());
    AssetMeta ma; CHECK(AssetMetaFile::Read(root / "a.png.meta", ma));

    // Sobra de escrita atomica interrompida (.tmp) e ignorada.
    Touch(root / "c.png.meta.tmp", "lixo");
    AssetRefreshReport r4 = reg.Refresh();
    CHECK(reg.FindByPath("c.png.meta.tmp") == nullptr);
    (void)r4;
}

static void TestRegistryTypeAndEdge() {
    std::puts("[AssetRegistry: tipo / bordas]");
    fs::path root = FreshDir("reg_edge");
    Touch(root / "x.png");

    AssetRegistry reg; reg.SetRoot(root); reg.Refresh();
    AssetID id = reg.IdForPath("x.png");

    // .meta com tipo errado e corrigido pela extensao, MANTENDO o ID.
    Touch(root / "x.png.meta", "Version=1\nID=" + id.ToString() + "\nType=Audio\n");
    reg.Refresh();
    CHECK(reg.IdForPath("x.png") == id);
    CHECK(reg.Find(id)->Type == AssetType::Texture);
    AssetMeta m; AssetMetaFile::Read(root / "x.png.meta", m);
    CHECK(m.Type == AssetType::Texture);

    // Pasta raiz inexistente / vazia: relatorio vazio, sem crash.
    AssetRegistry none;
    CHECK(none.Refresh().Registered == 0);              // sem SetRoot
    none.SetRoot(root / "naoexiste");
    CHECK(none.Refresh().Registered == 0);

    // Arquivo removido some do indice no Refresh seguinte.
    fs::remove(root / "x.png");
    fs::remove(root / "x.png.meta");
    reg.Refresh();
    CHECK(reg.Count() == 0 && reg.Find(id) == nullptr);

    // Caminhos com espaco e acento.
    Touch(root / "Minhas Texturas/Pedra áspera.png");
    reg.Refresh();
    CHECK(reg.IdForPath("Minhas Texturas/Pedra áspera.png").IsValid());
}


// ------------------------------------------------- integracao com Project
// Usa o Project REAL (New/Load/ProjectSerializer), nao um mock: garante que
// a integracao no ciclo de vida do projeto funciona de ponta a ponta.
static void TestProjectIntegration() {
    std::puts("[Project + AssetRegistry: ciclo de vida real]");
    fs::path dir = FreshDir("proj");

    Ref<Project> p = Project::New(dir / "MeuJogo", "MeuJogo");
    CHECK(p != nullptr);
    if (!p) return;

    // Projeto recem-criado: registro ja apontado para Assets/, e vazio.
    CHECK(p->GetAssetRegistry().GetRoot() == p->GetAssetDirectory());
    CHECK(p->GetAssetRegistry().Count() == 0);

    // O usuario solta assets nas pastas padrao e o editor pede um Refresh.
    Touch(p->GetAssetDirectory() / "Textures/Rock.png");
    Touch(p->GetMaterialDirectory() / "Stone.prismmat");
    Touch(p->GetPrefabDirectory() / "Tree.prismprefab");
    AssetRefreshReport r = p->GetAssetRegistry().Refresh();
    CHECK(r.Registered == 3 && r.NewlyCreated == 3);

    AssetID rock  = p->GetAssetRegistry().IdForPath("Textures/Rock.png");
    AssetID stone = p->GetAssetRegistry().IdForPath("Materials/Stone.prismmat");
    AssetID tree  = p->GetAssetRegistry().IdForPath("Prefabs/Tree.prismprefab");
    CHECK(rock.IsValid() && stone.IsValid() && tree.IsValid());
    CHECK(rock != stone && stone != tree && rock != tree);

    // FECHA e REABRE o projeto pelo .prismproj: os MESMOS IDs voltam.
    fs::path projFile = dir / "MeuJogo" / "MeuJogo.prismproj";
    Ref<Project> loaded = Project::Load(projFile);
    CHECK(loaded != nullptr);
    if (!loaded) return;

    CHECK(loaded.get() != p.get());                       // instancia nova de verdade
    CHECK(loaded->GetAssetRegistry().Count() == 3);       // ja varrido no Load
    CHECK(loaded->GetAssetRegistry().IdForPath("Textures/Rock.png") == rock);
    CHECK(loaded->GetAssetRegistry().IdForPath("Materials/Stone.prismmat") == stone);
    CHECK(loaded->GetAssetRegistry().IdForPath("Prefabs/Tree.prismprefab") == tree);

    // Reorganizar com o projeto FECHADO (o cenario real do usuario mexendo no
    // Explorer): move asset+meta, reabre - a referencia por ID continua valendo.
    loaded.reset(); p.reset();
    fs::create_directories(dir / "MeuJogo/Assets/Env");
    fs::rename(dir / "MeuJogo/Assets/Textures/Rock.png",      dir / "MeuJogo/Assets/Env/Pedra.png");
    fs::rename(dir / "MeuJogo/Assets/Textures/Rock.png.meta", dir / "MeuJogo/Assets/Env/Pedra.png.meta");

    Ref<Project> reopened = Project::Load(projFile);
    CHECK(reopened != nullptr);
    if (!reopened) return;
    CHECK(reopened->GetAssetRegistry().Find(rock) != nullptr);
    CHECK(reopened->GetAssetRegistry().Find(rock)->Path == "Env/Pedra.png");
    CHECK(reopened->GetAssetRegistry().AbsolutePath(rock) == reopened->GetAssetDirectory() / "Env/Pedra.png");

    // Trocar de projeto: o registro do outro projeto e INDEPENDENTE.
    Ref<Project> other = Project::New(dir / "Outro", "Outro");
    CHECK(other && other->GetAssetRegistry().Count() == 0);
    CHECK(other->GetAssetRegistry().Find(rock) == nullptr);
    CHECK(reopened->GetAssetRegistry().Find(rock) != nullptr);   // o primeiro nao foi afetado
}

int main() {
    TestAssetID();
    TestAssetType();
    TestAssetMetaFile();
    TestRegistryBasics();
    TestRegistryIdempotent();
    TestRegistryMove();
    TestRegistryDuplicateAndCorrupt();
    TestRegistryTypeAndEdge();
    TestProjectIntegration();

    std::printf("\n%d verificacoes, %d falha(s).\n", g_Checks, g_Failures);
    fs::remove_all(fs::temp_directory_path() / "prism_asset_tests");
    return g_Failures == 0 ? 0 : 1;
}
