#pragma once

// ============================================================================
// ComponentRegistry.h
// Registro CENTRAL de todo tipo de Component "opcional" da engine (todo
// component exceto TransformComponent/TagComponent, que toda entidade tem
// por definicao - ver Scene::CreateEntity - e por isso nunca precisam de
// flag de presenca nem de menu "Add Component").
//
// POR QUE ISTO EXISTE: antes deste arquivo, adicionar um Component novo
// exigia editar MANUALMENTE e em SINCRONIA 3 lugares diferentes, sem
// nenhuma rede de seguranca do compilador:
//   1. SceneSerializer.cpp - um bloco de escrita E um de leitura, cada um
//      code duplicado do outro (mesma ordem de campos, ambos precisam
//      concordar byte a byte).
//   2. EditorLayer.cpp - o menu "Add Component" (HasComponent + MenuItem
//      + AddComponentCommand) E a lista 'hasAnyMissing' usada para
//      decidir se o menu deve aparecer.
// Esquecer de atualizar UM desses lugares ao adicionar um Component novo
// nao gera erro de compilacao - so um bug silencioso (ex: campo que existe
// no component mas nunca e salvo, perdendo o valor toda vez que o mapa e
// recarregado). Isto e o motivo original desta etapa de arquitetura (ver
// discussao antes deste arquivo existir).
//
// O QUE ISTO NAO E: nao e reflection automatica (C++ nao tem isso nativo -
// nem C++20). Cada Component ainda precisa de um bloco de registro escrito
// a mao (ver ComponentRegistration.cpp) - o ganho e ter esse bloco em UM
// lugar central, nao 2-3 lugares que podem divergir silenciosamente. A UI
// do Properties panel (ImGui::ColorEdit3, sliders, etc, com seu undo/redo
// especifico por campo) e DELIBERADAMENTE deixada de fora deste registro
// por agora - ver discussao de arquitetura: generalizar isso tocaria no
// sistema de Command/undo-redo, que e uma etapa separada e mais arriscada.
//
// COMO REGISTRAR UM COMPONENT NOVO: ver ComponentRegistration.cpp - chamar
// ComponentRegistry::Register<MeuComponent>(...) uma vez, dentro de
// ComponentRegistry::RegisterAll(). Nenhum outro arquivo desta lista
// precisa saber que o Component existe.
// ============================================================================

#include "Entity.h"
#include "Scene.h"
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <cstdint>

namespace Prism {

    // Uma entrada por tipo de Component registrado - ver comentario grande
    // acima para o que cada campo substitui.
    struct ComponentTypeInfo {
        // Nome mostrado no menu "Add Component" e usado nas mensagens de
        // Undo/Redo (ver AddComponentCommand/RemoveComponentCommand,
        // EditorCommands.h - continuam sendo instanciados pelo EditorLayer,
        // este registro so decide QUANDO oferecer isso e QUAL nome usar).
        std::string DisplayName;

        std::function<bool(Entity)> Has;

        // Adiciona o Component com valores DEFAULT (T{} implicito, via
        // AddComponent<T>() sem argumentos) - usado por
        // EditorLayer::RenderAddComponentMenu. NAO substitui
        // AddComponentCommand<T> (que continua dando Undo) - ver
        // ComponentRegistration.cpp para como os dois se encaixam.
        std::function<void(Entity)> AddDefault;

        std::function<void(Entity)> Remove;

        // Serializa os campos do Component (SEM a flag de presenca de 1
        // byte - isso e escrito por SceneSerializer::Serialize antes de
        // chamar isto, igual o padrao que ja existia antes deste
        // registro existir) - so chamado quando Has(entity) == true.
        std::function<void(std::ofstream&, Entity)> Serialize;

        // Le os campos do Component (o AddComponent<T>() e feito DENTRO
        // desta funcao, nao antes - assim um component com validacao que
        // falha, ver RigidBodyComponent/LightComponent, pode decidir NAO
        // adicionar nada e so retornar false). Retorna false em caso de
        // dados corrompidos/invalidos - SceneSerializer::Deserialize trata
        // isso exatamente como os blocos manuais de antes (aborta a leitura
        // inteira com uma mensagem de erro), ver comentario la.
        // 'entityIndexForLogging' e so para a mensagem de erro (ver
        // padrao "arquivo de cena corrompido (X da entidade N)" que ja
        // existia antes deste registro).
        std::function<bool(std::ifstream&, Entity, uint32_t)> Deserialize;

