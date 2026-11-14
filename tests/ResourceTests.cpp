// ResourceTests.cpp - formato generico de recurso (Resources/Resource.h): texto, binario,
// robustez contra arquivos corrompidos/maliciosos e gravacao atomica. So std.
#include <Prism/Resources/Resource.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using namespace Prism;
namespace fs = std::filesystem;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static ResourceDocument MakeSample() {
    ResourceDocument doc;
    doc.Type = "ParticleEmitter";
    doc.Version = 3;
    ResourceValue& r = doc.Root;
    r.Set("Enabled", ResourceValue::MakeBool(true));
    r.Set("Count", ResourceValue::MakeInt(-42));
    r.Set("Big", ResourceValue::MakeInt(std::numeric_limits<int64_t>::max()));
    r.Set("Small", ResourceValue::MakeInt(std::numeric_limits<int64_t>::min()));
    r.Set("Drag", ResourceValue::MakeFloat(0.1));
    r.Set("Whole", ResourceValue::MakeFloat(2.0));          // decimal "redondo" nao pode voltar como inteiro
    r.Set("NegZero", ResourceValue::MakeFloat(-0.0));
    r.Set("Tiny", ResourceValue::MakeFloat(5e-324));        // denormal
    r.Set("Huge", ResourceValue::MakeFloat(1.7976931348623157e308));
    r.Set("Inf", ResourceValue::MakeFloat(std::numeric_limits<double>::infinity()));
    r.Set("NegInf", ResourceValue::MakeFloat(-std::numeric_limits<double>::infinity()));
    r.Set("Nan", ResourceValue::MakeFloat(std::numeric_limits<double>::quiet_NaN()));
    r.Set("Nothing", ResourceValue::MakeNull());
    r.Set("Name", ResourceValue::MakeString("Fogo \"quente\"\n\tcom\\barra e acentuacao: coracao \xC3\xA7\xC3\xA3o \xE2\x82\xAC"));
    r.Set("Ctrl", ResourceValue::MakeString(std::string("a\x01" "b\x7f" "c", 5)));
    r.Set("Empty", ResourceValue::MakeString(""));
    r.Set("weird key!", ResourceValue::MakeInt(1));          // chave que nao e identificador
    r.Set("Texture", ResourceValue::MakeAssetRef(AssetID(0x8f3a1c5d2b7e9041ull), "Textures/smoke.png"));
    r.Set("TextureNoHint", ResourceValue::MakeAssetRef(AssetID(0xabull)));

    float box[3] = { 1.0f, 2.5f, 3.0f };
    r.Set("ShapeBox", ResourceValue::MakeFloatArray(box, 3));

    ResourceValue curve = ResourceValue::MakeArray();
    for (int i = 0; i < 5; i++) {
        float key[2] = { i * 0.25f, 1.0f + i };
        curve.Add(ResourceValue::MakeFloatArray(key, 2));
    }
    r.Set("SizeOverLife", std::move(curve));

    ResourceValue gradient = ResourceValue::MakeObject("Gradient");
    ResourceValue stops = ResourceValue::MakeArray();
    for (int i = 0; i < 8; i++) {                           // longa o bastante para quebrar em varias linhas
        float stop[5] = { i / 7.0f, 1.0f, 0.5f, 0.25f, 1.0f };
        stops.Add(ResourceValue::MakeFloatArray(stop, 5));
    }
    gradient.Set("Stops", std::move(stops));
    ResourceValue nested = ResourceValue::MakeMap();
    nested.Set("Empty", ResourceValue::MakeMap());
    nested.Set("EmptyList", ResourceValue::MakeArray());
    nested.Set("EmptyObject", ResourceValue::MakeObject("Thing"));
    nested.Set("Mixed", ResourceValue::MakeArray());
    nested.Find("Mixed")->Add(ResourceValue::MakeInt(1));
    nested.Find("Mixed")->Add(ResourceValue::MakeString("dois"));
    nested.Find("Mixed")->Add(ResourceValue::MakeMap());
    nested.Find("Mixed")->Items(); // (leitura)
    gradient.Set("Extra", std::move(nested));
    r.Set("ColorOverLife", std::move(gradient));
    return doc;
}

