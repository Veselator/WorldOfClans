// GameSceneMinimap.cpp - the little picture of the world in the bottom-right corner.
//
// The minimap is not a second renderer, and it is deliberately cheap:
//
//   * the land comes from the map's baked portrait - a small RGBA picture written into the
//     map's folder when the map was saved. Nothing at run time ever looks at the eight
//     megabytes of colour layer to draw a thumbnail;
//   * the colouring over it comes from CoverageSystem's finished layer and its palette, so
//     the minimap shows whichever map mode the player has chosen without ever asking what
//     a mode is;
//   * what may be drawn at all comes from FogSystem, and so does the extent - with the fog
//     up the picture covers only the country the realm has walked, and grows as it does.
//
// The composite is rebuilt only when one of those three things actually changed, which in
// practice means a few times a second at most and not at all while the player sits still.
// Over it go the things that move: towns, armies, and the camera's own footprint, drawn as
// the quadrilateral the screen corners project to - so it shows both where the player is
// looking and which way the map has been spun.
#include "GameScene.h"

#include "../Core/Config.h"
#include "../Game/Map/MapLoader.h"
#include "../Game/Systems/CoverageSystem.h"
#include "../Game/Systems/FogSystem.h"
#include "../Game/World/World.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>
#include <cmath>

namespace woc
{
    namespace
    {
        /// The colour of country nobody has ever crossed, and of everything off the map.
        Color VoidColour()
        {
            const u32 packed = static_cast<u32>(
                std::strtoul(ConfigManager::Get().Str("render/fog/color", "141b26").c_str(), nullptr, 16));
            return Color::FromRGB(packed);
        }
    }

    Rect GameScene::MinimapRect() const
    {
        const Vec2 viewport = m_renderer.ViewportSize();
        const f32 width = m_theme.sidebarWidth;
        const f32 height = std::floor(width * ConfigManager::Get().Float("render/minimap/aspect", 0.5625f));
        return { viewport.x - width, viewport.y - height, width, height };
    }

    void GameScene::LoadMinimapBase()
    {
        // Saved maps carry their portrait; one that predates portraits gets one baked and
        // written now - from the map already in memory, rather than by reading it again.
        if (!MapLoader::LoadMinimap(m_mapFolder, m_minimapBase))
        {
            MapLoader::SaveMinimap(m_mapFolder, m_world.Map());
            if (!MapLoader::LoadMinimap(m_mapFolder, m_minimapBase))
            {
                m_minimapBase = m_world.Map().BuildMinimapImage(
                    static_cast<u32>(ConfigManager::Get().Int("map/minimapWidth", 384)));
            }
        }
        m_minimapDrawn = 0;   // force the first composite
    }

    void GameScene::UpdateMinimap(f32 deltaTime)
    {
        m_minimapTimer -= deltaTime;
        if (m_minimapTimer > 0.0f) return;
        m_minimapTimer = ConfigManager::Get().Float("render/minimap/refresh", 0.35f);

        // One number stands for everything the picture is made of. While it holds still -
        // no marching, no borders moving, no change of map mode - there is nothing to redraw
        // and the minimap costs exactly nothing.
        const u64 signature = FogSystem::Get().Revision() * 1000003ull +
                              CoverageSystem::Get().LayerRevision() * 1009ull +
                              static_cast<u64>(CoverageSystem::Get().Mode()) + 1ull;
        if (signature == m_minimapDrawn) return;

        m_minimapDrawn = signature;
        RebuildMinimap();
    }

