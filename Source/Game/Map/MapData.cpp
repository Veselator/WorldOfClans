#include "MapData.h"
#include "../../Core/Log.h"

#include <algorithm>
#include <deque>

namespace woc
{
    void MapData::Allocate(u32 pixelWidth, u32 pixelHeight, u32 tilePixels)
    {
        m_pixelWidth = pixelWidth;
        m_pixelHeight = pixelHeight;
        m_tilePixels = std::max(1u, tilePixels);
        m_tileWidth = std::max(1u, pixelWidth / m_tilePixels);
        m_tileHeight = std::max(1u, pixelHeight / m_tilePixels);
        m_tiles.assign(static_cast<size_t>(m_tileWidth) * m_tileHeight, Tile{});
    }

    Coord MapData::ToTile(const Vec2& mapPosition) const
    {
        return {
            std::clamp(static_cast<i32>(mapPosition.x) / static_cast<i32>(m_tilePixels), 0, static_cast<i32>(m_tileWidth) - 1),
            std::clamp(static_cast<i32>(mapPosition.y) / static_cast<i32>(m_tilePixels), 0, static_cast<i32>(m_tileHeight) - 1)
        };
    }

    Vec2 MapData::ToMap(const Coord& tile) const
    {
        const f32 half = m_tilePixels * 0.5f;
        return { tile.x * static_cast<f32>(m_tilePixels) + half, tile.y * static_cast<f32>(m_tilePixels) + half };
    }

    const Tile& MapData::AtClamped(const Coord& tile) const
    {
        const Coord clamped{
            std::clamp(tile.x, 0, static_cast<i32>(m_tileWidth) - 1),
            std::clamp(tile.y, 0, static_cast<i32>(m_tileHeight) - 1)
        };
        return m_tiles[Index(clamped)];
    }

    const TerrainInfo& MapData::TerrainAt(const Coord& tile) const
    {
        return TerrainDatabase::Get().At(AtClamped(tile).terrain);
    }

    const TerrainInfo& MapData::TerrainAtMap(const Vec2& mapPosition) const
    {
        return TerrainAt(ToTile(mapPosition));
    }

    f32 MapData::WorldHeight(const Coord& tile) const
    {
        return TerrainDatabase::Get().HeightScale() * AtClamped(tile).elevation;
    }

    f32 MapData::WorldHeightAtMap(const Vec2& mapPosition) const
    {
        // Bilinear across tile centres: the mesh is sampled far more finely than the grid,
        // and nearest-neighbour sampling would reintroduce the very steps we smoothed away.
        const f32 tileSize = static_cast<f32>(m_tilePixels);
        const f32 fx = mapPosition.x / tileSize - 0.5f;
        const f32 fy = mapPosition.y / tileSize - 0.5f;

        const i32 x0 = static_cast<i32>(std::floor(fx));
        const i32 y0 = static_cast<i32>(std::floor(fy));
        const f32 tx = fx - static_cast<f32>(x0);
        const f32 ty = fy - static_cast<f32>(y0);

        const f32 h00 = AtClamped({ x0,     y0     }).elevation;
        const f32 h10 = AtClamped({ x0 + 1, y0     }).elevation;
        const f32 h01 = AtClamped({ x0,     y0 + 1 }).elevation;
        const f32 h11 = AtClamped({ x0 + 1, y0 + 1 }).elevation;

        const f32 top = Lerp(h00, h10, tx);
        const f32 bottom = Lerp(h01, h11, tx);
        return TerrainDatabase::Get().HeightScale() * Lerp(top, bottom, ty);
    }

