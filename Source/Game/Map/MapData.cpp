#include "MapData.h"
#include "../../Core/Config.h"
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

    void MapData::Resize(u32 pixelWidth, u32 pixelHeight, u8 fill)
    {
        const u32 tilePixels = m_tilePixels;
        const u32 newTileWidth = std::max(1u, pixelWidth / tilePixels);
        const u32 newTileHeight = std::max(1u, pixelHeight / tilePixels);
        if (newTileWidth == m_tileWidth && newTileHeight == m_tileHeight) return;

        // The old map is kept where the two overlap, anchored at the top-left corner: a
        // designer who widens a map expects to find his coastline where he left it.
        std::vector<Tile> fresh(static_cast<size_t>(newTileWidth) * newTileHeight);
        for (Tile& tile : fresh) tile.terrain = fill;

        const u32 copyWidth = std::min(newTileWidth, m_tileWidth);
        const u32 copyHeight = std::min(newTileHeight, m_tileHeight);
        for (u32 y = 0; y < copyHeight; ++y)
        {
            for (u32 x = 0; x < copyWidth; ++x)
            {
                fresh[static_cast<size_t>(y) * newTileWidth + x] =
                    m_tiles[static_cast<size_t>(y) * m_tileWidth + x];
            }
        }

        m_tiles = std::move(fresh);
        m_tileWidth = newTileWidth;
        m_tileHeight = newTileHeight;
        m_pixelWidth = newTileWidth * tilePixels;
        m_pixelHeight = newTileHeight * tilePixels;
        m_colorPixels.assign(static_cast<size_t>(m_pixelWidth) * m_pixelHeight * 4, 255);
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

        // Height comes from the height map and from nowhere else. The terrain type used to
        // contribute its own base here, which meant that repainting a plain as hillside
        // silently lifted the ground - two tools quietly doing each other's work. Water is
        // the one exception: it is always at the datum, whatever was painted underneath.
        for (Tile& tile : m_tiles)
        {
            tile.elevation = terrain.At(tile.terrain).water ? 0.0f : tile.height;
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

    ImageData MapData::BuildMinimapImage(u32 width) const
    {
        ImageData out;
        if (!IsValid() || width == 0 || m_colorPixels.empty()) return out;

        out.width = width;
        out.height = std::max(1u, static_cast<u32>(std::lround(
            static_cast<f64>(width) * m_pixelHeight / m_pixelWidth)));
        out.channels = 4;
        out.pixels.assign(static_cast<size_t>(out.width) * out.height * 4, 255);

        const TerrainDatabase& terrain = TerrainDatabase::Get();

        // The sea as the game paints it, not as the colour layer keys it.
        ConfigManager& config = ConfigManager::Get();
        const Color deep = Color::FromRGB(static_cast<u32>(
            std::strtoul(config.Str("render/water/deepColor", "0b6d88").c_str(), nullptr, 16)));
        const Color shallowSea = Color::FromRGB(static_cast<u32>(
            std::strtoul(config.Str("render/water/shallowColor", "1ad8cf").c_str(), nullptr, 16)));
        const f32 deepR = deep.r, deepG = deep.g, deepB = deep.b;
        const f32 shallowR = shallowSea.r, shallowG = shallowSea.g, shallowB = shallowSea.b;

        // How far each stretch of water is from the shore, so the shallows read lighter.
        const std::vector<u8> shore = BuildShoreMask(6);

        // One output texel covers a block of map pixels, and every one of them is read:
        // point-sampling a 1920-wide layer down to 384 throws away five pixels in six and
        // turns a coastline into a dotted line.
        const f32 stepX = static_cast<f32>(m_pixelWidth) / out.width;
        const f32 stepY = static_cast<f32>(m_pixelHeight) / out.height;

        for (u32 py = 0; py < out.height; ++py)
        {
            const u32 y0 = static_cast<u32>(py * stepY);
            const u32 y1 = std::min(m_pixelHeight, static_cast<u32>((py + 1) * stepY) + 1);

            for (u32 px = 0; px < out.width; ++px)
            {
                const u32 x0 = static_cast<u32>(px * stepX);
                const u32 x1 = std::min(m_pixelWidth, static_cast<u32>((px + 1) * stepX) + 1);

                u32 r = 0, g = 0, b = 0, count = 0;
                for (u32 y = y0; y < y1; ++y)
                {
                    const u8* row = &m_colorPixels[(static_cast<size_t>(y) * m_pixelWidth + x0) * 4];
                    for (u32 x = x0; x < x1; ++x, row += 4)
                    {
                        r += row[0]; g += row[1]; b += row[2]; ++count;
                    }
                }
                if (count == 0) continue;

                f32 cr = static_cast<f32>(r) / count / 255.0f;
                f32 cg = static_cast<f32>(g) / count / 255.0f;
                f32 cb = static_cast<f32>(b) / count / 255.0f;

                // The tile under the middle of the block carries the things the colour layer
                // does not: the woods, and which way the ground falls.
                const Coord tile{ static_cast<i32>((x0 + x1) / 2) / static_cast<i32>(m_tilePixels),
                                  static_cast<i32>((y0 + y1) / 2) / static_cast<i32>(m_tilePixels) };
                const Tile& here = AtClamped(tile);
                const size_t tileIndex = static_cast<size_t>(std::clamp(tile.y, 0, static_cast<i32>(m_tileHeight) - 1)) * m_tileWidth +
                                         static_cast<size_t>(std::clamp(tile.x, 0, static_cast<i32>(m_tileWidth) - 1));
                const i32 shoreDepth = tileIndex < shore.size() && shore[tileIndex] > 0
                                     ? static_cast<i32>((255 - shore[tileIndex]) / 36) : 6;

                if (here.forest > 0.02f)
                {
                    const f32 k = std::min(here.forest * 0.75f, 0.6f);
                    cr += (0.12f - cr) * k;
                    cg += (0.26f - cg) * k;
                    cb += (0.10f - cb) * k;
                }

                if (terrain.At(here.terrain).water)
                {
                    // The colour layer paints the sea in the flat key colour the renderer
                    // matches on, which is not a colour anybody should have to look at. The
                    // portrait shows the sea the game actually draws, shallower by the shore.
                    const f32 shallow = Clamp01(1.0f - static_cast<f32>(shoreDepth) / 6.0f);
                    cr = deepR + (shallowR - deepR) * shallow;
                    cg = deepG + (shallowG - deepG) * shallow;
                    cb = deepB + (shallowB - deepB) * shallow;
                }
                else
                {
                    // A cheap north-west light: the slope between this tile and the one up
                    // and to the left is enough to make ranges legible at thumbnail size.
                    const f32 drop = here.elevation -
                                     AtClamped({ tile.x - 1, tile.y - 1 }).elevation;
                    const f32 shade = std::clamp(1.0f + drop * 3.2f, 0.72f, 1.28f);
                    cr *= shade; cg *= shade; cb *= shade;
                }

                u8* texel = out.At(px, py);
                texel[0] = static_cast<u8>(Clamp01(cr) * 255.0f);
                texel[1] = static_cast<u8>(Clamp01(cg) * 255.0f);
                texel[2] = static_cast<u8>(Clamp01(cb) * 255.0f);
                texel[3] = 255;
            }
        }
        return out;
    }

    std::vector<u8> MapData::BuildShoreMask(i32 reach) const
    {
        const TerrainDatabase& terrain = TerrainDatabase::Get();
        std::vector<u8> mask(m_tiles.size(), 0);
        if (reach <= 0 || m_tiles.empty()) return mask;

        const i32 width = static_cast<i32>(m_tileWidth);
        const i32 height = static_cast<i32>(m_tileHeight);
        static const i32 kSteps[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

        // The seed is the waterline itself - every tile with a neighbour of the opposite
        // wetness - and the sweep then runs outwards from it into the sea and into the land
        // alike, so one mask describes both halves of the band.
        std::vector<i32> distance(m_tiles.size(), -1);
        std::vector<u32> frontier;
        for (i32 y = 0; y < height; ++y)
        {
            for (i32 x = 0; x < width; ++x)
            {
                const size_t index = static_cast<size_t>(y) * m_tileWidth + x;
                const bool wet = terrain.At(m_tiles[index].terrain).water;

                bool onCoast = false;
                for (const auto& offset : kSteps)
                {
                    const i32 nx = x + offset[0];
                    const i32 ny = y + offset[1];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    const size_t neighbour = static_cast<size_t>(ny) * m_tileWidth + nx;
                    if (terrain.At(m_tiles[neighbour].terrain).water != wet) { onCoast = true; break; }
                }
                if (!onCoast) continue;

                distance[index] = 0;
                frontier.push_back(static_cast<u32>(index));
            }
        }

        for (i32 step = 1; step <= reach && !frontier.empty(); ++step)
        {
            std::vector<u32> next;
            for (u32 index : frontier)
            {
                const i32 x = static_cast<i32>(index) % width;
                const i32 y = static_cast<i32>(index) / width;
                for (const auto& offset : kSteps)
                {
                    const i32 nx = x + offset[0];
                    const i32 ny = y + offset[1];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;

                    const size_t neighbour = static_cast<size_t>(ny) * m_tileWidth + nx;
                    if (distance[neighbour] >= 0) continue;

                    distance[neighbour] = step;
                    next.push_back(static_cast<u32>(neighbour));

                }
            }
            frontier.swap(next);
        }

        for (size_t i = 0; i < m_tiles.size(); ++i)
        {
            if (distance[i] < 0) continue;   // too far from any coast to matter
            const f32 strength = 1.0f - static_cast<f32>(distance[i]) / static_cast<f32>(reach + 1);
            mask[i] = static_cast<u8>(Clamp01(strength) * 255.0f);
        }
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
        for (Tile& tile : m_tiles) { tile.owner = 0; tile.holder = 0; }
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
