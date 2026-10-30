#pragma once

// ============================================================================
// GpuTimer.h
// Tempo de GPU por zona (glQueryCounter/GL_TIMESTAMP, que permite zonas aninhadas).
// Os resultados chegam com ~3 quadros de atraso (nunca espera a GPU: so le a
// consulta quando ela ja esta pronta) e vao para o Profiler como zonas "GPU.*".
//
//   GpuTimer::BeginFrame();                    // 1x por quadro, na janela principal
//   { GpuTimer::Scope z("GPU.Sombras"); ... } // ou z.Stop() para encerrar antes
//
// So mede quando o Profiler esta ligado e so no contexto GL da janela principal
// (a janela de Play tem outro contexto, cujas consultas seriam invalidas).
// ============================================================================

#include <cstdint>

namespace Prism {

    class GpuTimer {
    public:
        static void BeginFrame();
        static void Shutdown();

        class Scope {
        public:
            explicit Scope(const char* name);
            ~Scope() { Stop(); }
            void Stop();
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;
        private:
            int m_Index = -1;
        };
    };

}
