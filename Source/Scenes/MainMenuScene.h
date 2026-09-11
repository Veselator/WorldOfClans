// MainMenuScene.h - the title screen.
#pragma once

#include "IScene.h"
#include "../Core/Math.h"
#include "../Game/Map/MapData.h"
#include "../Game/SaveGame.h"

namespace woc
{
    class MainMenuScene final : public IScene
    {
    public:
        SceneId Id() const override { return SceneId::MainMenu; }
        const char* Name() const override { return "MainMenu"; }

        void OnEnter() override;
        void Update(f32 deltaTime) override;
        void Render() override;

    private:
        void DrawBackdrop();
        void DrawMenu();
        void DrawLoadDialog();

        /// Picks one of the installed maps at random and puts it behind the menu.
        void LoadBackdropMap();
        void UpdateBackdropCamera(f32 deltaTime);

        MapData m_backdrop;
        bool m_hasBackdrop = false;
        f32 m_intro = 0.0f;        // 0..1, the opening push-in
        f32 m_drift = 0.0f;        // how far the slow orbit has turned

        std::vector<SaveSlot> m_saves;
        bool m_loadOpen = false;
        i32 m_selectedSave = -1;
        f32 m_saveScroll = 0.0f;
        f32 m_time = 0.0f;
        std::string m_version = "v1.0";
    };
}
