// WorldGenerator.h - turns party settings into a living world.
//
// Either replays the map's authored MapObjects.json, or seeds a fresh set of realms on
// the terrain when none is present (or when the player asked for a random start).
#pragma once

#include "../Core/Types.h"
#include "../Core/Math.h"
#include "../Core/Json.h"

namespace woc
{
    class World;
    class MapData;

    struct PartySettings
    {
        std::string mapFolder = "Test";
        u32 seed = 0;                       // 0 = draw one
        i32 stateCount = 5;
        std::string playerRace = "human";
        bool randomiseRaces = true;
        f32 aiAggression = 0.55f;
        /// Whether the player only sees what his own people can see.
        bool fogOfWar = false;

        /// Banner colour of the player's realm.
        u32 playerColor = 0xC8452D;
        /// Banner colours of the rival realms, in order; short lists fall back to the palette.
        std::vector<u32> rivalColors;

        Json ToJson() const;
        static PartySettings FromJson(const Json& node);
    };

    class WorldGenerator
    {
    public:
        /// Loads the map, populates the world and creates the player seats.
        static bool Generate(World& world, const PartySettings& settings);

        /// Writes the current world out as MapObjects.json next to the map.
        static bool SaveObjects(const World& world, const std::string& mapFolder);
        /// Reads MapObjects.json into the world; returns false when the file is absent.
        /// `includeRealms` is false for a new party, which keeps the map's roads, mines,
        /// forests and fields but seeds fresh realms from the party settings.
        static bool LoadObjects(World& world, const std::string& mapFolder, bool includeRealms = true);

        /// Uploads the loaded map to the renderer and frames the camera on it. Public because
        /// loading a save reaches the same point by a different road.
        static void PublishMapToRenderer(World& world);
        /// Publishes bare terrain with no realms on it - the title screen's backdrop.
        static void PublishMapToRenderer(const MapData& map);

    private:
        static void SeedRealms(World& world, const PartySettings& settings);
        static void SeedMines(World& world);
        static void CreatePlayers(World& world, const PartySettings& settings);
    };
}
