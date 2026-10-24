# Roadmap de melhorias

Plano de trabalho por fases. Cada fase termina com a engine funcionando e a documentação atualizada.

## Fase 1 — Modelos e texturas (feito)

- [x] Texturas de modelos importados invertidas: removido `aiProcess_FlipUVs` (a engine já carrega as imagens com flip vertical).
- [x] Previews de textura invertidos no editor (painel Material e Content Browser).
- [x] Importação de materiais: um submesh por material, texturas externas e embutidas, fatores PBR.
- [x] Arrastar modelo para Hierarquia/Viewport instancia com materiais (`MeshRenderer::SubmeshIndex`, mapa v14, prefab v2).

## Fase 2 — Áudio (feito)

- [x] `AudioEngine` em `Prism/Audio/` (miniaudio via FetchContent, só o header).
- [x] Components `AudioSource` (clip, volume, pitch, loop, 3D, play on start) e `AudioListener` (mapa v15, prefab v3).
- [x] Assets `.wav/.mp3/.ogg/.flac` arrastáveis para o campo Clip; botão **Ouvir** no editor.
- [x] API Lua: `entity:PlaySound()`, `Audio.Play/Play3D/SetMasterVolume`. Veja [audio.md](audio.md).
- [x] `.ogg` (stb_vorbis), canais de volume (buses) e cache de recursos.
- [ ] Pendente: efeitos (reverb, filtros).

## Fase 3 — Scripting Lua melhor (feito, exceto itens abaixo)

- Ciclo de vida: `OnCreate`, `OnUpdate(dt)`, `OnFixedUpdate`, `OnDestroy`, `OnCollisionEnter/Exit`.
- API de entidade: buscar por nome/tag, criar/destruir, acessar e modificar qualquer component, hierarquia.
- `Time`, `Input` completo (ações, mouse, gamepad), `Scene.Load`, `Physics` (forças, raycast, overlap), `Log`.
- Variáveis públicas do script editáveis no painel Propriedades.
- Mensagens de erro de Lua com arquivo/linha no Console; hot reload de scripts (Lua, preservando o estado; ver [scripting-lua.md](scripting-lua.md)).
- Entregue: callbacks de colisão/trigger, `Time`, `Scene` (Find/Create/Instantiate/Destroy), getters de components, operadores de `Vec3`, `Mathf`, variáveis públicas (`Properties`).
- [x] `OnFixedUpdate` (Lua e C++) e hot reload de Lua.
- [x] `Scene.Load` / `Scene.Reload` (Lua e C++).
- Pendente: gamepad.

## Fase 4 — Scripts em C++ (feito)

- Módulo nativo do projeto (DLL/.so) com CMake gerado pelo editor, carregado em runtime.
- Interface `ScriptableEntity` (`OnCreate/OnUpdate/OnFixedUpdate/OnDestroy`) e registro por macro `PRISM_REGISTER_SCRIPT`.
- Entregue: `ScriptableEntity`, `PRISM_REGISTER_SCRIPT`, módulo DLL/.so com CMake gerado, menu "Scripts C++" (gerar/compilar/recarregar), component "Native Script", API da engine para o módulo. Ver [scripting-cpp.md](scripting-cpp.md).
- [x] API v4: paridade com o Lua (transform de mundo, `ApplyForce`, hierarquia, mouse/cursor, UI, áudio com canais, `OnFixedUpdate`).
- [x] API v6: variáveis públicas (`OnProperties`), hot reload do módulo durante o Play, `LoadScene`/`ReloadScene`.

## Fase 5 — Básicos que ainda faltam

- [x] Sistema de partículas (5A): `ParticleEmitterComponent`, simulação CPU, billboards com blend, preview no editor, API Lua/C++.


- [x] Animação esquelética (skinning), animação de nós rígidos e importação de clipes: `AnimatorComponent`, crossfade, `entity:PlayAnimation` em Lua/C++ (API nativa v5), sombras e SSAO seguem a pose (ver [animacao.md](animacao.md)). Pendente: morph targets, máquina de estados, retargeting/IK, root motion.
- [x] Skybox (5B): cor, gradiente ou panorama equirretangular (.hdr/.png/.jpg) por cena (`EnvironmentComponent`).
- [x] UI in-game (5C): `UIElementComponent` (Panel/Texto/Imagem/Botão), âncoras, hierarquia, `OnClick` em Lua/C++, fonte embutida (ver [docs/ui.md](ui.md)).

- Pendente: IBL a partir do panorama (irradiância/reflexo), pós-processamento (bloom).
- Exportar o jogo em pasta distribuível (o `PrismRuntime` já roda um projeto sem o editor), gerenciador de assets mais completo (renomear/mover no editor).
