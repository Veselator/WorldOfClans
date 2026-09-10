// EventBus.h - decoupled publish/subscribe used for simulation notifications.
//
// Systems raise events (a settlement revolted, a battle resolved, a marriage was
// arranged); the UI chronicle, the AI and the audio layer subscribe without any of
// them knowing about each other.
#pragma once

#include "Singleton.h"
#include "Types.h"
#include <functional>
#include <typeindex>
#include <unordered_map>

namespace woc
{
    class EventBus final : public Singleton<EventBus>
    {
        friend class Singleton<EventBus>;
    public:
        using SubscriptionId = u32;

        template <typename TEvent>
        SubscriptionId Subscribe(std::function<void(const TEvent&)> handler)
        {
            const SubscriptionId id = m_nextId++;
            m_handlers[std::type_index(typeid(TEvent))].push_back(
                { id, [handler = std::move(handler)](const void* payload)
                    {
                        handler(*static_cast<const TEvent*>(payload));
                    } });
            return id;
        }

        template <typename TEvent>
        void Publish(const TEvent& event)
        {
            const auto it = m_handlers.find(std::type_index(typeid(TEvent)));
            if (it == m_handlers.end()) return;
            // Copy first: handlers are allowed to subscribe or unsubscribe while running.
            const auto snapshot = it->second;
            for (const Entry& entry : snapshot) entry.invoke(&event);
        }

        void Unsubscribe(SubscriptionId id);
        void Clear() { m_handlers.clear(); }

    private:
        EventBus() = default;
        ~EventBus() = default;

        struct Entry
        {
            SubscriptionId id;
            std::function<void(const void*)> invoke;
        };

        std::unordered_map<std::type_index, std::vector<Entry>> m_handlers;
        SubscriptionId m_nextId = 1;
    };
}
