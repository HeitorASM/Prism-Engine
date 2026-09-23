#pragma once

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