    void MapData::RebuildElevation(i32 smoothPasses)
    {
        const TerrainDatabase& terrain = TerrainDatabase::Get();

        // Raw combination first: the painted height map dominates, the terrain type
        // contributes its own base so hills read as hills even on a flat height map.
        for (Tile& tile : m_tiles)
        {
            const TerrainInfo& info = terrain.At(tile.terrain);
            tile.elevation = info.water ? 0.0f : (tile.height * 0.72f + info.heightFactor * 0.28f);
        }

        if (smoothPasses <= 0) return;

        const i32 width = static_cast<i32>(m_tileWidth);
        const i32 height = static_cast<i32>(m_tileHeight);
        std::vector<f32> buffer(m_tiles.size());

        for (i32 pass = 0; pass < smoothPasses; ++pass)
        {
            for (i32 y = 0; y < height; ++y)
            {
                for (i32 x = 0; x < width; ++x)
                {
                    // 3x3 tent filter, but the shoreline is pinned so coasts stay crisp.
                    const size_t index = Index({ x, y });
                    if (terrain.At(m_tiles[index].terrain).water) { buffer[index] = 0.0f; continue; }

                    f32 total = 0.0f;
                    f32 weightSum = 0.0f;
                    for (i32 dy = -1; dy <= 1; ++dy)
                    {
                        for (i32 dx = -1; dx <= 1; ++dx)
                        {
                            const Coord probe{ x + dx, y + dy };
                            if (!InBounds(probe)) continue;
                            const f32 weight = (dx == 0 && dy == 0) ? 4.0f : ((dx == 0 || dy == 0) ? 2.0f : 1.0f);
                            total += m_tiles[Index(probe)].elevation * weight;
                            weightSum += weight;
                        }
                    }
                    buffer[index] = weightSum > 0.0f ? total / weightSum : m_tiles[index].elevation;
                }
            }
            for (size_t i = 0; i < m_tiles.size(); ++i) m_tiles[i].elevation = buffer[i];
        }
    }

    f32 MapData::MoveCost(const Coord& tile) const
    {
        if (!InBounds(tile)) return -1.0f;
        const Tile& t = m_tiles[Index(tile)];
        const TerrainInfo& info = TerrainDatabase::Get().At(t.terrain);

        if (info.water)
        {
            if (t.bridged) return 1.0f;
            if (t.fordable) return TerrainDatabase::Get().FordableMoveCost();
            return -1.0f;
        }
        if (!info.passable) return -1.0f;

        f32 cost = info.moveCost;
        // Forest slows a marching column; a road more than makes up for it.
        cost *= 1.0f + t.forest * 0.45f;
        if (t.road > 0) cost *= 0.5f;
        return cost;
    }

    f32 MapData::CoverageCost(const Coord& tile) const
    {
        if (!InBounds(tile)) return -1.0f;
        const Tile& t = m_tiles[Index(tile)];
        const TerrainInfo& info = TerrainDatabase::Get().At(t.terrain);

        if (info.water)
        {
            if (t.bridged) return 1.2f;
            if (t.fordable) return TerrainDatabase::Get().FordableCoverageCost();
            return -1.0f;   // wide water is a hard border until a bridge is built
        }
        if (!info.passable) return -1.0f;

        f32 cost = info.coverageCost;
        cost *= 1.0f + t.forest * 0.30f;
        if (t.road > 0) cost *= 0.45f;
        return cost;
    }

    bool MapData::IsBuildable(const Coord& tile) const
    {
        if (!InBounds(tile)) return false;
        const TerrainInfo& info = TerrainDatabase::Get().At(m_tiles[Index(tile)].terrain);
        return info.buildable && info.passable && !info.water;
    }

    template <typename Fn>
    f32 MapData::SampleDisc(const Vec2& mapPosition, f32 radius, Fn&& fn) const
    {
        const Coord center = ToTile(mapPosition);
        const i32 span = std::max(1, static_cast<i32>(radius / static_cast<f32>(m_tilePixels)));
        const i32 spanSq = span * span;

        f32 total = 0.0f;
        u32 count = 0;
        for (i32 dy = -span; dy <= span; ++dy)
        {
            for (i32 dx = -span; dx <= span; ++dx)
            {
                if (dx * dx + dy * dy > spanSq) continue;
                const Coord probe{ center.x + dx, center.y + dy };
                if (!InBounds(probe)) continue;
                total += fn(m_tiles[Index(probe)]);
                ++count;
            }
        }
        return count > 0 ? total / static_cast<f32>(count) : 0.0f;
    }

    f32 MapData::SampleForest(const Vec2& mapPosition, f32 radius) const
    {
        return SampleDisc(mapPosition, radius, [](const Tile& t) { return t.forest; });
    }

    f32 MapData::SampleStone(const Vec2& mapPosition, f32 radius) const
    {
        return SampleDisc(mapPosition, radius,
            [](const Tile& t) { return TerrainDatabase::Get().At(t.terrain).stone; });
    }

