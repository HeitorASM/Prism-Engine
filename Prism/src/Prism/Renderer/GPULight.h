#pragma once

// ============================================================================
// GPULight.h
// A luz "pronta para a GPU" (traducao de LightComponent) e o limite de luzes
// por quadro. Fica num header proprio porque Renderer e ShadowSystem precisam
// dele e um incluir o outro seria circular.
// ============================================================================

#include <glm/glm.hpp>
#include <cstdint>

namespace Prism {

    // Representacao de UMA luz PRONTA PARA A GPU - a "traducao" de um
    // LightComponent (dado de editor, ver Components.h) para o formato
    // generico que o shader consome via array de uniforms.
    //
    // Esta struct existe para que o shader NUNCA precise saber sobre
    // LightType especificamente: toda luz, seja qual for seu tipo, vira
    // um GPULight com os mesmos campos - o shader so olha 'Type' (como
    // int) para decidir COMO usar Position/Direction/Cone. Isso e o que
    // torna adicionar um tipo novo (ver comentario grande sobre Area/IES
    // em LightType, Components.h) uma mudanca pequena: 1) um novo case em
    // CollectGPULights preenchendo estes mesmos campos, 2) o shader
    // interpretando um novo valor de Type. Nenhuma outra funcao muda de
    // assinatura.
    //
    // Layout pensado para bater 1:1 com o std140 do array de uniforms no
    // shader (ver shaders/basic.frag em Renderer.cpp) - todo vec3 alinhado a
    // 16 bytes, sem "buracos" inesperados.
    struct GPULight {
        int   Type = 0;          // == (int)LightType - Point=0, Spot=1, Directional=2, ...
        glm::vec3 Position = { 0.0f, 0.0f, 0.0f };   // world space - ignorado por Directional
        glm::vec3 Direction = { 0.0f, -1.0f, 0.0f }; // world space, normalizada - so Spot/Directional (e Area no futuro)
        glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;
        float Range = 10.0f;               // Point/Spot - Directional ignora
        float CosOuterAngle = -1.0f;       // cos(SpotAngle) pre-calculado - so Spot
        float CosInnerAngle = -1.0f;       // cos(InnerSpotAngle) pre-calculado - so Spot

        // Identidade da entidade EnTT dona deste LightComponent (ver
        // Renderer::CollectGPULights) - existe SO para permitir achar de
        // volta, dentro de 'lights', qual elemento corresponde a qual
        // entidade (ver Renderer::DrawScene, que usa isto para casar o
        // resultado de RenderShadowPass - que sabe a entidade shadow
        // caster, nao so o Type - com o indice certo em u_Lights[]). Sem
        // isto, uma cena com DUAS luzes Directional (so uma delas com
        // CastShadows=true) aplicaria a sombra na luz errada se a
        // primeira Directional da lista nao fosse a shadow caster.
        uint32_t SourceEntityId = 0;

        // Esta luz projeta sombra? (LightComponent::CastShadows). Quem decide se
        // ela TEM um mapa de sombra neste quadro (limites por tipo) e o
        // ShadowSystem, que grava o resultado em ShadowSlot.
        bool CastShadows = false;

        // Posicao desta luz nos mapas de sombra do quadro (-1 = sem sombra):
        // Directional -> 0 (cascatas); Spot -> indice no array de spots;
        // Point -> indice no cube array. Preenchido por ShadowSystem::Render.
        int ShadowSlot = -1;
    };

    // Numero maximo de luzes simultaneas que uma cena pode enviar ao
    // shader em um unico frame - array de tamanho fixo no lado GLSL (ver
    // "uniform GPULight u_Lights[MAX_LIGHTS]" em shaders/basic.frag). O
    // Renderer::Init injeta este valor no GLSL como '#define MAX_LIGHTS', entao
    // C++ e shader nunca divergem: se um mapa precisar de mais luzes
    // simultaneas no mesmo draw call, e so aumentar este numero (nao ha custo
    // alem do maior array copiado por frame).
    static constexpr uint32_t MAX_LIGHTS = 16;

}