static void TestValueApi() {
    ResourceValue m = ResourceValue::MakeMap();
    m.Set("a", ResourceValue::MakeInt(1));
    m.Set("b", ResourceValue::MakeInt(2));
    m.Set("a", ResourceValue::MakeInt(9));                  // substitui mantendo a posicao
    CHECK(m.Fields().size() == 2 && m.Fields()[0].first == "a" && m.Fields()[0].second.AsInt() == 9);

    m.Set("f", ResourceValue::MakeFloat(3.0));
    m.Set("frac", ResourceValue::MakeFloat(3.5));
    m.Set("s", ResourceValue::MakeString("x"));
    float f = -1; int64_t i = -1; bool b = false; std::string s;
    CHECK(m.GetFloat("a", f) && f == 9.0f);                 // int serve para decimal
    CHECK(m.GetInt("f", i) && i == 3);                      // decimal inteiro serve para int
    CHECK(!m.GetInt("frac", i) && i == 3);                  // 3.5 nao vira inteiro; 'out' intacto
    CHECK(!m.GetFloat("s", f) && f == 9.0f);                // tipo errado: 'out' intacto
    CHECK(!m.GetFloat("missing", f));
    CHECK(!m.GetBool("a", b));
    CHECK(m.GetString("s", s) && s == "x");
    float arr[3] = { 0, 0, 0 };
    m.Set("v", ResourceValue::MakeArray());
    m.Find("v")->Add(ResourceValue::MakeInt(1));
    m.Find("v")->Add(ResourceValue::MakeFloat(2.5));
    CHECK(!m.GetFloatArray("v", arr, 3));                   // tamanho errado
    m.Find("v")->Add(ResourceValue::MakeInt(3));
    CHECK(m.GetFloatArray("v", arr, 3) && arr[1] == 2.5f && arr[2] == 3.0f);
    m.Find("v")->Add(ResourceValue::MakeString("x"));
    CHECK(!m.GetFloatArray("v", arr, 4));                   // elemento nao numerico

    ResourceValue scalar = ResourceValue::MakeInt(1);
    CHECK(scalar.Find("a") == nullptr);                     // Find em nao-mapa e seguro
    CHECK(ResourceValue::MakeNull().AsString().empty());
    CHECK(ResourceValue::MakeInt(5) == ResourceValue::MakeInt(5));
    CHECK(ResourceValue::MakeInt(5) != ResourceValue::MakeFloat(5.0)); // tipos diferentes
}

static void TestFloat32() {
    // Todo float finito volta igual por texto e por binario, e 0.1f e escrito como 0.1.
    const float samples[] = { 0.1f, 0.3f, 1.0f / 3.0f, 3.14159274f, 1e-30f, 3.4028235e38f, 1.17549435e-38f, 1e-45f, -0.0f, 123456.789f, 70.0f };
    for (float f : samples) {
        ResourceDocument doc;
        doc.Type = "T";
        doc.Root.Set("v", ResourceValue::MakeFloat32(f));
        ResourceDocument viaText;
        CHECK(ResourceIO::ParseText(ResourceIO::WriteText(doc), viaText));
        float back = 0;
        CHECK(viaText.Root.GetFloat("v", back) && std::memcmp(&back, &f, sizeof(f)) == 0);
    }
    ResourceDocument doc;
    doc.Type = "T";
    doc.Root.Set("v", ResourceValue::MakeFloat32(0.1f));
    CHECK(ResourceIO::WriteText(doc).find("v = 0.1\n") != std::string::npos);
    CHECK(ResourceValue::MakeFloat32(std::numeric_limits<float>::infinity()).AsDouble() == std::numeric_limits<double>::infinity());
}