    void GameScene::RebuildMinimap()
    {
        const MapData& map = m_world.Map();
        if (!map.IsValid() || !m_minimapBase.IsValid()) return;

        FogSystem& fog = FogSystem::Get();
        const bool fogOn = fog.IsEnabled();
        const std::vector<u8>& states = fog.States();

        const f32 tileSize = static_cast<f32>(map.TilePixels());
        const f32 mapW = static_cast<f32>(map.PixelWidth());
        const f32 mapH = static_cast<f32>(map.PixelHeight());

        // --- how much of the world the picture covers ---------------------------------------
        Vec2 min{ 0.0f, 0.0f };
        Vec2 max{ mapW, mapH };

        if (fogOn && fog.ExploredMax().x >= fog.ExploredMin().x)
        {
            // Only as far as the realm has actually looked. A player two hours in should not
            // be reading his kingdom out of one corner of a mostly empty rectangle.
            const f32 pad = ConfigManager::Get().Float("render/minimap/padTiles", 5.0f) * tileSize;
            const Coord lo = fog.ExploredMin();
            const Coord hi = fog.ExploredMax();
            min = { static_cast<f32>(lo.x) * tileSize - pad, static_cast<f32>(lo.y) * tileSize - pad };
            max = { static_cast<f32>(hi.x + 1) * tileSize + pad, static_cast<f32>(hi.y + 1) * tileSize + pad };
        }

        // A minimum extent, or the very first frames of a party show four tiles at the size
        // of the whole widget. It never exceeds the map itself: a small world should fill
        // the picture, not sit in the middle of a field of nothing.
        const f32 floorSize = std::min(ConfigManager::Get().Float("render/minimap/minExtent", 2200.0f),
                                       std::min(mapW, mapH));
        const Vec2 centre{ (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
        Vec2 half{ std::max((max.x - min.x) * 0.5f, floorSize * 0.5f),
                   std::max((max.y - min.y) * 0.5f, floorSize * 0.5f) };

        // Stretched to the widget's own shape so nothing is squashed. Growing the short axis
        // rather than cropping the long one keeps every explored tile on the picture.
        const Rect widget = MinimapRect();
        const f32 want = widget.h > 0.0f ? widget.w / widget.h : 1.0f;
        const f32 have = half.y > 0.0f ? half.x / half.y : 1.0f;
        if (have < want) half.x = half.y * want;
        else              half.y = half.x / want;

        m_minimapRegion = { centre.x - half.x, centre.y - half.y, half.x * 2.0f, half.y * 2.0f };

        // --- the picture itself ---------------------------------------------------------------
        const u32 texW = static_cast<u32>(std::clamp(ConfigManager::Get().Int("render/minimap/resolution", 200), 32, 512));
        const u32 texH = std::max(1u, static_cast<u32>(std::lround(texW / want)));

        const std::vector<u8>& layer = CoverageSystem::Get().Layer();
        const std::vector<Color>& palette = CoverageSystem::Get().LayerPalette();
        const f32 tint = ConfigManager::Get().Float("render/minimap/tint", 0.62f);
        const f32 dim = ConfigManager::Get().Float("render/minimap/rememberedDim", 0.52f);

        const Color blank = VoidColour();
        const u8 blankR = static_cast<u8>(blank.r * 255.0f);
        const u8 blankG = static_cast<u8>(blank.g * 255.0f);
        const u8 blankB = static_cast<u8>(blank.b * 255.0f);

        m_minimapPixels.assign(static_cast<size_t>(texW) * texH * 4, 0);

        for (u32 py = 0; py < texH; ++py)
        {
            const f32 mapY = m_minimapRegion.y + (static_cast<f32>(py) + 0.5f) / texH * m_minimapRegion.h;
            for (u32 px = 0; px < texW; ++px)
            {
                const f32 mapX = m_minimapRegion.x + (static_cast<f32>(px) + 0.5f) / texW * m_minimapRegion.w;
                u8* out = &m_minimapPixels[(static_cast<size_t>(py) * texW + px) * 4];
                out[3] = 255;

                const bool offMap = mapX < 0.0f || mapY < 0.0f || mapX >= mapW || mapY >= mapH;
                u8 state = static_cast<u8>(FogState::Visible);
                size_t tileIndex = 0;
                if (!offMap)
                {
                    const u32 tx = std::min(static_cast<u32>(mapX / tileSize), map.TileWidth() - 1);
                    const u32 ty = std::min(static_cast<u32>(mapY / tileSize), map.TileHeight() - 1);
                    tileIndex = static_cast<size_t>(ty) * map.TileWidth() + tx;
                    if (fogOn && tileIndex < states.size()) state = states[tileIndex];
                }

                if (offMap || state == static_cast<u8>(FogState::Unseen))
                {
                    out[0] = blankR; out[1] = blankG; out[2] = blankB;
                    continue;
                }

                // The land, straight out of the baked portrait.
                const u32 bx = std::min(m_minimapBase.width - 1,
                                        static_cast<u32>(mapX / mapW * m_minimapBase.width));
                const u32 by = std::min(m_minimapBase.height - 1,
                                        static_cast<u32>(mapY / mapH * m_minimapBase.height));
                const u8* base = m_minimapBase.At(bx, by);
                f32 r = base[0] / 255.0f;
                f32 g = base[1] / 255.0f;
                f32 b = base[2] / 255.0f;

                // The thematic layer, straight off the same mask the terrain shader reads.
                if (tileIndex < layer.size())
                {
                    const u8 slot = layer[tileIndex];
                    if (slot > 0 && slot < palette.size() && palette[slot].a > 0.01f)
                    {
                        const f32 k = tint * palette[slot].a;
                        r += (palette[slot].r - r) * k;
                        g += (palette[slot].g - g) * k;
                        b += (palette[slot].b - b) * k;
                    }
                }

                // Country you walked once and are not watching keeps its shape, loses its light.
                if (state == static_cast<u8>(FogState::Explored))
                {
                    r *= dim; g *= dim; b *= dim;
                }

                out[0] = static_cast<u8>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
                out[1] = static_cast<u8>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
                out[2] = static_cast<u8>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
            }
        }

        m_renderer.SetMinimapImage(m_minimapPixels, texW, texH);
    }

    void GameScene::DrawMinimap()
    {
        const MapData& map = m_world.Map();
        if (!map.IsValid() || m_minimapRegion.w <= 0.0f || m_minimapRegion.h <= 0.0f) return;

        const Rect widget = MinimapRect();
        m_ui.Panel(widget);

        const Rect inner = widget.Inset(4.0f);
        m_renderer.UIMinimap(inner, Color(1.0f, 1.0f, 1.0f, 1.0f));

        auto toWidget = [&](const Vec2& mapPosition)
        {
            return Vec2{ inner.x + (mapPosition.x - m_minimapRegion.x) / m_minimapRegion.w * inner.w,
                         inner.y + (mapPosition.y - m_minimapRegion.y) / m_minimapRegion.h * inner.h };
        };

        m_renderer.PushClip(inner);

        FogSystem& fog = FogSystem::Get();
        const bool fogOn = fog.IsEnabled();

        // --- towns ------------------------------------------------------------------------
        // Under fog these are the ones the player remembers, banner and all, which is the
        // same list the map itself draws in explored country.
        const f32 seatSize = ConfigManager::Get().Float("render/minimap/seatSize", 4.0f);
        auto seat = [&](const Vec2& position, const Color& color)
        {
            const Vec2 p = toWidget(position);
            m_renderer.UIRect({ p.x - seatSize * 0.5f - 1.0f, p.y - seatSize * 0.5f - 1.0f,
                                seatSize + 2.0f, seatSize + 2.0f }, m_theme.shadow.WithAlpha(0.7f));
            m_renderer.UIRect({ p.x - seatSize * 0.5f, p.y - seatSize * 0.5f, seatSize, seatSize }, color);
        };

        if (fogOn)
        {
            for (const FogSystem::SeenSettlement& seen : fog.Remembered()) seat(seen.position, seen.color);
        }
        else
        {
            for (const auto& entry : m_world.Settlements()) seat(entry.second.position, ClanColor(entry.second.owner));
        }

        // --- armies -----------------------------------------------------------------------
        // Only the ones somebody of yours can see: you remember where a town stood, never
        // where a column happens to be standing today.
        const f32 armySize = ConfigManager::Get().Float("render/minimap/armySize", 3.0f);
        for (const auto& entry : m_world.Cohorts())
        {
            const Cohort& cohort = entry.second;
            if (fogOn && !fog.IsVisible(m_world, cohort.position)) continue;

            const Vec2 p = toWidget(cohort.position);
            const bool selected = IsSelected(cohort.id);
            const f32 size = selected ? armySize + 2.0f : armySize;
            m_renderer.UIRect({ p.x - size * 0.5f - 1.0f, p.y - size * 0.5f - 1.0f, size + 2.0f, size + 2.0f },
                              selected ? m_theme.accent : m_theme.shadow.WithAlpha(0.75f));
            m_renderer.UIRect({ p.x - size * 0.5f, p.y - size * 0.5f, size, size }, ClanColor(cohort.clan));
        }

        // --- where the player is looking -----------------------------------------------------
        // The four screen corners projected onto the ground. Drawn as a quadrilateral rather
        // than a rectangle, so the shape itself tells the player how far the map is turned.
        const Camera& camera = m_renderer.GetCamera();
        const Vec2 viewport = m_renderer.ViewportSize();
        const Vec2 screenCorners[4] = {
            { 0.0f, 0.0f }, { viewport.x, 0.0f }, { viewport.x, viewport.y }, { 0.0f, viewport.y }
        };
        Vec2 frustum[4];
        for (int i = 0; i < 4; ++i) frustum[i] = toWidget(camera.ScreenToMap(screenCorners[i], 0.0f));
        for (int i = 0; i < 4; ++i)
        {
            m_renderer.UILine(frustum[i], frustum[(i + 1) % 4], m_theme.textStrong.WithAlpha(0.85f), 1.4f);
        }

        // And the heading, as a stub struck from the centre towards the top of the screen.
        const Vec2 eye = toWidget(camera.Focus());
        const Vec2 ahead = toWidget(camera.ScreenToMap({ viewport.x * 0.5f, 0.0f }, 0.0f));
        m_renderer.UILine(eye, { eye.x + (ahead.x - eye.x) * 0.45f, eye.y + (ahead.y - eye.y) * 0.45f },
                          m_theme.accent, 1.6f);
        m_renderer.UIRect({ eye.x - 1.5f, eye.y - 1.5f, 3.0f, 3.0f }, m_theme.accent);

        m_renderer.PopClip();

        // The minimap eats its own clicks, or dragging a selection box across it would give
        // orders on the ground underneath. A click recentres, which is what a map is for.
        if (m_ui.InvisibleButton(widget, "game.minimap"))
        {
            const Vec2 mouse = m_input.MousePosition();
            const Vec2 target{
                m_minimapRegion.x + (mouse.x - inner.x) / inner.w * m_minimapRegion.w,
                m_minimapRegion.y + (mouse.y - inner.y) / inner.h * m_minimapRegion.h
            };
            Camera& mutableCamera = m_renderer.GetCamera();
            mutableCamera.SetFocus(target);
            mutableCamera.ClampToBounds();
        }
        m_ui.BlockMouse(widget);
    }
}
