// MapGenerator.h - procedural landscapes.
//
// The field is built in layers, and each layer is a knob the player can turn: a smooth
// continental mass that decides where land is at all, a ridged layer that decides where the
// mountains run, a warp that decides how ragged the coast is, and a moisture field that
// decides what grows. A single fractal pulled down at the edges - which is what this used to
// be - can only ever make one island with one hill in the middle of it, which is exactly
// what it kept making.
#pragma once

#include "MapData.h"
#include "MapLoader.h"
#include "../../Core/Json.h"

#include <string>
#include <vector>

namespace woc
{
    /// Everything the generator needs. All of it is live-editable in the editor and in the
    /// party screens, which is why it travels as its own little document.
    struct MapGenSettings
    {
        u32 seed = 0;              // 0 = draw one
        u32 width = 1920;
        u32 height = 1080;
        u32 tilePixels = 4;

        /// How many islands' worth of noise fits across the map. Larger is busier.
        f32 scale = 3.2f;
        f32 seaLevel = 0.42f;
        f32 mountains = 0.78f;
        f32 forest = 0.45f;
        /// How high the highest ground stands, on the same 0..1 scale a painted height map
        /// uses. The authored maps top out around 0.7 and spend most of their land between
        /// 0.2 and 0.4, and a generated world that does not match that reads as a plateau
        /// with the sea cut out of it.
        f32 relief = 0.95f;
        /// How the climb is shaped between the waterline and the highest ground. Above one
        /// the lowlands stay low and the rise is saved for the last stretch, which is what
        /// keeps a beach a beach while the interior gets its height.
        f32 reliefCurve = 1.7f;
        /// The share of the map, from each edge inwards, that is always open sea. Without
        /// it the noise can run right up to the boundary and the world ends in a cliff.
        f32 seaMargin = 0.09f;

        // --- the shape of the world ---------------------------------------------------------
        /// How much detail the height carries. Few octaves give soft rolling country; many
        /// give a busy, broken landscape with a great deal of small relief in it.
        i32 octaves = 5;
        /// How much of the height comes from ridged noise rather than smooth. Ridged noise
        /// makes *chains* - a spine of mountains with passes through it - where the smooth
        /// kind can only make one dome.
        f32 ridges = 0.4f;
        /// How hard the coastline is warped. At zero the shore follows the height field; the
        /// higher it goes the more the shore is dragged about into headlands, bays and fjords.
        f32 coastRoughness = 0.25f;
        /// How much land there is at all, before the sea level is applied.
        f32 landMass = 0.55f;
        /// Nought pulls the land apart into an archipelago; one gathers it into a single
        /// continent by flattening the radial falloff into one broad dome.
        f32 continent = 0.5f;
        /// How dry the world is. Dry country loses its forests, its black earth turns to
        /// loam and its worst ground turns to sand.
        f32 aridity = 0.0f;

        /// When above one: one island per realm rather than a single landmass, joined by
        /// narrow isthmuses so that an army can still march between them. The party screen
        /// sets it to the number of realms.
        i32 islands = 0;
        /// How wide the land bridges between those islands are, in map units. Nought leaves
        /// the islands separate, which is a different game entirely.
        f32 isthmusWidth = 26.0f;

        Json ToJson() const;
        static MapGenSettings FromJson(const Json& node);
    };

    /// A named set of those knobs. Presets are the honest way to expose a generator with a
    /// dozen parameters: the player picks the world he wants and then nudges it.
    struct MapGenPreset
    {
        std::string name;
        std::string description;
        MapGenSettings settings;
    };

    class MapGenerator
    {
    public:
        /// Builds the landscape into `map` and fills in the description that goes with it.
        /// Returns the seed actually used, so a drawn one can be shown back to the player.
        static u32 Generate(MapData& map, MapDescription& description, const MapGenSettings& settings,
                            const std::string& name = std::string());

        /// Repaints the full-resolution colour layer from the terrain indices.
        static void RebuildColorLayer(MapData& map);

        /// Generates a map and writes it into Maps/ as a folder of its own, so that from
        /// that moment it is an ordinary map: it can be saved into, loaded from a save,
        /// listed beside the authored ones and sent to another player. Returns the folder,
        /// or an empty string if it could not be written.
        static std::string GenerateAndStore(const MapGenSettings& settings, MapDescription& outDescription);

        /// The worlds the party screen offers by name. The size, the seed and the number of
        /// islands are the caller's business and are left alone by these.
        static const std::vector<MapGenPreset>& Presets();

        /// One set of knobs for the whole game - the party screen, the lobby and the editor all
        /// read and write this, so a world shaped in one of them is the world the next one
        /// offers. Without it the same dialogue in two places quietly means two things.
        static MapGenSettings& Shared();
    };
}
