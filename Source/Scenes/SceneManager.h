// SceneManager.h - owns the active screen and performs deferred transitions.
//
// Scenes are created through a registered factory, so a new screen is added by
// registering a creator rather than by editing a switch statement here.
#pragma once

#include "IScene.h"
#include "../Core/Singleton.h"
#include "../Core/Json.h"
#include <functional>
#include <unordered_map>

namespace woc
{
    class SceneManager final : public Singleton<SceneManager>
    {
        friend class Singleton<SceneManager>;
    public:
        using Creator = std::function<Scope<IScene>()>;

        void Register(SceneId id, Creator creator);

        /// Queues a transition; it happens between frames so a scene may request its own exit.
        void Request(SceneId id);
        bool HasPending() const { return m_hasPending; }

        /// Asks the application loop to terminate after the current frame.
        void RequestQuit() { m_quitRequested = true; }
        bool QuitRequested() const { return m_quitRequested; }

        void ApplyPendingTransition();
        void Update(f32 deltaTime);
        void Render();
        void Shutdown();

        IScene* Current() const { return m_current.get(); }

        /// Data handed to the next scene, e.g. the party settings chosen before a game starts.
        void SetPayload(const std::string& key, const std::string& value) { m_payload[key] = value; }
        std::string Payload(const std::string& key, const std::string& fallback = "") const;
        void ClearPayload() { m_payload.clear(); m_data.clear(); }

        /// Structured hand-off, for anything that does not fit in a string (party settings).
        void SetData(const std::string& key, Json value) { m_data[key] = std::move(value); }
        const Json& Data(const std::string& key) const;

    private:
        SceneManager() = default;
        ~SceneManager() = default;

        std::unordered_map<int, Creator> m_creators;
        std::unordered_map<std::string, std::string> m_payload;
        std::unordered_map<std::string, Json> m_data;
        Scope<IScene> m_current;
        SceneId m_pending = SceneId::MainMenu;
        bool m_hasPending = false;
        bool m_quitRequested = false;
    };
}
