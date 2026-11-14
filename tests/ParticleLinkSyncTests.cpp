#include "Prism/Resources/ParticleLinkSync.h"
#include <cstdio>
#include <fstream>
#include <chrono>

using namespace Prism;
namespace fs = std::filesystem;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FALHA %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
    fs::path dir = fs::temp_directory_path() / "prism_linksync_test";
    fs::remove_all(dir); fs::create_directories(dir);
    const fs::path file = dir / "a.prismtres";
    const AssetID id{0x1234ABCD};
    ParticleLinkSync::PathResolver resolve = [&](AssetID a) { return a == id ? file : fs::path{}; };

    // arquivo inicial: Drag = 2
    ParticleEmitterComponent seed; seed.Drag = 2.0f;
    CHECK(ParticleResource::Save(file, seed));

    ParticleEmitterComponent a, b; a.LinkedAsset = id; b.LinkedAsset = id; a.Drag = 0.f; b.Drag = 0.f;
    ParticleEmitterComponent unlinked; unlinked.Drag = 7.f;
    std::vector<ParticleEmitterComponent*> all{&a, &b, &unlinked};
    ParticleLinkSync sync;
    double t = 0;

    // 1a varredura: arquivo manda
    CHECK(sync.Update(all, resolve, t, false));
    CHECK(a.Drag == 2.0f && b.Drag == 2.0f && unlinked.Drag == 7.f);
    CHECK(sync.GetStats().FileReloads == 1);

    // throttle: chamada imediata nao faz nada
    CHECK(!sync.Update(all, resolve, t + 0.01, false));

    // edicao em 'a': nao grava antes do atraso
    a.Drag = 3.5f;
    t += 0.2; sync.Update(all, resolve, t, false);
    CHECK(sync.HasPending() && sync.GetStats().Saves == 0);
    // holdSave adia mesmo passado o atraso
    t += 1.0; sync.Update(all, resolve, t, true);
    CHECK(sync.GetStats().Saves == 0 && b.Drag == 2.0f);
    // solta: grava e propaga para b
    t += 0.2; CHECK(sync.Update(all, resolve, t, false));
    CHECK(sync.GetStats().Saves == 1 && !sync.HasPending() && b.Drag == 3.5f);
    ParticleEmitterComponent disk; CHECK(ParticleResource::Load(file, disk)); CHECK(disk.Drag == 3.5f);

    // estavel: sem mudancas nao regrava
    for (int i = 0; i < 10; i++) { t += 0.2; sync.Update(all, resolve, t, false); }
    CHECK(sync.GetStats().Saves == 1);

    // Undo: voltar ao estado do arquivo antes do atraso cancela a gravacao
    a.Drag = 9.f; t += 0.2; sync.Update(all, resolve, t, false);
    a.Drag = 3.5f; t += 0.2; sync.Update(all, resolve, t, false);
    CHECK(!sync.HasPending());

    // arquivo alterado por fora -> arquivo manda
    ParticleEmitterComponent ext; ext.Drag = 11.f;
    CHECK(ParticleResource::Save(file, ext));
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
    t += 0.2; CHECK(sync.Update(all, resolve, t, false));
    CHECK(a.Drag == 11.f && b.Drag == 11.f && sync.GetStats().Saves == 1);

    // arquivo ilegivel: nao derruba nem mexe nos emissores
    { std::ofstream(file, std::ios::trunc) << "isto nao e um recurso {{{"; }
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
    t += 0.2; sync.Update(all, resolve, t, false);
    CHECK(a.Drag == 11.f && !sync.TakeLastError().empty());

    // asset sumido: ignora
    ParticleEmitterComponent orphan; orphan.LinkedAsset = AssetID{0x99}; orphan.Drag = 4.f;
    std::vector<ParticleEmitterComponent*> o{&orphan};
    t += 0.2; CHECK(!sync.Update(o, resolve, t, false) && orphan.Drag == 4.f);

    // FlushAll grava pendencia sem esperar
    fs::remove(file); ParticleResource::Save(file, seed);
    ParticleLinkSync s2; double u = 0;
    ParticleEmitterComponent c; c.LinkedAsset = id; std::vector<ParticleEmitterComponent*> cs{&c};
    s2.Update(cs, resolve, u, false); CHECK(c.Drag == 2.f);
    c.Drag = 6.f; u += 0.2; s2.Update(cs, resolve, u, false);
    CHECK(s2.HasPending()); s2.FlushAll(resolve); CHECK(!s2.HasPending());
    ParticleEmitterComponent d; ParticleResource::Load(file, d); CHECK(d.Drag == 6.f);

    fs::remove_all(dir);
    std::printf("%d checks, %d falhas\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
