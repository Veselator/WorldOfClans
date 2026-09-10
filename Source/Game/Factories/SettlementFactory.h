// SettlementFactory.h - creates settlements, with the placement rules applied.
#pragma once

#include "../World/Settlement.h"
#include "../../Core/Random.h"

namespace woc
{
    class World;
    class MapData;

    struct SettlementRequest
    {
        SettlementKind kind = SettlementKind::Village;
        Vec2 position;
        std::string raceId = "human";
        std::string faithId;          // empty = the race's default faith
        EntityId owner = kInvalidId;  // kInvalidId = independent
        i32 population = 0;           // 0 = pick a plausible figure for the kind
        std::string name;             // empty = draw from the name pool
    };

    class SettlementFactory
    {
    public:
        static Settlement& Create(World& world, const SettlementRequest& request, Random& random);

        /// Placement validity: buildable ground, far enough from its neighbours.
        static bool CanPlace(const World& world, const MapData& map, SettlementKind kind, const Vec2& position);

        /// Finds a spot near `origin` that satisfies CanPlace, or returns false.
        static bool FindSite(const World& world, const MapData& map, SettlementKind kind,
                             const Vec2& origin, f32 searchRadius, Random& random, Vec2& outPosition);
    };
}
