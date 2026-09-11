// MapData.h - the simulation's view of the world grid.
//
// Everything the game reasons about spatially lives here: terrain, elevation, forest and
// field density, roads and the ownership field that produces the dynamic borders.
// One tile aggregates `TilePixels` map pixels; the colour layer stays at full resolution
// for rendering while the simulation works on the coarser grid.
#pragma once

#include "TerrainTypes.h"
#include "../../Core/ImageIO.h"
#include "../../Core/Math.h"

namespace woc
{
    struct Tile
    {
        u8 terrain = 0;
        u8 owner = 0;          // 0 = unclaimed, otherwise a realm palette slot
        u8 holder = 0;         // which individual seat's zone of influence this tile is in
        u8 road = 0;           // 0 = none, higher = better road
        bool fordable = false; // narrow water an army can wade across
        bool bridged = false;  // a bridge lets coverage and troops cross wide water
        f32 height = 0.0f;     // normalised 0..1 straight from the height map
        f32 elevation = 0.0f;  // smoothed, terrain-aware 0..1 used for geometry
        f32 forest = 0.0f;     // 0..1 density
        f32 field = 0.0f;      // 0..1 tilled coverage
    };

    class MapData
    {
    public:
        void Allocate(u32 pixelWidth, u32 pixelHeight, u32 tilePixels);
        /// Re-shapes the map, keeping what is already painted. Ground that falls outside
        /// the new bounds is lost; ground the map grows into is filled with `fill`.
        void Resize(u32 pixelWidth, u32 pixelHeight, u8 fill);

        // --- dimensions --------------------------------------------------------------------
        u32 PixelWidth() const { return m_pixelWidth; }
        u32 PixelHeight() const { return m_pixelHeight; }
        u32 TileWidth() const { return m_tileWidth; }
        u32 TileHeight() const { return m_tileHeight; }
        u32 TilePixels() const { return m_tilePixels; }
        bool IsValid() const { return m_tileWidth > 0 && m_tileHeight > 0; }

        // --- coordinate conversion ----------------------------------------------------------
        Coord ToTile(const Vec2& mapPosition) const;
        Vec2 ToMap(const Coord& tile) const;
        bool InBounds(const Coord& tile) const
        {
            return tile.x >= 0 && tile.y >= 0 &&
                   tile.x < static_cast<i32>(m_tileWidth) && tile.y < static_cast<i32>(m_tileHeight);
        }
        size_t Index(const Coord& tile) const
        {
            return static_cast<size_t>(tile.y) * m_tileWidth + static_cast<size_t>(tile.x);
        }

        // --- tile access ---------------------------------------------------------------------
        Tile& At(const Coord& tile) { return m_tiles[Index(tile)]; }
        const Tile& At(const Coord& tile) const { return m_tiles[Index(tile)]; }
        const Tile& AtClamped(const Coord& tile) const;
        std::vector<Tile>& Tiles() { return m_tiles; }
        const std::vector<Tile>& Tiles() const { return m_tiles; }

        const TerrainInfo& TerrainAt(const Coord& tile) const;
        const TerrainInfo& TerrainAtMap(const Vec2& mapPosition) const;

        // --- derived queries ------------------------------------------------------------------
        /// World-space elevation used by the renderer and by the battle height advantage.
        f32 WorldHeight(const Coord& tile) const;
        f32 WorldHeightAtMap(const Vec2& mapPosition) const;

        /// Movement cost multiplier for a single tile step; returns a negative value when
        /// the tile cannot be entered at all.
        f32 MoveCost(const Coord& tile) const;
        /// Cost consumed by a settlement's coverage flood; negative means "blocks coverage".
        f32 CoverageCost(const Coord& tile) const;

        bool IsPassable(const Coord& tile) const { return MoveCost(tile) > 0.0f; }
        bool IsBuildable(const Coord& tile) const;

        /// Average of a tile property over a disc, used for "is there forest/stone nearby".
        f32 SampleForest(const Vec2& mapPosition, f32 radius) const;
        f32 SampleStone(const Vec2& mapPosition, f32 radius) const;
        f32 SampleSoil(const Vec2& mapPosition, f32 radius) const;
        f32 SampleWater(const Vec2& mapPosition, f32 radius) const;
        f32 SampleCoast(const Vec2& mapPosition, f32 radius) const;
        f32 SampleField(const Vec2& mapPosition, f32 radius) const;

        u32 CountTilesInRadius(const Vec2& mapPosition, f32 radius) const;

        // --- layers shared with the renderer -------------------------------------------------
        std::vector<u8> BuildForestMask() const;
        std::vector<u8> BuildFieldMask() const;
        std::vector<u8> BuildOwnerMask() const;
        /// Nearness to the waterline, on both sides of it: 255 on the tiles that touch the
        /// coast - sea and beach alike - fading to 0 `reach` tiles away in either direction.
        ///
        /// Both sides, because the ground does not stop where the colour layer says the sea
        /// does: elevation runs smoothly across the coast, so a strip of what is drawn as
        /// land still reads as shallows. Surf that breaks only seaward of the line leaves
        /// that strip looking like flooded field. The sea pass shades its half and the
        /// terrain pass shades the other, from this one mask, so the band is continuous.
        std::vector<u8> BuildShoreMask(i32 reach) const;

        void ClearOwners();

        /// Combines the height map with each terrain's base elevation and smooths the result,
        /// so the rendered ground rolls instead of stepping between tiles.
        void RebuildElevation(i32 smoothPasses = 3);

        /// Marks narrow water as fordable by measuring the distance from each water tile
        /// to the nearest land; wide rivers and open sea stay impassable.
        void ComputeFordableWater(i32 fordRadius);

        /// The map's portrait: the colour layer boxed down to a small RGBA picture, with
        /// the forest darkening it and the slopes shaded, so that a thumbnail reads as
        /// country rather than as a flat colour chart. Baked once when a map is saved and
        /// then used everywhere a small picture of the world is wanted - the minimap, the
        /// map list before a party, the editor's browser.
        ///
        /// `width` is fixed; the height follows the map's own proportions so nothing is
        /// stretched.
        ImageData BuildMinimapImage(u32 width) const;

        const std::vector<u8>& ColorPixels() const { return m_colorPixels; }
        std::vector<u8>& ColorPixels() { return m_colorPixels; }

    private:
        template <typename Fn>
        f32 SampleDisc(const Vec2& mapPosition, f32 radius, Fn&& fn) const;

        u32 m_pixelWidth = 0;
        u32 m_pixelHeight = 0;
        u32 m_tileWidth = 0;
        u32 m_tileHeight = 0;
        u32 m_tilePixels = 4;

        std::vector<Tile> m_tiles;
        std::vector<u8> m_colorPixels;   // RGBA at full map resolution
    };
}
