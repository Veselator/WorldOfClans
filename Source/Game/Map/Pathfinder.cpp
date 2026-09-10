#include "Pathfinder.h"

#include <algorithm>
#include <queue>
#include <limits>

namespace woc
{
    namespace
    {
        struct OpenNode
        {
            f32 estimated;
            u32 index;
            bool operator>(const OpenNode& other) const { return estimated > other.estimated; }
        };

        constexpr i32 kNeighbourOffsets[8][2] = {
            { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
            { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
        };

        constexpr f32 kDiagonal = 1.41421356f;

        f32 OctileHeuristic(const Coord& a, const Coord& b)
        {
            const f32 dx = static_cast<f32>(std::abs(a.x - b.x));
            const f32 dy = static_cast<f32>(std::abs(a.y - b.y));
            return (dx + dy) + (kDiagonal - 2.0f) * std::min(dx, dy);
        }
    }

    PathResult Pathfinder::FindPath(const MapData& map, const Coord& start, const Coord& goal,
                                    const TileCostFn& cost, u32 maxNodes) const
    {
        PathResult result;
        if (!map.InBounds(start) || !map.InBounds(goal)) return result;
        if (cost(map, goal) < 0.0f) return result;

        const size_t tileCount = map.Tiles().size();
        const f32 infinity = std::numeric_limits<f32>::max();

        std::vector<f32> best(tileCount, infinity);
        std::vector<u32> cameFrom(tileCount, kInvalidId);
        std::vector<u8> closed(tileCount, 0);
        std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;

        const u32 startIndex = static_cast<u32>(map.Index(start));
        const u32 goalIndex = static_cast<u32>(map.Index(goal));
        best[startIndex] = 0.0f;
        open.push({ OctileHeuristic(start, goal), startIndex });

        const i32 width = static_cast<i32>(map.TileWidth());
        const i32 height = static_cast<i32>(map.TileHeight());
        u32 expanded = 0;

        while (!open.empty() && expanded < maxNodes)
        {
            const OpenNode node = open.top();
            open.pop();
            if (closed[node.index]) continue;
            closed[node.index] = 1;
            ++expanded;

            if (node.index == goalIndex)
            {
                result.found = true;
                result.cost = best[goalIndex];
                for (u32 current = goalIndex; current != kInvalidId; current = cameFrom[current])
                {
                    result.tiles.push_back({ static_cast<i32>(current % map.TileWidth()),
                                             static_cast<i32>(current / map.TileWidth()) });
                    if (current == startIndex) break;
                }
                std::reverse(result.tiles.begin(), result.tiles.end());
                return result;
            }

            const i32 x = static_cast<i32>(node.index % map.TileWidth());
            const i32 y = static_cast<i32>(node.index / map.TileWidth());

            for (int i = 0; i < 8; ++i)
            {
                const i32 nx = x + kNeighbourOffsets[i][0];
                const i32 ny = y + kNeighbourOffsets[i][1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;

                const Coord neighbour{ nx, ny };
                const u32 neighbourIndex = static_cast<u32>(map.Index(neighbour));
                if (closed[neighbourIndex]) continue;

                const f32 stepCost = cost(map, neighbour);
                if (stepCost < 0.0f) continue;

                const bool diagonal = i >= 4;
                if (diagonal)
                {
                    // Refuse to cut a corner between two blocked tiles.
                    if (cost(map, { x + kNeighbourOffsets[i][0], y }) < 0.0f) continue;
                    if (cost(map, { x, y + kNeighbourOffsets[i][1] }) < 0.0f) continue;
                }

                const f32 tentative = best[node.index] + stepCost * (diagonal ? kDiagonal : 1.0f);
                if (tentative >= best[neighbourIndex]) continue;

                best[neighbourIndex] = tentative;
                cameFrom[neighbourIndex] = node.index;
                open.push({ tentative + OctileHeuristic(neighbour, goal), neighbourIndex });
            }
        }

        return result;
    }

    void Pathfinder::FloodFill(const MapData& map, const Coord& start, f32 budget,
                               const TileCostFn& cost, std::vector<f32>& outCost, u32 maxNodes) const
    {
        const size_t tileCount = map.Tiles().size();
        const f32 infinity = std::numeric_limits<f32>::max();
        outCost.assign(tileCount, infinity);
        if (!map.InBounds(start) || budget <= 0.0f) return;

        std::vector<u8> closed(tileCount, 0);
        std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;

        const u32 startIndex = static_cast<u32>(map.Index(start));
        outCost[startIndex] = 0.0f;
        open.push({ 0.0f, startIndex });

        const i32 width = static_cast<i32>(map.TileWidth());
        const i32 height = static_cast<i32>(map.TileHeight());
        u32 expanded = 0;

        while (!open.empty() && expanded < maxNodes)
        {
            const OpenNode node = open.top();
            open.pop();
            if (closed[node.index]) continue;
            closed[node.index] = 1;
            ++expanded;

            const i32 x = static_cast<i32>(node.index % map.TileWidth());
            const i32 y = static_cast<i32>(node.index / map.TileWidth());

            for (int i = 0; i < 8; ++i)
            {
                const i32 nx = x + kNeighbourOffsets[i][0];
                const i32 ny = y + kNeighbourOffsets[i][1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;

                const Coord neighbour{ nx, ny };
                const u32 neighbourIndex = static_cast<u32>(map.Index(neighbour));
                if (closed[neighbourIndex]) continue;

                const f32 stepCost = cost(map, neighbour);
                if (stepCost < 0.0f) continue;

                const bool diagonal = i >= 4;
                if (diagonal)
                {
                    if (cost(map, { x + kNeighbourOffsets[i][0], y }) < 0.0f) continue;
                    if (cost(map, { x, y + kNeighbourOffsets[i][1] }) < 0.0f) continue;
                }

                const f32 tentative = outCost[node.index] + stepCost * (diagonal ? kDiagonal : 1.0f);
                if (tentative > budget || tentative >= outCost[neighbourIndex]) continue;

                outCost[neighbourIndex] = tentative;
                open.push({ tentative, neighbourIndex });
            }
        }
    }

    std::vector<Vec2> Pathfinder::ToWaypoints(const MapData& map, const PathResult& path)
    {
        std::vector<Vec2> waypoints;
        if (path.tiles.empty()) return waypoints;

        waypoints.reserve(path.tiles.size());
        // Collapse runs that share a direction: fewer waypoints, identical geometry.
        Coord previousDirection{ 0, 0 };
        for (size_t i = 0; i < path.tiles.size(); ++i)
        {
            const bool last = (i + 1 == path.tiles.size());
            if (last) { waypoints.push_back(map.ToMap(path.tiles[i])); break; }

            const Coord direction = path.tiles[i + 1] - path.tiles[i];
            if (direction != previousDirection)
            {
                waypoints.push_back(map.ToMap(path.tiles[i]));
                previousDirection = direction;
            }
        }
        return waypoints;
    }
}