        // Copia os campos do Component de 'source' para 'destination' (que
        // ainda NAO deve ter este Component - AddComponent<T> dentro
        // desta funcao falharia via PRISM_ASSERT se ja tivesse). Gerado
        // automaticamente por Register<T> (ver ComponentRegistry.h) a
        // partir do operator= copia implicito de T - nao precisa ser
        // escrito a mao por Component em ComponentRegistration.cpp, ao
        // contrario de Serialize/Deserialize (que dependem do FORMATO DE
        // ARQUIVO, nao so da copia de memoria). Usado por Scene::Clone()
        // (ver Scene.h/.cpp) para clonar uma cena inteira SEM ir a disco -
        // ver comentario grande la sobre o que isto substitui.
        std::function<void(Entity source, Entity destination)> Copy;

        // Hook OPCIONAL (nullptr = nao faz nada extra), chamado pelo
        // EditorLayer depois de adicionar o Component via
        // AddComponentCommand - cobre ajustes de consistencia que nao sao
        // "o valor default do component", mas dependem do RESTO da cena
        // (ex: CameraComponent nasce com Primary=true, mas se outra
        // camera ja e Primary, o EditorLayer corrige isso aqui - ver
        // ComponentRegistration.cpp). NAO faz parte do historico de
        // Undo/Redo, igual o comportamento que ja existia antes deste
        // registro (ver comentario original em EditorLayer, "e so um
        // ajuste de consistencia, nao uma edicao que o usuario pediu").
        std::function<void(Entity, Scene&)> OnAfterAddInEditor = nullptr;
    };

    class ComponentRegistry {
    public:
        // Preenche a tabela com TODO Component opcional conhecido pela
        // engine - ver ComponentRegistration.cpp. Idempotente (chamar
        // duas vezes nao duplica entradas) mas so precisa ser chamado UMA
        // vez, no bootstrap do editor (ver EditorLayer::OnAttach ou
        // equivalente) - antes disso, GetAll() retorna vazio.
        static void RegisterAll();

        static const std::vector<ComponentTypeInfo>& GetAll() { return s_Registry; }

        // --- helpers de escrita/leitura binaria, expostos aqui para os
        // blocos de registro em ComponentRegistration.cpp poderem usar
        // exatamente as mesmas rotinas que SceneSerializer.cpp sempre
        // usou (WriteRaw/ReadRaw/WriteString/ReadString) - migradas para
        // ca (de static locais do .cpp) para serem compartilhaveis. O
        // FORMATO BINARIO GERADO E IDENTICO ao de antes deste registro
        // existir - nenhuma migracao de versao necessaria por esta
        // mudanca isoladamente.
        static void WriteString(std::ofstream& out, const std::string& str);
        static bool ReadString(std::ifstream& in, std::string& outStr);

        template<typename T>
        static void WriteRaw(std::ofstream& out, const T& value) {
            static_assert(std::is_trivially_copyable_v<T>, "WriteRaw so deve ser usado com tipos POD (floats, ints, glm::vec3, etc).");
            out.write(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        template<typename T>
        static bool ReadRaw(std::ifstream& in, T& value) {
            static_assert(std::is_trivially_copyable_v<T>, "ReadRaw so deve ser usado com tipos POD (floats, ints, glm::vec3, etc).");
            in.read(reinterpret_cast<char*>(&value), sizeof(T));
            return (bool)in;
        }

    private:
        // Chamado por RegisterAll() uma vez por tipo de Component - ver
        // ComponentRegistration.cpp para todos os usos.
        template<typename T>
        static void Register(
            std::string displayName,
            std::function<void(std::ofstream&, Entity)> serialize,
            std::function<bool(std::ifstream&, Entity, uint32_t)> deserialize,
            std::function<void(Entity, Scene&)> onAfterAddInEditor = nullptr)
        {
            ComponentTypeInfo info;
            info.DisplayName = std::move(displayName);
            info.Has = [](Entity e) { return e.HasComponent<T>(); };
            info.AddDefault = [](Entity e) { e.AddComponent<T>(); };
            info.Remove = [](Entity e) { e.RemoveComponent<T>(); };
            info.Serialize = std::move(serialize);
            info.Deserialize = std::move(deserialize);
            info.Copy = [](Entity source, Entity destination) {
                destination.AddComponent<T>(source.GetComponent<T>());
            };
            info.OnAfterAddInEditor = std::move(onAfterAddInEditor);
            s_Registry.push_back(std::move(info));
        }

        static std::vector<ComponentTypeInfo> s_Registry;
    };

}
