// Extensao de exemplo: component "Health" (vida) e um sistema que regenera vida durante o Play.
// Copie esta pasta para <Projeto>/Extensions/Health/ (ou <pasta do editor>/Extensions/Health/),
// compile (veja CMakeLists.txt) e reinicie o editor.
#include <Prism/Extensions/ExtensionAPI.h>
#include <Prism/Scene/Scene.h>
#include <Prism/Scene/Components.h>

#include <algorithm>
#include <cstring>

using namespace Prism;

struct Health {
    float Hp = 100.0f;
    float MaxHp = 100.0f;
    float RegenPerSecond = 0.0f;
};

class HealthSystem : public IEngineSystem {
public:
    const char* GetName() const override { return "Health.Regen"; }
    void OnUpdate(Scene& scene, float dt) override {
        for (auto handle : scene.GetRegistry().view<Health>()) {
            Health& h = scene.GetRegistry().get<Health>(handle);
            if (h.RegenPerSecond > 0.0f)
                h.Hp = std::min(h.MaxHp, h.Hp + h.RegenPerSecond * dt);
        }
    }
};

static bool Init(const ExtensionHost& host) {
    ExtensionComponentDesc d;
    d.Name = "Health";            // chave gravada nos mapas: nao renomeie depois de publicar
    d.Category = "Gameplay";
    d.Version = 1;                // campos novos: acrescente NO FIM e suba a versao
    d.Serialize = [](std::ostream& out, Entity e) {
        const Health& h = e.GetComponent<Health>();
        ComponentRegistry::WriteRaw(out, h.Hp);
        ComponentRegistry::WriteRaw(out, h.MaxHp);
        ComponentRegistry::WriteRaw(out, h.RegenPerSecond);
    };
    d.Deserialize = [](std::istream& in, Entity e, uint32_t /*version*/) {
        Health h;
        if (!ComponentRegistry::ReadRaw(in, h.Hp) || !ComponentRegistry::ReadRaw(in, h.MaxHp) ||
            !ComponentRegistry::ReadRaw(in, h.RegenPerSecond))
            return false;
        if (e.HasComponent<Health>()) e.RemoveComponent<Health>();
        e.AddComponent<Health>(h);
        return true;
    };
    ExtensionUi ui = host.Ui; // por valor: o host so vale durante o Init
    d.DrawInspector = [ui](Entity e) {
        Health& h = e.GetComponent<Health>();
        ui.DragFloat("Vida", &h.Hp, 1.0f, 0.0f, h.MaxHp);
        ui.DragFloat("Vida maxima", &h.MaxHp, 1.0f, 1.0f, 100000.0f);
        ui.DragFloat("Regeneracao/s", &h.RegenPerSecond, 0.1f, 0.0f, 1000.0f);
    };
    if (!RegisterComponent<Health>(host, d))
        return false;
    RegisterSystem<HealthSystem>(host);
    LogMessage(host, 0, "Health carregada.");
    return true;
}

PRISM_EXTENSION(Init)
