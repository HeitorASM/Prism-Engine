#pragma once

// ============================================================================
// EngineSystem.h
// Um SISTEMA registravel: logica que roda a cada quadro de jogo sobre os
// components de uma Scene (inventario, missoes, dialogos, IA...), sem editar o
// nucleo da engine. Registra-se uma vez (ver SystemRegistry.h) e a Scene chama
// os callbacks na ordem de 'GetOrder()' durante o Play.
//
// Todos os callbacks sao opcionais. So rodam enquanto a Scene esta em Play (no
// editor parado nada executa, igual aos scripts). Uma excecao dentro de um
// callback desliga SO aquele sistema (erro no log com o nome dele); o resto da
// engine segue.
// ============================================================================

namespace Prism {

    class Scene;

    class IEngineSystem {
    public:
        virtual ~IEngineSystem() = default;

        // Nome unico (ex: "Inventario"). E a identidade do sistema no registro e nos logs.
        virtual const char* GetName() const = 0;

        // Menor roda primeiro. Empate: quem foi registrado antes. O que a engine ja faz
        // (fisica, scripts, particulas, animacao) roda ANTES de todos os sistemas
        // registrados, entao eles enxergam o estado final do quadro.
        virtual int GetOrder() const { return 0; }

        // O Play comecou / terminou nesta Scene (depois de os scripts carregarem / antes de descarregarem).
        virtual void OnSceneStart(Scene& scene) { (void)scene; }
        virtual void OnSceneStop(Scene& scene) { (void)scene; }

        // Uma vez por quadro.
        virtual void OnUpdate(Scene& scene, float deltaTime) { (void)scene; (void)deltaTime; }

        // Passo FIXO (1/60 s, o mesmo da fisica): 0 ou mais vezes por quadro, depois dos scripts.
        virtual void OnFixedUpdate(Scene& scene, float fixedDeltaTime) { (void)scene; (void)fixedDeltaTime; }

        // O sistema esta sendo removido do registro (Unregister/Shutdown). Libere recursos aqui.
        virtual void OnShutdown() {}
    };

}
