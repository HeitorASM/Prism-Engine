#pragma once

// ============================================================================
// Event.h
// Sistema de eventos da engine. Toda comunicacao de "algo aconteceu"
// (redimensionar janela, tecla pressionada, mouse moveu) passa por aqui.
//
// Por que isso importa desde o inicio:
// O Editor, o Runtime e a propria Window nunca vao se conhecer diretamente.
// A Window dispara eventos; a Application os recebe e repassa para a
// LayerStack; cada Layer decide se quer consumir o evento ou deixar passar
// adiante (ex: o Editor pode "engolir" um clique do mouse se estiver sobre
// um painel do ImGui, impedindo que a viewport 3D tambem reaja a ele).
// ============================================================================

#include "Base.h"
#include <string>
#include <sstream>
#include <functional>

namespace Prism {

    enum class EventType {
        None = 0,
        WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
        KeyPressed, KeyReleased, KeyTyped,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

    enum EventCategory {
        None = 0,
        EventCategoryApplication = PRISM_BIT(0),
        EventCategoryInput       = PRISM_BIT(1),
        EventCategoryKeyboard    = PRISM_BIT(2),
        EventCategoryMouse       = PRISM_BIT(3),
        EventCategoryMouseButton = PRISM_BIT(4)
    };

#define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; }\
                                EventType GetEventType() const override { return GetStaticType(); }\
                                const char* GetName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) int GetCategoryFlags() const override { return category; }

    class Event {
    public:
        virtual ~Event() = default;

        bool Handled = false;

        virtual EventType GetEventType() const = 0;
        virtual const char* GetName() const = 0;
        virtual int GetCategoryFlags() const = 0;
        virtual std::string ToString() const { return GetName(); }

        bool IsInCategory(EventCategory category) const {
            return GetCategoryFlags() & category;
        }
    };

    // Despachante: permite escrever "se este evento for do tipo X, chame esta funcao"
    // de forma type-safe, sem precisar de RTTI/dynamic_cast espalhado pelo codigo.
    class EventDispatcher {
    public:
        EventDispatcher(Event& event) : m_Event(event) {}

        template<typename T, typename F>
        bool Dispatch(const F& func) {
            if (m_Event.GetEventType() == T::GetStaticType()) {
                m_Event.Handled |= func(static_cast<T&>(m_Event));
                return true;
            }
            return false;
        }

    private:
        Event& m_Event;
    };

    using EventCallbackFn = std::function<void(Event&)>;

}
