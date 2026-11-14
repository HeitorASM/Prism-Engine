// ParticleResourceTests.cpp - emissor de particulas <-> recurso generico (.prismres).
// Usa so Components.h (glm/EnTT, sem OpenGL) + o formato de recurso: nao precisa da engine inteira.
#include <Prism/Resources/ParticleResource.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace Prism;
namespace fs = std::filesystem;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Compara pela propria representacao em recurso (cobre todos os campos da definicao).
static bool SameDefinition(const ParticleEmitterComponent& a, const ParticleEmitterComponent& b) {
    return ParticleResource::ToResource(a) == ParticleResource::ToResource(b);
}

static ParticleEmitterComponent MakeCustom() {
    ParticleEmitterComponent c;
    c.Loop = false; c.Duration = 2.5f; c.MaxParticles = 1234; c.EmissionRate = 77.0f; c.BurstCount = 12;
    c.LifetimeMin = 0.25f; c.LifetimeMax = 3.0f; c.SpeedMin = 0.5f; c.SpeedMax = 9.0f;
    c.SpreadAngle = 70.0f; c.GravityScale = -0.5f;
    c.SizeStart = 0.8f; c.SizeEnd = 0.01f;
    c.ColorStart = { 0.1f, 0.2f, 0.3f, 0.4f }; c.ColorEnd = { 0.9f, 0.8f, 0.7f, 0.6f };
    c.BlendMode = ParticleBlendMode::SoftAdditive;
    c.TexturePath = "Textures/smoke \"sheet\".png";
    c.SizeOverLife.AddKey(0.5f, 2.0f);
    c.SpeedOverLife = ParticleCurve::Constant(0.5f);
    c.ColorOverLife.AddStop(0.5f);
    c.ColorOverLife.C[1][0] = 0.25f;
    c.Drag = 1.5f; c.SizeVariation = 0.3f;
    c.SimulationSpace = ParticleSimulationSpace::Local;
    c.Shape = ParticleShape::Circle;
    c.ShapeRadius = 2.0f; c.ShapeThickness = 0.5f; c.ShapeBox = { 1.0f, 2.0f, 3.0f }; c.ShapeRadialBlend = 0.75f;
    c.RotationMin = -45.0f; c.RotationMax = 45.0f; c.AngularVelocityMin = -90.0f; c.AngularVelocityMax = 90.0f;
    c.FlipbookColumns = 4; c.FlipbookRows = 2; c.FlipbookMode = ParticleFlipbookMode::FixedFPS; c.FlipbookFPS = 24.0f;
    c.Wind = { 1.0f, -2.0f, 0.5f }; c.TurbulenceStrength = 3.0f; c.TurbulenceFrequency = 0.2f; c.TurbulenceSpeed = 1.5f;
    c.CollideGround = true; c.GroundHeight = -1.5f; c.Bounce = 0.7f; c.CollisionFriction = 0.3f; c.CollisionLifeLoss = 0.25f;
    return c;
}

static void TestRoundTrip() {
    for (const ParticleEmitterComponent& original : { ParticleEmitterComponent{}, MakeCustom() }) {
        ResourceDocument doc = ParticleResource::ToResource(original);
        CHECK(doc.Type == "ParticleEmitter" && doc.Version == ParticleResource::kVersion);

        ParticleEmitterComponent back;
        CHECK(ParticleResource::FromResource(doc, back));
        CHECK(SameDefinition(original, back));

        // pelos dois formatos de arquivo
        ResourceDocument viaText, viaBinary;
        CHECK(ResourceIO::ParseText(ResourceIO::WriteText(doc), viaText));
        CHECK(ResourceIO::ParseBinary(ResourceIO::WriteBinary(doc), viaBinary));
        ParticleEmitterComponent fromText, fromBinary;
        CHECK(ParticleResource::FromResource(viaText, fromText) && SameDefinition(original, fromText));
        CHECK(ParticleResource::FromResource(viaBinary, fromBinary) && SameDefinition(original, fromBinary));
    }
}

static void TestInstanceFieldsAreNotInTheResource() {
    ParticleEmitterComponent a = MakeCustom();
    a.Enabled = false; a.PlayOnStart = false; a.LinkedAsset = AssetID(0x1234ull);
    ResourceDocument doc = ParticleResource::ToResource(a);
    CHECK(doc.Root.Find("Enabled") == nullptr && doc.Root.Find("PlayOnStart") == nullptr && doc.Root.Find("LinkedAsset") == nullptr);

    // Aplicar um recurso preserva o que e da instancia
    ParticleEmitterComponent target;
    target.Enabled = false; target.PlayOnStart = false; target.LinkedAsset = AssetID(0xBEEFull);
    CHECK(ParticleResource::FromResource(ParticleResource::ToResource(MakeCustom()), target));
    CHECK(!target.Enabled && !target.PlayOnStart && target.LinkedAsset == AssetID(0xBEEFull));
    CHECK(target.Shape == ParticleShape::Circle && target.EmissionRate == 77.0f);
}

