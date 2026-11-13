// Extensao de exemplo (API v2): painel acoplavel, painel em janela propria, item de menu,
// hook de component (campos extras no inspetor da "Light" + OnUpdate) e hook de estagio (Particles).
// Copie para <Projeto>/Extensions/Painel/, compile (CMakeLists.txt) e reinicie o editor.
#include <Prism/Extensions/ExtensionAPI.h>
#include <Prism/Scene/Scene.h>
#include <Prism/Scene/Components.h>

#include <cstdio>

using namespace Prism;

static float s_Elapsed = 0.0f;
static int s_ParticleFrames = 0;

static bool Init(const ExtensionHost& host) {
    ExtensionUi ui = host.Ui;       // por valor: o host so vale durante o Init
    ExtensionUiEx ex = host.UiEx;

    // 1) Painel acoplavel (menu Janela > "Painel de exemplo").
    ExtensionPanelDesc panel;
    panel.Id = "exemplo";
    panel.Title = "Painel de exemplo";
    panel.Dock = ExtensionDockHint::Right;
    panel.OpenByDefault = true;
    panel.Draw = [ui, ex]() {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Tempo de Play: %.1fs", s_Elapsed);
        ui.Text(buf);
        std::snprintf(buf, sizeof(buf), "Quadros de particulas: %d", s_ParticleFrames);
        ex.TextDisabled(buf);
        if (ex.Button("Zerar")) { s_Elapsed = 0.0f; s_ParticleFrames = 0; }
    };
    RegisterPanel(host, panel);

    // 2) Janela propria do sistema (pode ir para outro monitor).
    ExtensionPanelDesc win;
    win.Id = "janela";
    win.Title = "Janela de exemplo";
    win.Mode = ExtensionPanelMode::OsWindow;
    win.Draw = [ex]() { ex.TextWrapped("Esta e uma janela separada do editor."); };
    RegisterPanel(host, win);

    // 3) Menu novo: "Exemplo/Zerar contador".
    ExtensionMenuItemDesc item;
    item.Path = "Exemplo/Zerar contador";
    item.Action = []() { s_Elapsed = 0.0f; s_ParticleFrames = 0; };
    RegisterMenuItem(host, item);

    // 4) Hook de component: modifica o comportamento do "Light" da engine.
    ExtensionComponentHookDesc light;
    light.Component = "Light";
    light.OnUpdate = [](Entity, float dt) { s_Elapsed += dt; };
    light.DrawInspector = [ex](Entity) {
        ex.Separator();
        ex.TextDisabled("Campos extras (extensao Painel)");
        return false; // nada foi alterado
    };
    RegisterComponentHook(host, light);

    // 5) Hook de estagio: conta quadros do estagio de particulas (Before/After nao substituem a engine).
    ExtensionStageHookDesc stage;
    stage.Stage = EngineStage::Particles;
    stage.After = [](Scene&, float) { s_ParticleFrames++; };
    RegisterStageHook(host, stage);

    LogMessage(host, 0, "Painel carregada.");
    return true;
}

PRISM_EXTENSION(Init)
