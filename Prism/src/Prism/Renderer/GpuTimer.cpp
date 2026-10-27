#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "GpuTimer.h"
#include "../Core/Profiler.h"

namespace Prism {

    namespace {
        constexpr int kSlots = 4;        // quadros em voo (leitura de 3 quadros atras)
        constexpr int kMaxZones = 16;    // zonas de GPU por quadro

        struct Zone {
            const char* Name = nullptr;
            GLuint Start = 0, End = 0;
            bool Ended = false;
        };
        struct Slot {
            Zone Zones[kMaxZones];
            int Count = 0;
        };

        Slot s_Slots[kSlots];
        int s_Current = 0;
        uint64_t s_Frame = 0;
        GLFWwindow* s_Context = nullptr; // contexto dono das consultas
        bool s_Active = false;           // este quadro esta medindo?

        void FreeSlotQueries(Slot& slot) {
            for (int i = 0; i < kMaxZones; i++) {
                Zone& z = slot.Zones[i];
                if (z.Start) glDeleteQueries(1, &z.Start);
                if (z.End) glDeleteQueries(1, &z.End);
                z = Zone{};
            }
            slot.Count = 0;
        }

        // Le e publica as zonas de um slot antigo; zonas ainda nao prontas sao descartadas.
        void Collect(Slot& slot) {
            for (int i = 0; i < slot.Count; i++) {
                Zone& z = slot.Zones[i];
                if (!z.Ended || !z.Start || !z.End)
                    continue;
                GLint ready = 0;
                glGetQueryObjectiv(z.End, GL_QUERY_RESULT_AVAILABLE, &ready);
                if (!ready)
                    continue;
                GLuint64 t0 = 0, t1 = 0;
                glGetQueryObjectui64v(z.Start, GL_QUERY_RESULT, &t0);
                glGetQueryObjectui64v(z.End, GL_QUERY_RESULT, &t1);
                if (t1 >= t0)
                    Profiler::SetGpuZone(z.Name, (float)((double)(t1 - t0) * 1e-6)); // ns -> ms
            }
            // Mantem os objetos (reaproveitados); so zera a contagem.
            for (int i = 0; i < slot.Count; i++) { slot.Zones[i].Ended = false; slot.Zones[i].Name = nullptr; }
            slot.Count = 0;
        }
    }

    void GpuTimer::BeginFrame() {
        if (!Profiler::IsEnabled()) {
            s_Active = false;
            return;
        }
        GLFWwindow* current = glfwGetCurrentContext();
        if (!s_Context)
            s_Context = current;
        if (current != s_Context) {
            s_Active = false;
            return;
        }
        s_Frame++;
        // Le o slot mais antigo (3 quadros atras) e reaproveita o proximo para este quadro.
        Collect(s_Slots[(s_Frame + 1) % kSlots]);
        s_Current = (int)(s_Frame % kSlots);
        s_Slots[s_Current].Count = 0;
        s_Active = true;
    }

    void GpuTimer::Shutdown() {
        if (s_Context && glfwGetCurrentContext() == s_Context)
            for (Slot& slot : s_Slots)
                FreeSlotQueries(slot);
        s_Context = nullptr;
        s_Active = false;
    }

    GpuTimer::Scope::Scope(const char* name) {
        if (!s_Active || glfwGetCurrentContext() != s_Context)
            return;
        Slot& slot = s_Slots[s_Current];
        if (slot.Count >= kMaxZones)
            return;
        Zone& z = slot.Zones[slot.Count];
        if (!z.Start) glGenQueries(1, &z.Start);
        if (!z.End) glGenQueries(1, &z.End);
        z.Name = name;
        z.Ended = false;
        glQueryCounter(z.Start, GL_TIMESTAMP);
        m_Index = slot.Count++;
    }

    void GpuTimer::Scope::Stop() {
        if (m_Index < 0)
            return;
        Zone& z = s_Slots[s_Current].Zones[m_Index];
        glQueryCounter(z.End, GL_TIMESTAMP);
        z.Ended = true;
        m_Index = -1;
    }

}