    f32 MapData::SampleSoil(const Vec2& mapPosition, f32 radius) const
    {
        return SampleDisc(mapPosition, radius,
            [](const Tile& t) { return TerrainDatabase::Get().At(t.terrain).soil; });
    }

    f32 MapData::SampleWater(const Vec2& mapPosition, f32 radius) const
    {
        return SampleDisc(mapPosition, radius,
            [](const Tile& t) { return TerrainDatabase::Get().At(t.terrain).water ? 1.0f : 0.0f; });
    }

    f32 MapData::SampleCoast(const Vec2& mapPosition, f32 radius) const
    {
        const u8 coast = TerrainDatabase::Get().IndexOf("coast");
        return SampleDisc(mapPosition, radius,
            [coast](const Tile& t) { return t.terrain == coast ? 1.0f : 0.0f; });
    }

    f32 MapData::SampleField(const Vec2& mapPosition, f32 radius) const
    {
        return SampleDisc(mapPosition, radius, [](const Tile& t) { return t.field; });
    }

    u32 MapData::CountTilesInRadius(const Vec2& mapPosition, f32 radius) const
    {
        const Coord center = ToTile(mapPosition);
        const i32 span = std::max(1, static_cast<i32>(radius / static_cast<f32>(m_tilePixels)));
        u32 count = 0;
        for (i32 dy = -span; dy <= span; ++dy)
            for (i32 dx = -span; dx <= span; ++dx)
                if (dx * dx + dy * dy <= span * span && InBounds({ center.x + dx, center.y + dy })) ++count;
        return count;
    }

    std::vector<u8> MapData::BuildForestMask() const
    {
        std::vector<u8> mask(m_tiles.size());
        for (size_t i = 0; i < m_tiles.size(); ++i)
            mask[i] = static_cast<u8>(Clamp01(m_tiles[i].forest) * 255.0f + 0.5f);
        return mask;
    }

    std::vector<u8> MapData::BuildFieldMask() const
    {
        std::vector<u8> mask(m_tiles.size());
        for (size_t i = 0; i < m_tiles.size(); ++i)
            mask[i] = static_cast<u8>(Clamp01(m_tiles[i].field) * 255.0f + 0.5f);
        return mask;
    }

    std::vector<u8> MapData::BuildOwnerMask() const
    {
        std::vector<u8> mask(m_tiles.size());
        for (size_t i = 0; i < m_tiles.size(); ++i) mask[i] = m_tiles[i].owner;
        return mask;
    }

    void MapData::ClearOwners()
    {
        for (Tile& tile : m_tiles) tile.owner = 0;
    }

    void MapData::ComputeFordableWater(i32 fordRadius)
    {
        // Multi-source BFS from every land tile; water within `fordRadius` steps of land is
        // a narrow river or a shallow, and armies may wade across it.
        const size_t count = m_tiles.size();
        std::vector<i16> distance(count, -1);
        std::deque<u32> queue;

        for (u32 i = 0; i < count; ++i)
        {
            const TerrainInfo& info = TerrainDatabase::Get().At(m_tiles[i].terrain);
            if (!info.water)
            {
                distance[i] = 0;
                queue.push_back(i);
            }
            m_tiles[i].fordable = false;
        }

        const i32 width = static_cast<i32>(m_tileWidth);
        const i32 height = static_cast<i32>(m_tileHeight);

        while (!queue.empty())
        {
            const u32 current = queue.front();
            queue.pop_front();
            if (distance[current] >= fordRadius) continue;

            const i32 x = static_cast<i32>(current % m_tileWidth);
            const i32 y = static_cast<i32>(current / m_tileWidth);
            const i32 offsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const auto& offset : offsets)
            {
                const i32 nx = x + offset[0];
                const i32 ny = y + offset[1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                const u32 next = static_cast<u32>(ny) * m_tileWidth + static_cast<u32>(nx);
                if (distance[next] >= 0) continue;
                distance[next] = static_cast<i16>(distance[current] + 1);
                m_tiles[next].fordable = true;
                queue.push_back(next);
            }
        }

        u32 fordable = 0;
        for (const Tile& tile : m_tiles) if (tile.fordable) ++fordable;
        WOC_LOG_INFO("Fordable water tiles: ", fordable, " (radius ", fordRadius, ")");
    }
}
