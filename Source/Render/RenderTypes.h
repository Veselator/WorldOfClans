// RenderTypes.h - vertex and instance layouts shared between C++ and the shaders.
#pragma once

#include "../Core/Math.h"

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
        Sprite = 2
    };
}
