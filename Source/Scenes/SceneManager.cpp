#include "SceneManager.h"
#include "../Core/Log.h"

namespace woc
{
    void SceneManager::Register(SceneId id, Creator creator)
    {
        m_creators[static_cast<int>(id)] = std::move(creator);
    }

    void SceneManager::Request(SceneId id)
    {
        m_pending = id;
        m_hasPending = true;
    }

    void SceneManager::ApplyPendingTransition()
    {
        if (!m_hasPending) return;
        m_hasPending = false;

        const auto it = m_creators.find(static_cast<int>(m_pending));
        if (it == m_creators.end())
        {
            WOC_LOG_ERROR("No scene registered for id ", static_cast<int>(m_pending));
            return;
        }

        if (m_current)
        {
            // Remembered before the screen is torn down, and only when we are actually
            // going somewhere else: re-entering the same screen would otherwise erase
            // the trail back.
            if (m_current->Id() != m_pending) m_previous = m_current->Id();
            m_current->OnExit();
            m_current.reset();
        }

        m_current = it->second();
        if (m_current)
        {
            WOC_LOG_INFO("Entering scene: ", m_current->Name());
            m_current->OnEnter();
        }
    }

    void SceneManager::Update(f32 deltaTime)
    {
        if (m_current) m_current->Update(deltaTime);
    }

    void SceneManager::Render()
    {
        if (m_current) m_current->Render();
    }

    void SceneManager::Shutdown()
    {
        if (m_current)
        {
            m_current->OnExit();
            m_current.reset();
        }
        m_creators.clear();
        m_payload.clear();
    }

    const Json& SceneManager::Data(const std::string& key) const
    {
        static const Json s_null;
        const auto it = m_data.find(key);
        return it == m_data.end() ? s_null : it->second;
    }

    std::string SceneManager::Payload(const std::string& key, const std::string& fallback) const
    {
        const auto it = m_payload.find(key);
        return it == m_payload.end() ? fallback : it->second;
    }
}
