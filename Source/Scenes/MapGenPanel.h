// MapGenPanel.h - the generator's controls, in one place.
//
// The party screen, the lobby and the map editor all set up the same generator, and for a
// while each of them drew its own idea of what the knobs were. They now share this block,
// so a world built in the editor and a world built from the menu are built by the same
// dialogue - and, because they also share one settings object, by the same numbers.
#pragma once

#include "../Game/Map/MapGenerator.h"
#include "../UI/UI.h"

#include <string>

namespace woc
{
    /// What the panel needs to remember between frames but the generator does not care
    /// about: which preset is highlighted, and the half-typed contents of its text fields.
    struct MapGenPanelState
    {
        i32 preset = 0;
        std::string seedText = "0";
        std::string widthText = "1920";
        std::string heightText = "1080";

        /// Points the text fields at the numbers currently in `settings`.
        void SyncText(const MapGenSettings& settings);
    };

    namespace MapGenPanel
    {
        /// Draws the whole block into a column and returns the y it finished at.
        /// `realmCount` is how many islands "one island per realm" should make.
        f32 Draw(UI& ui, f32 x, f32 y, f32 width, MapGenSettings& settings,
                 MapGenPanelState& state, i32 realmCount, bool includeSize = true);
    }
}
