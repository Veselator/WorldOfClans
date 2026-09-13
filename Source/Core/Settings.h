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
        /// The pitch is deliberately not here. The whole look of the map - how the sprites
        /// sit on the ground, where the labels land, how far the horizon is - is built
        /// around one angle, and letting it be dragged about only lets the player break it.
        /// It comes from camera/pitchDegrees in game.json and stays there.
        f32 rotateSpeed = 90.0f;
        f32 edgeScroll = 8.0f;

        // --- world display ---------------------------------------------------------------
        /// Frame counter in the top-left corner. A debug build wants it, a release build
        /// does not, so the default follows the configuration rather than a fixed value.
#ifdef _DEBUG
        bool showFps = true;
#else
        bool showFps = false;
#endif
        bool showBorders = true;
        /// How the frontier is drawn. Off by default: the map is painted in tiles and the
        /// hard edge is the honest picture of it. On, the line is smoothed into a drawn
        /// border - prettier, and a shade dearer to draw.
        bool smoothBorders = false;
        bool showLabels = true;
        f32 labelMinZoom = 1.3f;
        /// How large the interface is drawn, 1.0 being the size it was designed at. Every
        /// metric in the theme and every piece of text scales together, so the layout keeps
        /// its proportions instead of overflowing its panels.
        f32 uiScale = 1.0f;

        // --- sound -------------------------------------------------------------------------
        f32 masterVolume = 1.0f;
        f32 musicVolume = 0.45f;
        f32 sfxVolume = 0.8f;

        // --- game ------------------------------------------------------------------------
        i32 defaultSpeedIndex = 2;
        /// How often the party writes itself out, in game days. 0 is never. The choices the
        /// screen offers are a week, a fortnight, a month, half a year and a year, because
        /// those are the intervals the game's own clock is read in.
        i32 autosaveDays = 0;

        /// The intervals offered, and their names, kept beside each other so the screen and
        /// the loader never disagree about what a stored number means.
        static const std::vector<i32>& AutosaveChoices();
        static std::string AutosaveLabel(i32 days);

        /// Resolutions offered in the settings screen.
        const std::vector<DisplayMode>& Resolutions() const { return m_resolutions; }

    private:
        Settings() = default;
        ~Settings() = default;

        std::vector<DisplayMode> m_resolutions;
    };
}