static void TestTextRoundTrip() {
    const ResourceDocument original = MakeSample();
    const std::string text = ResourceIO::WriteText(original);
    ResourceDocument parsed;
    std::string error;
    const bool ok = ResourceIO::ParseText(text, parsed, &error);
    if (!ok) std::printf("erro de leitura: %s\n--- texto ---\n%s\n", error.c_str(), text.c_str());
    CHECK(ok);
    CHECK(parsed == original);
    CHECK(ResourceIO::WriteText(parsed) == text);           // escrever de novo da os mesmos bytes (estavel)

    // Detalhes de legibilidade: ordem preservada, decimal com '.', curta numa linha so.
    CHECK(text.rfind("prism_resource type=\"ParticleEmitter\" version=3\n", 0) == 0);
    CHECK(text.find("Whole = 2.0\n") != std::string::npos);
    CHECK(text.find("ShapeBox = [1.0, 2.5, 3.0]\n") != std::string::npos);
    CHECK(text.find("Texture = @asset(8f3a1c5d2b7e9041, \"Textures/smoke.png\")") != std::string::npos);
    CHECK(text.find("\"weird key!\" = 1") != std::string::npos);
    CHECK(text.find("Gradient {") != std::string::npos);
    CHECK(text.find("Count") < text.find("Big") && text.find("Big") < text.find("Drag"));
}

static void TestBinaryRoundTrip() {
    const ResourceDocument original = MakeSample();
    const std::string bytes = ResourceIO::WriteBinary(original);
    CHECK(ResourceIO::LooksBinary(bytes));
    ResourceDocument parsed;
    std::string error;
    CHECK(ResourceIO::ParseBinary(bytes, parsed, &error));
    CHECK(parsed == original);
    CHECK(ResourceIO::WriteBinary(parsed) == bytes);

    // texto -> binario -> texto: mesma arvore
    ResourceDocument viaText;
    CHECK(ResourceIO::ParseText(ResourceIO::WriteText(original), viaText));
    CHECK(ResourceIO::WriteBinary(viaText) == bytes);
    CHECK(bytes.size() < ResourceIO::WriteText(original).size()); // binario e mais compacto
}

static void TestHandWrittenText() {
    const std::string text =
        "\xEF\xBB\xBF"                                     // BOM
        "# um comentario no topo\r\n"
        "prism_resource   type=\"Thing\"  version=2  extra=\"ignorado\" # comentario no cabecalho\r\n"
        "\r\n"
        "A = 1  B = 2.5,  C = \"x\"          # varias por linha, virgula opcional\r\n"
        "D = [1 2 3,\r\n"
        "     4]\r\n"
        "E = Foo { x = 1 y = { z = true } }\r\n"
        "\"quoted key\" = null\r\n"
        "F = -7\r\n"
        "G = +3\r\n"
        "H = 1e3\r\n"
        "I = .5\r\n"
        "J = -inf\r\n"
        "K = @asset( 00000000000000AB )\r\n"
        "A = 99\r\n";                                      // repetida: a ultima vence
    ResourceDocument doc;
    std::string error;
    CHECK(ResourceIO::ParseText(text, doc, &error));
    if (!error.empty()) std::printf("%s\n", error.c_str());
    CHECK(doc.Type == "Thing" && doc.Version == 2);
    int64_t i = 0; float f = 0; std::string s; AssetID id;
    CHECK(doc.Root.GetInt("A", i) && i == 99);
    CHECK(doc.Root.Fields()[0].first == "A");               // manteve a posicao da primeira
    CHECK(doc.Root.GetFloat("B", f) && f == 2.5f);
    CHECK(doc.Root.GetString("C", s) && s == "x");
    CHECK(doc.Root.Find("D") && doc.Root.Find("D")->Items().size() == 4);
    const ResourceValue* e = doc.Root.Find("E");
    CHECK(e && e->GetKind() == ResourceValue::Kind::Object && e->TypeName() == "Foo");
    CHECK(e && e->Find("y") && e->Find("y")->Find("z") && e->Find("y")->Find("z")->AsBool());
    CHECK(doc.Root.Find("quoted key") && doc.Root.Find("quoted key")->IsNull());
    CHECK(doc.Root.GetInt("F", i) && i == -7);
    CHECK(doc.Root.GetInt("G", i) && i == 3);
    CHECK(doc.Root.Find("H")->GetKind() == ResourceValue::Kind::Float && doc.Root.GetFloat("H", f) && f == 1000.0f);
    CHECK(doc.Root.GetFloat("I", f) && f == 0.5f);
    CHECK(doc.Root.Find("J")->AsDouble() == -std::numeric_limits<double>::infinity());
    CHECK(doc.Root.GetAssetID("K", id) && id.Value() == 0xabull);
}

