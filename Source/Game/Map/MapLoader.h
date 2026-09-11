// MapLoader.h - turns a map folder (Map.json + PNG layers) into simulation data and a mesh.
#pragma once

#include "MapData.h"
#include "../../Render/Renderer.h"

namespace woc
{
    struct MapDescription
    {
        std::string folder;         // folder name under Maps/
        std::string name;
        std::string description;
        u32 width = 0;
        u32 height = 0;
        u32 tilePixels = 4;
        u32 seed = 0;
    };

    class MapLoader
    {
    public:
        /// The map's baked portrait, written beside its layers when the map is saved and
        /// read back wherever a small picture of the world is wanted. Returns false when the
        /// map has none - an old map saved before portraits existed.
        static bool LoadMinimap(const std::string& folder, ImageData& out);
        /// Bakes the portrait and writes it into the map's folder.
        static bool SaveMinimap(const std::string& folder, const MapData& map);
        /// The portrait, baking and saving one first if the map has none. Maps written
        /// before portraits existed pay for this once and are then as cheap as the rest.
        static bool EnsureMinimap(const std::string& folder, ImageData& out);

        /// Reads Map.json only - used by the party set-up screen to list playable maps.
        static bool ReadDescription(const std::string& folder, MapDescription& out);
        static std::vector<MapDescription> ListMaps();

        /// Loads every layer and fills `outMap`.
        static bool Load(const std::string& folder, MapData& outMap, MapDescription& outDescription);

        /// Builds the renderable grid. `step` is the spacing in map pixels.
        static TerrainMesh BuildMesh(const MapData& map, u32 step);

        /// Writes a Map.json/PNG set; the editor uses this to save generated maps.
        static bool Save(const std::string& folder, const MapData& map, const MapDescription& description);
    };
}
