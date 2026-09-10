#include "EventBus.h"

#include <algorithm>

namespace woc
{
    void EventBus::Unsubscribe(SubscriptionId id)
    {
        for (auto& [type, entries] : m_handlers)
        {
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                    [id](const Entry& e) { return e.id == id; }),
                entries.end());
        }
    }
}