static bool Fails(const std::string& text, const char* expectedFragment = nullptr) {
    ResourceDocument doc;
    doc.Type = "intacto";
    std::string error;
    const bool ok = ResourceIO::ParseText(text, doc, &error);
    if (ok || doc.Type != "intacto" || error.empty()) // falhar deixa 'out' intacto e explica o erro
        return false;
    if (expectedFragment && error.find(expectedFragment) == std::string::npos) {
        std::printf("  erro inesperado: '%s' (esperava conter '%s')\n", error.c_str(), expectedFragment);
        return false;
    }
    return true;
}

static void TestTextErrors() {
    CHECK(Fails("", "prism_resource"));
    CHECK(Fails("hello = 1", "prism_resource"));
    CHECK(Fails("prism_resource version=1\n", "type"));
    CHECK(Fails("prism_resource type=\"\" version=1\n", "type"));
    CHECK(Fails("prism_resource type=\"X\" version=-1\n", "version"));
    CHECK(Fails("prism_resource type=\"X\" version=1.5\n", "version"));
    CHECK(Fails("prism_resource type=\"X\"\nA = \"sem fim", "aspas"));
    CHECK(Fails("prism_resource type=\"X\"\nA = \"escape \\q\"", "escape"));
    CHECK(Fails("prism_resource type=\"X\"\nA = \"\\u12\"", "\\u"));
    CHECK(Fails("prism_resource type=\"X\"\nA = \"\\ud83d\"", "surrogate"));
    CHECK(Fails("prism_resource type=\"X\"\nA = \"quebra\ndentro\"", "quebra"));
    CHECK(Fails("prism_resource type=\"X\"\nA = [1, 2", "']'"));
    CHECK(Fails("prism_resource type=\"X\"\nA = { b = 1", "'}'"));
    CHECK(Fails("prism_resource type=\"X\"\nA = 1 }", "'}'"));
    CHECK(Fails("prism_resource type=\"X\"\nA 1", "'='"));
    CHECK(Fails("prism_resource type=\"X\"\nA =", "acabou"));
    CHECK(Fails("prism_resource type=\"X\"\nA = bareword", "bareword"));
    CHECK(Fails("prism_resource type=\"X\"\nA = 1.2.3", "decimal"));
    CHECK(Fails("prism_resource type=\"X\"\nA = 1e", "decimal"));
    CHECK(Fails("prism_resource type=\"X\"\nA = -", "numero"));
    CHECK(Fails("prism_resource type=\"X\"\nA = 99999999999999999999", "grande"));
    CHECK(Fails("prism_resource type=\"X\"\nA = -9223372036854775809", "grande"));
    CHECK(Fails("prism_resource type=\"X\"\nA = @asset(0000000000000000)", "ID"));   // 0 reservado
    CHECK(Fails("prism_resource type=\"X\"\nA = @asset(abc)", "ID"));
    CHECK(Fails("prism_resource type=\"X\"\nA = @asset(00000000000000ab", "')'"));
    CHECK(Fails("prism_resource type=\"X\"\nA = @other(1)", "@asset"));
    CHECK(Fails("prism_resource type=\"X\"\nA = $", "caractere"));

    // Numero de linha no erro
    ResourceDocument doc;
    std::string error;
    CHECK(!ResourceIO::ParseText("prism_resource type=\"X\"\n\n\nA = bareword\n", doc, &error));
    CHECK(error.rfind("linha 4:", 0) == 0);

    // limites exatos de int64 sao aceitos
    ResourceDocument ok;
    CHECK(ResourceIO::ParseText("prism_resource type=\"X\"\nA = 9223372036854775807\nB = -9223372036854775808\n", ok));
    int64_t v = 0;
    CHECK(ok.Root.GetInt("A", v) && v == std::numeric_limits<int64_t>::max());
    CHECK(ok.Root.GetInt("B", v) && v == std::numeric_limits<int64_t>::min());
}

