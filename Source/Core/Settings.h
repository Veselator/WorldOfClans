// Settings.h - the player's own preferences, separate from the designers' balance files.
//
// game.json holds defaults; Config/settings.json holds whatever the player changed. Keeping
// them apart means a balance patch never silently overwrites someone's display choices.
#pragma once

#include "Singleton.h"
#include "Types.h"

namespace woc
{
    class Window;

    struct DisplayMode
    {
        u32 width = 0;
        u32 height = 0;
        std::string Label() const { return std::to_string(width) + " x " + std::to_string(height); }
    };

    class Settings final : public Singleton<Settings>
    {
        friend class Singleton<Settings>;
    public:
        /// Reads settings.json, falling back to the defaults in game.json.
        void Load();
        void Save() const;

        /// Pushes the current values into the window, renderer, camera and simulation.
        void Apply(Window& window);

        // --- display -------------------------------------------------------------------
        bool fullscreen = true;
        u32 windowWidth = 1600;
        u32 windowHeight = 900;
        bool vsync = true;

        // --- camera ---------------------------------------------------------------------
        f32 cameraPitch = 46.0f;
        f32 rotateSpeed = 90.0f;
        f32 edgeScroll = 8.0f;

        // --- world display ---------------------------------------------------------------
        bool showBorders = true;
        bool showLabels = true;
        f32 labelMinZoom = 1.3f;

        // --- game ------------------------------------------------------------------------
        i32 defaultSpeedIndex = 2;

        /// Resolutions offered in the settings screen.
        const std::vector<DisplayMode>& Resolutions() const { return m_resolutions; }

    private:
        Settings() = default;
        ~Settings() = default;

        std::vector<DisplayMode> m_resolutions;
    };
}
