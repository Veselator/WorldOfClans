// Pathfinder.h - A* over the tile grid, plus the Dijkstra flood the coverage field needs.
//
// The same cost model drives both: a march and a lord's reach are limited by the same
// hills, forests and rivers, which is why borders end up hugging the terrain.
#pragma once

#include "MapData.h"
#include "../../Core/Singleton.h"
#include <functional>

namespace woc
{
    struct PathResult
    {
        std::vector<Coord> tiles;   // from start to goal, inclusive
        f32 cost = 0.0f;
        bool found = false;
    };

    /// Cost of entering a tile; return a negative value to declare it impassable.
    using TileCostFn = std::function<f32(const MapData&, const Coord&)>;

    class Pathfinder final : public Singleton<Pathfinder>
    {
        friend class Singleton<Pathfinder>;
    public:
        /// Classic A* with an octile heuristic. `maxNodes` bounds the work per call.
        PathResult FindPath(const MapData& map, const Coord& start, const Coord& goal,
                            const TileCostFn& cost, u32 maxNodes = 200000) const;

        /// Uniform-cost flood outwards from `start` until `budget` is spent. Writes the
        /// accumulated cost for every reached tile into `outCost` (indexed like MapData).
        void FloodFill(const MapData& map, const Coord& start, f32 budget,
                       const TileCostFn& cost, std::vector<f32>& outCost, u32 maxNodes = 120000) const;

        /// Convenience wrappers around the two standard cost models.
        static f32 MovementCost(const MapData& map, const Coord& tile) { return map.MoveCost(tile); }
        static f32 CoverageCost(const MapData& map, const Coord& tile) { return map.CoverageCost(tile); }

        /// Straight-line path in map pixels, resampled from a tile path.
        static std::vector<Vec2> ToWaypoints(const MapData& map, const PathResult& path);

    private:
        Pathfinder() = default;
        ~Pathfinder() = default;
    };
}