static void TestTolerantReading() {
    // Recurso minimo escrito a mao: so o que importa; o resto vem do padrao.
    ResourceDocument doc;
    std::string error;
    CHECK(ResourceIO::ParseText(
        "prism_resource type=\"ParticleEmitter\" version=1\n"
        "Drag = 2\n"                                   // inteiro serve para decimal
        "Shape = \"Sphere\"\n"
        "SizeOverLife = [[0, 0.5], [1, 2]]\n"
        "ChaveDeUmaVersaoFutura = [1, 2, 3]\n", doc, &error));
    ParticleEmitterComponent c;
    c.EmissionRate = 999.0f;                           // valor "sujo" do component: o recurso manda (volta ao padrao)
    CHECK(ParticleResource::FromResource(doc, c));
    ParticleEmitterComponent defaults;
    CHECK(c.Drag == 2.0f && c.Shape == ParticleShape::Sphere);
    CHECK(c.EmissionRate == defaults.EmissionRate);
    CHECK(c.SizeOverLife.Count == 2 && std::fabs(c.SizeOverLife.Evaluate(0.0f) - 0.5f) < 1e-6f);
    CHECK(c.BlendMode == defaults.BlendMode && c.SimulationSpace == defaults.SimulationSpace);

    // Tipos errados, nomes desconhecidos e listas malformadas sao ignorados (ficam no padrao).
    ResourceDocument bad;
    CHECK(ResourceIO::ParseText(
        "prism_resource type=\"ParticleEmitter\" version=1\n"
        "Drag = \"muito\"\n"
        "Shape = \"Dodecaedro\"\n"
        "BlendMode = 3\n"
        "ShapeBox = [1, 2]\n"
        "ColorStart = [1, 0, 0, \"a\"]\n"
        "SizeOverLife = [[0, 1], [0.5]]\n"
        "ColorOverLife = Gradient { Stops = [] }\n"
        "MaxParticles = -5\n"
        "FlipbookColumns = 2.5\n", bad, &error));
    ParticleEmitterComponent d;
    CHECK(ParticleResource::FromResource(bad, d));
    CHECK(SameDefinition(d, defaults));
}

static void TestSanitizesHostileValues() {
    ResourceDocument doc;
    std::string error;
    CHECK(ResourceIO::ParseText(
        "prism_resource type=\"ParticleEmitter\" version=1\n"
        "MaxParticles = 99999999\n"
        "Drag = nan\n"
        "SizeVariation = inf\n"
        "ShapeRadius = -5\n"
        "FlipbookColumns = 100000\n"
        "FlipbookFPS = -1\n"
        "SizeOverLife = [[0.9, 1000], [0.1, -5]]\n"    // fora de ordem e de faixa
        "LifetimeMin = 1e30\n", doc, &error));
    ParticleEmitterComponent c;
    CHECK(ParticleResource::FromResource(doc, c));
    CHECK(c.MaxParticles == 10000);
    CHECK(c.Drag == 0.0f);
    CHECK(c.SizeVariation == 0.0f);                    // infinito nao e valido: volta ao padrao
    CHECK(c.ShapeRadius == 0.0f);
    CHECK(c.FlipbookColumns == 64);
    CHECK(c.FlipbookFPS == 0.0f);
    CHECK(c.SizeOverLife.T[0] <= c.SizeOverLife.T[1]);
    CHECK(c.SizeOverLife.V[0] >= 0.0f && c.SizeOverLife.V[1] <= 100.0f);
}

static void TestWrongTypeIsRejectedWithoutChanges() {
    ResourceDocument doc;
    doc.Type = "Material";
    ParticleEmitterComponent c = MakeCustom();
    const ParticleEmitterComponent before = c;
    std::string error;
    CHECK(!ParticleResource::FromResource(doc, c, &error));
    CHECK(error.find("Material") != std::string::npos);
    CHECK(SameDefinition(c, before));
}

static void TestSanitizeIsIdempotent() {
    ParticleEmitterComponent c = MakeCustom();
    c.Shape = (ParticleShape)99;
    c.BlendMode = (ParticleBlendMode)-4;
    c.Drag = std::numeric_limits<float>::quiet_NaN();
    c.Sanitize();
    CHECK(c.Shape == ParticleShape::Cone && c.BlendMode == ParticleBlendMode::Additive && c.Drag == 0.0f);
    const ResourceDocument once = ParticleResource::ToResource(c);
    c.Sanitize();
    CHECK(ParticleResource::ToResource(c) == once);
}

static void TestFiles() {
    const fs::path dir = fs::temp_directory_path() / "prism_particle_resource_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);

    const ParticleEmitterComponent original = MakeCustom();
    std::string error;
    CHECK(ParticleResource::Save(dir / "Effects" / "fogo.prismtres", original, ResourceFormat::Text, &error));
    CHECK(ParticleResource::Save(dir / "fogo_bin.prismres", original, ResourceFormat::Binary, &error));

    ParticleEmitterComponent a, b;
    CHECK(ParticleResource::Load(dir / "Effects" / "fogo.prismtres", a, &error) && SameDefinition(a, original));
    CHECK(ParticleResource::Load(dir / "fogo_bin.prismres", b, &error) && SameDefinition(b, original));

    // Outro tipo de recurso no arquivo: erro claro, componente intacto
    ResourceDocument other;
    other.Type = "Curve";
    CHECK(ResourceIO::Save(dir / "curve.prismtres", other, ResourceFormat::Text, &error));
    ParticleEmitterComponent untouched = original;
    CHECK(!ParticleResource::Load(dir / "curve.prismtres", untouched, &error) && SameDefinition(untouched, original));

    // O arquivo de texto e legivel de verdade
    std::ifstream in(dir / "Effects" / "fogo.prismtres");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("Shape = \"Circle\"") != std::string::npos);
    CHECK(text.find("BlendMode = \"SoftAdditive\"") != std::string::npos);
    CHECK(text.find("ColorOverLife = Gradient {") != std::string::npos);
    std::printf("%s\n", text.c_str());

    fs::remove_all(dir, ec);
}

int main() {
    TestRoundTrip();
    TestInstanceFieldsAreNotInTheResource();
    TestTolerantReading();
    TestSanitizesHostileValues();
    TestWrongTypeIsRejectedWithoutChanges();
    TestSanitizeIsIdempotent();
    TestFiles();
    std::printf("%d verificacoes, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