static void TestDepthBombs() {
    // 100000 colchetes abertos: tem que falhar com erro, sem estourar a pilha.
    std::string bomb = "prism_resource type=\"X\"\nA = ";
    bomb.append(100000, '[');
    CHECK(Fails(bomb, "profundo"));
    std::string braces = "prism_resource type=\"X\"\nA = ";
    for (int i = 0; i < 100000; i++) braces += "T{a=";
    CHECK(Fails(braces, "profundo"));

    // exatamente no limite e aceito
    std::string deep = "prism_resource type=\"X\"\nA = ";
    deep.append(ResourceIO::kMaxDepth, '[');
    deep.append(ResourceIO::kMaxDepth, ']');
    ResourceDocument ok;
    CHECK(ResourceIO::ParseText(deep, ok));
    std::string tooDeep = "prism_resource type=\"X\"\nA = ";
    tooDeep.append(ResourceIO::kMaxDepth + 2, '[');
    tooDeep.append(ResourceIO::kMaxDepth + 2, ']');
    CHECK(Fails(tooDeep, "profundo"));

    // binario: lista aninhada 100000 vezes
    std::string bin = ResourceIO::WriteBinary(ResourceDocument{ "X", 1, ResourceValue::MakeMap() });
    bin.pop_back(); // tira a contagem 0 do mapa raiz
    bin.pop_back(); // e a tag do mapa
    bin.push_back(7); bin.push_back(1); bin.push_back(1); // mapa raiz com 1 campo: chave de 1 byte...
    bin.push_back('a');
    for (int i = 0; i < 100000; i++) { bin.push_back(6); bin.push_back(1); }
    ResourceDocument doc;
    std::string error;
    CHECK(!ResourceIO::ParseBinary(bin, doc, &error));
    CHECK(!error.empty());
}

static void TestBinaryRobustness() {
    const std::string good = ResourceIO::WriteBinary(MakeSample());
    ResourceDocument doc;
    doc.Type = "intacto";

    // Todo prefixo truncado falha com erro (nunca crasha, nunca "le" pela metade).
    int accepted = 0;
    for (size_t n = 0; n < good.size(); n++) {
        std::string error;
        ResourceDocument out;
        out.Type = "intacto";
        const bool ok = ResourceIO::ParseBinary(std::string_view(good.data(), n), out, &error);
        if (ok) accepted++;
        if (!ok) { CHECK(out.Type == "intacto"); CHECK(!error.empty()); }
    }
    CHECK(accepted == 0);

    // Byte sobrando no fim
    std::string error;
    CHECK(!ResourceIO::ParseBinary(good + "x", doc, &error) && error.find("sobrando") != std::string::npos);
    // Assinatura errada / vazio / versao futura
    CHECK(!ResourceIO::ParseBinary("", doc, &error));
    CHECK(!ResourceIO::ParseBinary("NOPE12345678", doc, &error));
    std::string future = good;
    future[4] = 99; // versao do contêiner
    CHECK(!ResourceIO::ParseBinary(future, doc, &error) && error.find("versao") != std::string::npos);

    // Contagens mentirosas: lista de 2^60 itens num arquivo minusculo
    std::string liar = ResourceIO::WriteBinary(ResourceDocument{ "X", 1, ResourceValue::MakeMap() });
    liar.pop_back(); liar.pop_back();
    liar.push_back(7); liar.push_back(1); liar.push_back(1); liar.push_back('a');
    liar.push_back(6);
    for (int i = 0; i < 8; i++) liar.push_back((char)0xFF);
    liar.push_back(0x0F); // varint enorme
    CHECK(!ResourceIO::ParseBinary(liar, doc, &error));
    // Texto maior que o arquivo
    std::string longString = good.substr(0, 4);
    longString.append("\x01\0\0\0", 4);
    longString.push_back((char)0xFF); longString.push_back((char)0xFF); longString.push_back(0x7F); // tamanho ~2 MB, sem bytes
    CHECK(!ResourceIO::ParseBinary(longString, doc, &error));

    // Corrupcao de UM byte em cada posicao: pode ate ler (valor diferente), mas jamais crashar.
    for (size_t i = 0; i < good.size(); i++) {
        std::string bad = good;
        bad[i] = (char)(bad[i] ^ 0xA5);
        ResourceDocument out;
        std::string e;
        (void)ResourceIO::ParseBinary(bad, out, &e);
    }
    CHECK(true); // chegou ate aqui sem crash

    // Raiz que nao e mapa
    std::string rootList = good.substr(0, 4);
    rootList.append("\x01\0\0\0", 4);
    rootList.push_back(1); rootList.push_back('X');           // tipo "X"
    rootList.append("\x01\0\0\0", 4);                         // versao do tipo
    rootList.push_back(6); rootList.push_back(0);             // raiz = lista vazia
    CHECK(!ResourceIO::ParseBinary(rootList, doc, &error) && error.find("mapa") != std::string::npos);
}

