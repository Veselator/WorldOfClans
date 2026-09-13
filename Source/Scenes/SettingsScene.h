// SettingsScene.h - display, sound and control preferences.
//
// Not a screen of its own: the settings live inside the title screen, over the same drifting
// map, so changing them never costs the player the backdrop he was looking at. It is a panel
// the menu opens, exactly like the load dialog beside it.
#pragma once

#include "../Core/Math.h"

#include <string>

namespace woc
{
    class SettingsPanel
    {
    public:
        /// Reads the current settings into the controls and plays the panel in.
        void Open();
        /// Puts the panel away, persisting whatever was changed. Nobody expects to lose a
        /// toggle by pressing Escape.
        void Close();
        bool IsOpen() const { return m_open; }

        /// Escape and the status line's clock.
        void Update(f32 deltaTime);
        /// Draws the whole overlay - the veil, the panel and its two columns.
        void Draw();

    private:
        void DrawDisplay(const Rect& area);
        void DrawGameplay(const Rect& area);

        i32 m_resolutionIndex = 2;
        bool m_open = false;
        bool m_dirty = false;
        std::string m_status;
        f32 m_statusTimer = 0.0f;
    };
}
