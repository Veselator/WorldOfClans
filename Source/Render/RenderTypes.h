// RenderTypes.h - vertex and instance layouts shared between C++ and the shaders.
#pragma once

#include "../Core/Math.h"

#include <vector>

namespace woc
{
    /// Named slots inside WoCVisual.png. The concrete tile indices come from
    /// Config/game.json so the sheet can be re-authored without touching code.
    enum class SpriteId : u32
    {
        Human = 0,
        Elf,
        Dwarf,
        Cohort,
        Village,
        City,
        Castle,
        Circle,
        Forest,
        Quarry,
        Field,
        // Second row of the sheet: what a village and a city look like once they have
        // outgrown the hamlet they started as.
        VillageLarge,
        VillageGreat,
        CityLarge,
        CityGreat,
        Count
    };

    struct TerrainVertex
    {
        Vec3 position;
        Vec2 uv;
        Vec3 normal;
    };

    /// One instanced billboard.
    struct SpriteInstance
    {
        Vec3 worldPosition;
        Vec2 size;
        Vec4 uvRect;
        Color color;
        Vec4 params;   // x = vertical anchor (0 = stands on the point), y = depth bias, z = flash
    };

    struct UIVertex
    {
        Vec2 position;
        Vec2 uv;
        Color color;
    };

    /// Which sampler and blending rule the UI batch should use for a run of vertices.
    enum class UIDrawMode : u32
    {
        Solid = 0,
        Glyph = 1,
        Sprite = 2,
        /// The minimap's own picture: a full RGBA image with no cut-out, drawn from a
        /// texture the scene rebuilds rather than from the shared atlas.
        Minimap = 3,
        /// An arbitrary picture a scene has handed the renderer - a map's baked portrait,
        /// say. Drawn exactly like the minimap; the range carries which one.
        Image = 4
    };
}