static void TestFileIO() {
    const fs::path dir = fs::temp_directory_path() / "prism_resource_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);

    const ResourceDocument original = MakeSample();
    const fs::path textFile = dir / "sub" / "dir" / "fogo.prismtres";   // cria as pastas
    std::string error;
    CHECK(ResourceIO::Save(textFile, original, ResourceFormat::Text, &error));
    CHECK(fs::exists(textFile) && !fs::exists(fs::path(textFile).concat(".tmp")));   // sem lixo temporario

    const fs::path binaryFile = dir / "fogo_bin.prismres";              // mesma extensao, formato diferente
    CHECK(ResourceIO::Save(binaryFile, original, ResourceFormat::Binary, &error));

    ResourceDocument a, b;
    CHECK(ResourceIO::Load(textFile, a, &error));
    CHECK(ResourceIO::Load(binaryFile, b, &error));                     // detecta pelo conteudo
    CHECK(a == original && b == original);

    // Sobrescrever troca o conteudo por inteiro
    ResourceDocument small;
    small.Type = "Small";
    CHECK(ResourceIO::Save(textFile, small, ResourceFormat::Text, &error));
    ResourceDocument c;
    CHECK(ResourceIO::Load(textFile, c, &error) && c.Type == "Small" && c.Root.Fields().empty());

    // CRLF (arquivo editado no Windows) e lido
    {
        std::string text = ResourceIO::WriteText(original);
        std::string crlf;
        for (char ch : text) { if (ch == '\n') crlf += "\r\n"; else crlf.push_back(ch); }
        std::ofstream out(dir / "crlf.prismtres", std::ios::binary);
        out << crlf;
    }
    ResourceDocument d;
    CHECK(ResourceIO::Load(dir / "crlf.prismtres", d, &error));
    CHECK(d == original);

    // Erros de arquivo
    CHECK(!ResourceIO::Load(dir / "nao_existe.prismres", d, &error) && !error.empty());
    { std::ofstream out(dir / "lixo.prismres", std::ios::binary); out << "isto nao e um recurso"; }
    CHECK(!ResourceIO::Load(dir / "lixo.prismres", d, &error) && !error.empty());
    CHECK(d == original);                                               // falha nao altera o destino

    // Extensoes: .prismtres = texto, .prismres = binario (Save por extensao); Load abre qualquer um pelo conteudo.
    ResourceFormat f = ResourceFormat::Text;
    CHECK(ResourceIO::FormatFromPath("a/x.PRISMRES", f) && f == ResourceFormat::Binary);
    CHECK(ResourceIO::FormatFromPath("x.prismtres", f) && f == ResourceFormat::Text);
    CHECK(!ResourceIO::FormatFromPath("x.txt", f) && ResourceIO::IsResourcePath("x.prismres") && !ResourceIO::IsResourcePath("x"));
    CHECK(std::string(ResourceIO::ExtensionFor(ResourceFormat::Binary)) == ".prismres" && std::string(ResourceIO::ExtensionFor(ResourceFormat::Text)) == ".prismtres");
    CHECK(ResourceIO::SaveByExtension(dir / "b.prismres", original, &error));
    CHECK(ResourceIO::SaveByExtension(dir / "t.prismtres", original, &error));
    auto slurp = [](const fs::path& p) { std::ifstream in(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), {}); };
    CHECK(ResourceIO::LooksBinary(slurp(dir / "b.prismres")) && !ResourceIO::LooksBinary(slurp(dir / "t.prismtres")));
    ResourceDocument viaB, viaT;
    CHECK(ResourceIO::Load(dir / "b.prismres", viaB, &error) && viaB == original);
    CHECK(ResourceIO::Load(dir / "t.prismtres", viaT, &error) && viaT == original);

    fs::remove_all(dir, ec);
}

int main() {
    TestValueApi();
    TestFloat32();
    TestTextRoundTrip();
    TestBinaryRoundTrip();
    TestHandWrittenText();
    TestTextErrors();
    TestDepthBombs();
    TestBinaryRobustness();
    TestFileIO();
    std::printf("%d verificacoes, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
