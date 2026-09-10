// SettingsScene.h - display and control preferences.
#pragma once

#include "IScene.h"
#include "../Core/Math.h"

namespace woc
{
    class SettingsScene final : public IScene
    {
    public:
        SceneId Id() const override { return SceneId::Settings; }
        const char* Name() const override { return "Settings"; }

        void OnEnter() override;
        void OnExit() override;
        void Update(f32 deltaTime) override;
        void Render() override;

    private:
        void DrawDisplay(const Rect& area);
        void DrawGameplay(const Rect& area);

        i32 m_resolutionIndex = 2;
        bool m_dirty = false;
        std::string m_status;
        f32 m_statusTimer = 0.0f;
    };
}
