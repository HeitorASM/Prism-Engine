#pragma once

// ============================================================================
// InstanceRing.h
// Alocador de faixas para UM buffer de instancias persistente (so std: sem
// OpenGL - testado em tests/InstanceRingTests.cpp). Antes, cada lote
// instanciado chamava glNamedBufferData (realoca o armazenamento todo vez).
// Agora o buffer tem uma capacidade fixa e cada lote ganha uma FAIXA nova
// (offset crescente), gravada com glNamedBufferSubData: lotes do mesmo quadro
// nunca se sobrescrevem, entao a GPU nao precisa esperar o anterior terminar.
// Quando a faixa nao cabe (fim do buffer) ou o buffer precisa crescer, o
// chamador "orfana" (glNamedBufferData com nullptr e a nova capacidade): os
// desenhos em voo continuam lendo o armazenamento antigo e o offset volta a 0.
// ============================================================================

#include <cstddef>

namespace Prism {

    class InstanceRing {
    public:
        struct Allocation {
            size_t Offset = 0;        // byte onde gravar
            bool Orphan = false;      // true: chamar glNamedBufferData(buffer, Capacity, nullptr) ANTES de gravar
            size_t Capacity = 0;      // capacidade atual do buffer (valida com Orphan)
        };

        static constexpr size_t kMinCapacity = 1u << 20;   // 1 MiB
        static constexpr size_t kAlignment = 16;
        static constexpr size_t kMaxCapacity = size_t(1) << 30; // 1 GiB: teto contra pedido absurdo

        // 'bytes' == 0 ou acima do teto: devolve Offset 0 sem Orphan e Capacity 0 (o chamador nao deve desenhar).
        Allocation Allocate(size_t bytes) {
            Allocation a;
            if (bytes == 0 || bytes > kMaxCapacity)
                return a;
            const size_t size = (bytes + kAlignment - 1) & ~(kAlignment - 1);
            if (m_Capacity == 0 || size > m_Capacity) {
                size_t capacity = m_Capacity ? m_Capacity : kMinCapacity;
                while (capacity < size && capacity < kMaxCapacity)
                    capacity *= 2;
                m_Capacity = capacity;
                m_Head = 0;
                a.Orphan = true;
            }
            else if (m_Head + size > m_Capacity) {
                m_Head = 0;
                a.Orphan = true;
            }
            a.Offset = m_Head;
            a.Capacity = m_Capacity;
            m_Head += size;
            return a;
        }

        size_t Capacity() const { return m_Capacity; }
        size_t Head() const { return m_Head; }

    private:
        size_t m_Capacity = 0;
        size_t m_Head = 0;
    };

}
