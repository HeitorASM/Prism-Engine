#pragma once

// ============================================================================
// AssetID.h
// Identidade ESTAVEL de um asset. Um caminho de arquivo NAO serve como
// identidade: renomear ou mover "Rock.png" quebraria toda referencia
// gravada em mapas/prefabs/materiais. Com um AssetID gravado no .meta ao
// lado do arquivo (ver AssetMeta.h), o asset continua o MESMO mesmo depois
// de mudar de pasta ou de nome - e o mesmo papel do "uid://" da Godot e do
// GUID da Unity.
//
// 64 bits aleatorios (nao 128 como um UUID completo): a chance de colisao
// entre N assets e ~N^2 / 2^65 - para 1 milhao de assets, na ordem de
// 3e-8. Em troca, um AssetID e um uint64_t trivialmente copiavel: cabe no
// ComponentRegistry::WriteRaw/ReadRaw sem formato novo, e serve direto
// como chave de unordered_map.
//
// O valor 0 e RESERVADO como "sem asset" (referencia vazia). Nunca e
// gerado por AssetID::Generate().
//
// Sem dependencia de OpenGL/EnTT/GLFW de proposito - este modulo precisa
// compilar e ser testavel sozinho.
// ============================================================================

#include <cstdint>
#include <string>
#include <functional>

namespace Prism {

    class AssetID {
    public:
        constexpr AssetID() = default;
        constexpr explicit AssetID(uint64_t value) : m_Value(value) {}

        // Gera um ID novo, aleatorio e diferente de 0.
        static AssetID Generate();

        // SO PARA TESTES: substitui a fonte de numeros por uma funcao
        // deterministica (nullptr = volta ao gerador real). Existe porque a
        // regra "Generate() nunca devolve 0" tem chance ~2^-64 de ser
        // violada por acaso - impossivel de exercitar so chamando Generate()
        // em laco. Com a fonte forcada a devolver 0, o teste PROVA que o
        // codigo rejeita o valor reservado em vez de depender de sorte.
        using RandomSource = uint64_t(*)();
        static void SetRandomSourceForTesting(RandomSource source);

        // 16 caracteres hex minusculos ("00000000000000ab"). Formato fixo,
        // sempre 16 chars: ordena lexicograficamente igual ao valor numerico
        // e nunca varia com o locale do sistema.
        std::string ToString() const;

        // Aceita SOMENTE exatamente 16 chars hex ([0-9a-fA-F]) e valor != 0.
        // Qualquer outra coisa (vazio, curto, com lixo, "0000000000000000")
        // devolve false e deixa 'out' intacto - um .meta editado a mao ou
        // corrompido nunca vira um ID "quase certo".
        static bool TryParse(const std::string& text, AssetID& out);

        constexpr bool IsValid() const { return m_Value != 0; }
        constexpr uint64_t Value() const { return m_Value; }

        constexpr bool operator==(const AssetID& o) const { return m_Value == o.m_Value; }
        constexpr bool operator!=(const AssetID& o) const { return m_Value != o.m_Value; }
        constexpr bool operator<(const AssetID& o) const { return m_Value < o.m_Value; }

    private:
        uint64_t m_Value = 0;
    };

}

template<>
struct std::hash<Prism::AssetID> {
    size_t operator()(const Prism::AssetID& id) const noexcept {
        return std::hash<uint64_t>{}(id.Value());
    }
};
