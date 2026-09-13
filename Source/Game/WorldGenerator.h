// WorldGenerator.h - turns party settings into a living world.
//
// Either replays the map's authored MapObjects.json, or seeds a fresh set of realms on
// the terrain when none is present (or when the player asked for a random start).
#pragma once

#include "../Core/Types.h"
#include "../Core/Math.h"
#include "../Core/Json.h"
#include "Map/MapGenerator.h"
#include "Systems/BanditSystem.h"

namespace woc
{
    class World;
    class MapData;

    /// One realm at the table, with everything that is decided about it before the first
    /// day. A seat is a seat whether a person or the machine is sitting in it, which is
    /// what lets the single-player screen and the lobby hand the generator the same thing.
    struct SeatSettings
    {
        /// Empty means "draw one" - the party screen's "випадковий" choice.
        std::string raceId;
        u32 color = 0xC8452D;
        /// Who is at the table. In a single-player party exactly one seat is human.
        bool human = false;
        /// Shown in the lobby and in the realm list; empty falls back to the realm's name.
        std::string playerName;
        /// Which network peer holds this seat, if any. Empty for AI and for the local game.
        std::string peerId;

        Json ToJson() const;
        static SeatSettings FromJson(const Json& node);
    };

    struct PartySettings
    {
        std::string mapFolder = "Test";
        u32 seed = 0;                       // 0 = draw one
        i32 stateCount = 5;
        f32 aiAggression = 0.55f;
        /// Whether the player only sees what his own people can see.
        bool fogOfWar = false;
        /// The robbers, and how thick the country is with them. They are drawn fresh for
        /// every party rather than stored with the map, so the same world is never robbed
        /// in the same places twice.
        BanditSettings bandits;

        /// One entry per realm; short lists are padded with AI seats when the party starts.
        std::vector<SeatSettings> seats;
        /// Which seat the player at this machine takes. In multiplayer each client sets
        /// its own, and the seats themselves are identical on every machine.
        i32 humanSeat = 0;

        /// Start on fresh ground instead of an authored map. The landscape is generated and
        /// written into Maps/ before the party begins, so from then on it is a map like any
        /// other - loadable from a save, and sendable to another player.
        bool generateMap = false;
        MapGenSettings mapGen;

        /// The seat as the generator should read it, padded and with the gaps filled in.
        SeatSettings SeatFor(size_t index) const;

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
