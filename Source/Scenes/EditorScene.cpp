#include "EditorScene.h"
#include "SceneManager.h"

#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"
#include "../Core/Random.h"
#include "../Game/Factories/NamePool.h"
#include "../Game/Factories/SettlementFactory.h"
#include "../Game/Systems/CoverageSystem.h"
#include "../Game/World/RaceDatabase.h"
#include "../Game/World/World.h"
#include "../Game/WorldGenerator.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>
#include <cstdio>

namespace woc
{
    namespace
    {
        /// Value noise with smooth interpolation; cheap, deterministic and good enough
        /// for a designer-facing height map they will then paint over anyway.
        f32 Hash2D(i32 x, i32 y, u32 seed)
        {
            u32 h = static_cast<u32>(x) * 374761393u + static_cast<u32>(y) * 668265263u + seed * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return static_cast<f32>((h ^ (h >> 16)) & 0xFFFFFF) / static_cast<f32>(0xFFFFFF);
        }

        f32 SmoothNoise(f32 x, f32 y, u32 seed)
        {
            const i32 x0 = static_cast<i32>(std::floor(x));
            const i32 y0 = static_cast<i32>(std::floor(y));
            const f32 fx = x - static_cast<f32>(x0);
            const f32 fy = y - static_cast<f32>(y0);

            const f32 ux = fx * fx * (3.0f - 2.0f * fx);
            const f32 uy = fy * fy * (3.0f - 2.0f * fy);

            const f32 a = Hash2D(x0, y0, seed);
            const f32 b = Hash2D(x0 + 1, y0, seed);
            const f32 c = Hash2D(x0, y0 + 1, seed);
            const f32 d = Hash2D(x0 + 1, y0 + 1, seed);

            return Lerp(Lerp(a, b, ux), Lerp(c, d, ux), uy);
        }

        f32 Fbm(f32 x, f32 y, u32 seed, int octaves)
        {
            f32 total = 0.0f;
            f32 amplitude = 1.0f;
            f32 frequency = 1.0f;
            f32 normalisation = 0.0f;
            for (int i = 0; i < octaves; ++i)
            {
                total += SmoothNoise(x * frequency, y * frequency, seed + static_cast<u32>(i) * 71u) * amplitude;
                normalisation += amplitude;
                amplitude *= 0.5f;
                frequency *= 2.0f;
            }
            return normalisation > 0.0f ? total / normalisation : 0.0f;
        }
    }

    void EditorScene::OnEnter()
    {
        Renderer::Get().SetTerrainEnabled(true);
        Renderer::Get().SetClearColor(Color::FromRGB(0x05121a));

        NamePool::Get().Load();
        World::Get().Reset();

        m_maps = MapLoader::ListMaps();
        if (!m_maps.empty())
        {
            LoadMap(m_maps.front().folder);
        }
        else
        {
            GenerateTerrain();
        }
    }

    void EditorScene::OnExit()
    {
        World::Get().Reset();
    }

    // =====================================================================================
    // Map handling
    // =====================================================================================

    bool EditorScene::LoadMap(const std::string& folder)
    {
        World& world = World::Get();
        world.Reset();

        if (!world.LoadMap(folder))
        {
            m_message = "Не вдалося відкрити карту";
            m_messageTimer = 4.0f;
            return false;
        }
        m_description = world.MapInfo();
        m_mapName = m_description.name;
        m_folderName = folder;
        m_genWidth = static_cast<i32>(m_description.width);
        m_genHeight = static_cast<i32>(m_description.height);

        WorldGenerator::LoadObjects(world, folder);
        PushToRenderer(true);

        m_message = "Відкрито карту: " + m_description.name;
        m_messageTimer = 3.0f;
        return true;
    }

    void EditorScene::GenerateTerrain()
    {
        World& world = World::Get();
        world.Reset();

        MapData& map = world.MutableMap();
        const u32 tilePixels = 4;
        map.Allocate(static_cast<u32>(m_genWidth), static_cast<u32>(m_genHeight), tilePixels);

        const TerrainDatabase& terrainDb = TerrainDatabase::Get();
        const u8 water = terrainDb.IndexOf("water");
        const u8 coast = terrainDb.IndexOf("coast");
        const u8 plainRich = terrainDb.IndexOf("plainRich");
        const u8 plain = terrainDb.IndexOf("plain");
        const u8 plainPoor = terrainDb.IndexOf("plainPoor");
        const u8 hills = terrainDb.IndexOf("hills");
        const u8 highland = terrainDb.IndexOf("highland");
        const u8 mountain = terrainDb.IndexOf("mountain");

        const u32 seed = static_cast<u32>(m_genSeed);
        const f32 tilesX = static_cast<f32>(map.TileWidth());
        const f32 tilesY = static_cast<f32>(map.TileHeight());

        for (u32 ty = 0; ty < map.TileHeight(); ++ty)
        {
            for (u32 tx = 0; tx < map.TileWidth(); ++tx)
            {
                const f32 u = static_cast<f32>(tx) / tilesX;
                const f32 v = static_cast<f32>(ty) / tilesY;

                // Radial falloff turns the noise field into an island rather than a slab.
                const f32 dx = (u - 0.5f) * 2.0f;
                const f32 dy = (v - 0.5f) * 2.0f;
                const f32 falloff = 1.0f - std::min(1.0f, std::sqrt(dx * dx * 0.75f + dy * dy * 1.15f));

                f32 elevation = Fbm(u * m_genScale, v * m_genScale, seed, 5);
                elevation = elevation * 0.65f + falloff * 0.55f;
                elevation = Clamp01(elevation);

                const f32 moisture = Fbm(u * m_genScale * 1.7f + 11.0f, v * m_genScale * 1.7f + 7.0f,
                                         seed + 991u, 4);

                Tile& tile = map.At({ static_cast<i32>(tx), static_cast<i32>(ty) });
                tile.height = elevation;
                tile.field = 0.0f;

                if (elevation < m_genSeaLevel)
                {
                    tile.terrain = water;
                    tile.height = 0.0f;
                    tile.forest = 0.0f;
                }
                else if (elevation < m_genSeaLevel + 0.035f)
                {
                    tile.terrain = coast;
                    tile.forest = 0.0f;
                }
                else if (elevation > m_genMountains + 0.13f)
                {
                    tile.terrain = mountain;
                    tile.forest = 0.0f;
                }
                else if (elevation > m_genMountains)
                {
                    tile.terrain = highland;
                    tile.forest = moisture > 0.72f ? 0.2f : 0.0f;
                }
                else if (elevation > m_genMountains - 0.12f)
                {
                    tile.terrain = hills;
                    tile.forest = moisture > 0.55f ? (moisture - 0.55f) * 1.4f : 0.0f;
                }
                else
                {
                    // Soil quality follows moisture: the wettest lowlands are the richest.
                    tile.terrain = moisture > 0.62f ? plainRich : (moisture > 0.4f ? plain : plainPoor);
                    const f32 forestNoise = Fbm(u * m_genScale * 2.6f + 31.0f, v * m_genScale * 2.6f + 17.0f,
                                                seed + 4211u, 4);
                    tile.forest = forestNoise > (1.0f - m_genForest)
                        ? Clamp01((forestNoise - (1.0f - m_genForest)) * 3.0f)
                        : 0.0f;
                }
            }
        }

        map.RebuildElevation(ConfigManager::Get().Int("render/terrainSmoothPasses", 4));
        map.ComputeFordableWater(ConfigManager::Get().Int("map/fordableWaterRadius", 3));

        m_description.name = m_mapName;
        m_description.width = static_cast<u32>(m_genWidth);
        m_description.height = static_cast<u32>(m_genHeight);
        m_description.tilePixels = tilePixels;
        m_description.seed = seed;

        RebuildColorLayer();
        PushToRenderer(true);

        m_message = "Ландшафт згенеровано";
        m_messageTimer = 3.0f;
    }

    void EditorScene::RebuildColorLayer()
    {
        MapData& map = World::Get().MutableMap();
        std::vector<u8>& pixels = map.ColorPixels();
        pixels.assign(static_cast<size_t>(map.PixelWidth()) * map.PixelHeight() * 4, 255);

        for (u32 y = 0; y < map.PixelHeight(); ++y)
        {
            for (u32 x = 0; x < map.PixelWidth(); ++x)
            {
                const Coord tile = map.ToTile({ static_cast<f32>(x), static_cast<f32>(y) });
                const u32 color = TerrainDatabase::Get().At(map.At(tile).terrain).color;
                u8* texel = pixels.data() + (static_cast<size_t>(y) * map.PixelWidth() + x) * 4;
                texel[0] = static_cast<u8>((color >> 16) & 0xFF);
                texel[1] = static_cast<u8>((color >> 8) & 0xFF);
                texel[2] = static_cast<u8>(color & 0xFF);
                texel[3] = 255;
            }
        }
        m_dirtyColor = false;
    }

    void EditorScene::PushToRenderer(bool rebuildMesh)
    {
        World& world = World::Get();
        const MapData& map = world.Map();
        Renderer& renderer = Renderer::Get();

        renderer.SetTerrainColor(map.ColorPixels(), map.PixelWidth(), map.PixelHeight());
        renderer.SetTreeMask(map.BuildForestMask(), map.TileWidth(), map.TileHeight());
        renderer.SetFieldMask(map.BuildFieldMask(), map.TileWidth(), map.TileHeight());
        renderer.SetOwnerMask(map.BuildOwnerMask(), map.TileWidth(), map.TileHeight());

        if (rebuildMesh)
        {
            const u32 step = static_cast<u32>(ConfigManager::Get().Int("render/terrainMeshStep", 8));
            renderer.SetTerrainMesh(MapLoader::BuildMesh(map, step));
            renderer.SetWaterPlane({ static_cast<f32>(map.PixelWidth()), static_cast<f32>(map.PixelHeight()) },
                                   ConfigManager::Get().Float("render/water/planeMargin", 8000.0f));

            Camera& camera = renderer.GetCamera();
            camera.SetBounds({ 0.0f, 0.0f },
                             { static_cast<f32>(map.PixelWidth()), static_cast<f32>(map.PixelHeight()) });
            camera.SetFocus({ map.PixelWidth() * 0.5f, map.PixelHeight() * 0.5f });
        }
    }

    bool EditorScene::SaveMap()
    {
        World& world = World::Get();
        if (m_folderName.empty()) return false;

        m_description.name = m_mapName;
        if (m_dirtyColor) RebuildColorLayer();

        if (!MapLoader::Save(m_folderName, world.Map(), m_description))
        {
            m_message = "Не вдалося зберегти карту";
            m_messageTimer = 4.0f;
            return false;
        }
        if (!WorldGenerator::SaveObjects(world, m_folderName))
        {
            m_message = "Карту збережено, а об'єкти — ні";
            m_messageTimer = 4.0f;
            return false;
        }

        m_maps = MapLoader::ListMaps();
        m_message = "Збережено в Maps/" + m_folderName;
        m_messageTimer = 4.0f;
        return true;
    }

    // =====================================================================================
    // Editing
    // =====================================================================================

    void EditorScene::ApplyBrush(const Vec2& mapPosition, bool erase)
    {
        MapData& map = World::Get().MutableMap();
        const Coord center = map.ToTile(mapPosition);
        const i32 span = std::max(1, m_brushRadius / static_cast<i32>(map.TilePixels()));
        const f32 spanF = static_cast<f32>(span);

        for (i32 dy = -span; dy <= span; ++dy)
        {
            for (i32 dx = -span; dx <= span; ++dx)
            {
                const f32 distance = std::sqrt(static_cast<f32>(dx * dx + dy * dy));
                if (distance > spanF) continue;

                const Coord probe{ center.x + dx, center.y + dy };
                if (!map.InBounds(probe)) continue;

                Tile& tile = map.At(probe);
                const f32 falloff = 1.0f - distance / spanF;
                const f32 amount = m_brushStrength * falloff;

                switch (m_tool)
                {
                case EditorTool::Terrain:
                    if (falloff > 0.35f)
                    {
                        tile.terrain = static_cast<u8>(m_terrainIndex);
                        const TerrainInfo& info = TerrainDatabase::Get().At(tile.terrain);
                        // Keep elevation plausible for the painted terrain.
                        tile.height = info.water ? 0.0f : std::max(tile.height, info.heightFactor * 0.8f);
                        if (info.water) { tile.forest = 0.0f; tile.field = 0.0f; }
                        m_dirtyColor = true;
                    }
                    break;

                case EditorTool::Forest:
                    tile.forest = Clamp01(tile.forest + (erase ? -amount : amount));
                    break;

                case EditorTool::Field:
                    tile.field = Clamp01(tile.field + (erase ? -amount : amount));
                    break;

                case EditorTool::Road:
                    if (falloff > 0.55f) tile.road = erase ? 0 : 1;
                    break;

                default:
                    break;
                }
            }
        }

        if (m_tool == EditorTool::Terrain)
        {
            map.RebuildElevation(ConfigManager::Get().Int("render/terrainSmoothPasses", 4));
            map.ComputeFordableWater(ConfigManager::Get().Int("map/fordableWaterRadius", 3));
        }
    }

    void EditorScene::PlaceSettlement(const Vec2& mapPosition)
    {
        World& world = World::Get();
        Random& random = GlobalRandom();

        if (!SettlementFactory::CanPlace(world, world.Map(), m_settlementKind, mapPosition))
        {
            m_message = "Тут будувати не можна";
            m_messageTimer = 3.0f;
            return;
        }

        const std::vector<RaceInfo>& races = RaceDatabase::Get().Races();
        const RaceInfo& race = races[static_cast<size_t>(
            std::clamp(m_raceIndex, 0, static_cast<i32>(races.size()) - 1))];

        // The editor keeps a pool of anonymous clans, one per owner slot, so the map can be
        // authored with borders before a party ever assigns real dynasties to them.
        EntityId owner = kInvalidId;
        if (m_ownerSlot > 0)
        {
            std::vector<EntityId> clanIds;
            for (const auto& [id, clan] : world.Clans()) clanIds.push_back(id);
            std::sort(clanIds.begin(), clanIds.end());

            while (static_cast<i32>(clanIds.size()) < m_ownerSlot)
            {
                State& state = world.CreateState();
                state.raceId = race.id;
                state.name = NamePool::Get().StateName(race.id, random);

                Clan& clan = world.CreateClan();
                clan.state = state.id;
                clan.raceId = race.id;
                clan.faithId = race.defaultFaith;
                clan.name = NamePool::Get().ClanName(race.id, random);

                const Json& colors = ConfigManager::Get().Game()["clanColors"];
                const std::string hex = colors[clanIds.size() % std::max<size_t>(1, colors.Size())]
                                            .AsString("c8452d");
                clan.color = Color::FromRGB(static_cast<u32>(std::strtoul(hex.c_str(), nullptr, 16)));
                state.color = clan.color;
                state.AddClan(clan.id);

                clanIds.push_back(clan.id);
            }
            owner = clanIds[static_cast<size_t>(m_ownerSlot - 1)];
        }

        SettlementRequest request;
        request.kind = m_settlementKind;
        request.position = mapPosition;
        request.raceId = race.id;
        request.faithId = race.defaultFaith;
        request.owner = owner;

        Settlement& settlement = SettlementFactory::Create(world, request, random);
        m_inspected = settlement.id;
        m_message = "Розміщено: " + settlement.name;
        m_messageTimer = 2.5f;
        CoverageSystem::Get().MarkDirty();
    }

    void EditorScene::PlaceMine(const Vec2& mapPosition)
    {
        World& world = World::Get();
        MineSite& mine = world.CreateMine();
        mine.position = mapPosition;
        mine.resource = ResourceType::Stone;
        mine.richness = std::max(0.4f, world.Map().TerrainAtMap(mapPosition).stone);
        m_message = "Розміщено шахту";
        m_messageTimer = 2.0f;
    }

    void EditorScene::RemoveAt(const Vec2& mapPosition)
    {
        World& world = World::Get();

        EntityId best = kInvalidId;
        f32 bestDistance = 30.0f * 30.0f;
        for (const auto& [id, settlement] : world.Settlements())
        {
            const f32 distance = DistanceSq(settlement.position, mapPosition);
            if (distance < bestDistance) { bestDistance = distance; best = id; }
        }
        if (best != kInvalidId)
        {
            world.DestroySettlement(best);
            if (m_inspected == best) m_inspected = kInvalidId;
            CoverageSystem::Get().MarkDirty();
            return;
        }

        std::vector<MineSite>& mines = world.Mines();
        const auto it = std::find_if(mines.begin(), mines.end(), [&](const MineSite& mine)
        {
            return DistanceSq(mine.position, mapPosition) < 30.0f * 30.0f;
        });
        if (it != mines.end()) mines.erase(it);
    }

    // =====================================================================================
    // Frame
    // =====================================================================================

    void EditorScene::UpdateCamera(f32 deltaTime)
    {
        ConfigManager& config = ConfigManager::Get();
        Camera& camera = Renderer::Get().GetCamera();
        Input& input = Input::Get();
        UI& ui = UI::Get();

        const Vec2 viewport = Renderer::Get().ViewportSize();
        camera.SetViewport(viewport.x, viewport.y);
        if (ui.WantsKeyboard()) return;

        const f32 panSpeed = config.Float("camera/panSpeed", 900.0f) / camera.Zoom();
        Vec2 pan;
        if (input.IsKeyDown(Key::A) || input.IsKeyDown(Key::Left)) pan.x -= 1.0f;
        if (input.IsKeyDown(Key::D) || input.IsKeyDown(Key::Right)) pan.x += 1.0f;
        if (input.IsKeyDown(Key::W) || input.IsKeyDown(Key::Up)) pan.y -= 1.0f;
        if (input.IsKeyDown(Key::S) || input.IsKeyDown(Key::Down)) pan.y += 1.0f;

        if (pan.LengthSq() > 0.0f)
        {
            camera.MoveFocus(pan.Normalized() * (panSpeed * deltaTime));
            camera.ClampToBounds();
        }

        const f32 wheel = input.WheelDelta();
        if (wheel != 0.0f && !ui.WantsMouse())
        {
            const f32 step = config.Float("camera/zoomStep", 1.12f);
            camera.ZoomAt(wheel > 0.0f ? step : 1.0f / step, input.MousePosition());
        }
    }

    void EditorScene::Update(f32 deltaTime)
    {
        if (m_messageTimer > 0.0f) m_messageTimer -= deltaTime;
        UpdateCamera(deltaTime);

        Input& input = Input::Get();
        UI& ui = UI::Get();
        if (ui.WantsMouse()) return;

        Camera& camera = Renderer::Get().GetCamera();
        const Vec2 mapPosition = camera.ScreenToMap(input.MousePosition());

        const bool painting = input.IsMouseDown(MouseButton::Left);
        const bool erasing = input.IsMouseDown(MouseButton::Right);

        switch (m_tool)
        {
        case EditorTool::Terrain:
        case EditorTool::Forest:
        case EditorTool::Field:
        case EditorTool::Road:
            if (painting || erasing)
            {
                ApplyBrush(mapPosition, erasing);
                m_meshRefreshTimer = 0.25f;
            }
            break;

        case EditorTool::Settlement:
            if (input.WasMousePressed(MouseButton::Left)) PlaceSettlement(mapPosition);
            if (input.WasMousePressed(MouseButton::Right)) RemoveAt(mapPosition);
            break;

        case EditorTool::Mine:
            if (input.WasMousePressed(MouseButton::Left)) PlaceMine(mapPosition);
            if (input.WasMousePressed(MouseButton::Right)) RemoveAt(mapPosition);
            break;

        case EditorTool::Erase:
            if (input.WasMousePressed(MouseButton::Left)) RemoveAt(mapPosition);
            break;

        case EditorTool::Inspect:
            if (input.WasMousePressed(MouseButton::Left))
            {
                m_inspected = kInvalidId;
                f32 best = 30.0f * 30.0f;
                for (const auto& [id, settlement] : World::Get().Settlements())
                {
                    const f32 distance = DistanceSq(settlement.position, mapPosition);
                    if (distance < best) { best = distance; m_inspected = id; }
                }
            }
            break;
        }

        // Repainting the whole colour layer every frame would stall; do it when the
        // brush pauses instead.
        if (m_meshRefreshTimer > 0.0f)
        {
            m_meshRefreshTimer -= deltaTime;
            if (m_meshRefreshTimer <= 0.0f)
            {
                if (m_dirtyColor) RebuildColorLayer();
                PushToRenderer(true);
            }
        }

        if (CoverageSystem::Get().IsDirty())
        {
            CoverageSystem::Get().Recompute(World::Get());
        }

        if (input.WasKeyPressed(Key::Escape)) SceneManager::Get().Request(SceneId::MainMenu);
    }

    void EditorScene::Render()
    {
        DrawObjects();
        DrawToolbar();
        DrawInspector();

        if (m_messageTimer > 0.0f && !m_message.empty())
        {
            Renderer& renderer = Renderer::Get();
            const Theme& theme = Theme::Get();
            const Vec2 viewport = renderer.ViewportSize();
            const f32 width = renderer.TextWidth(m_message) + 24.0f;
            const Rect box{ (viewport.x - width) * 0.5f, viewport.y - 60.0f, width, 26.0f };
            renderer.UIRect(box, theme.panel.WithAlpha(0.92f));
            renderer.UIRectOutline(box, theme.accent, 1.0f);
            renderer.UITextCentered(m_message, box, theme.textStrong);
        }
    }

    void EditorScene::DrawObjects()
    {
        World& world = World::Get();
        Renderer& renderer = Renderer::Get();
        const MapData& map = world.Map();
        ConfigManager& config = ConfigManager::Get();

        const f32 settlementSize = config.Float("render/spriteScale/settlement", 26.0f);
        for (const auto& [id, settlement] : world.Settlements())
        {
            const Clan* clan = world.FindClan(settlement.owner);
            renderer.DrawSprite(settlement.Sprite(), settlement.position,
                                map.WorldHeightAtMap(settlement.position), settlementSize,
                                clan ? clan->color : Color::FromRGB(0xB9C0C8), 0.5f);
            if (id == m_inspected)
            {
                renderer.DrawSprite(SpriteId::Circle, settlement.position,
                                    map.WorldHeightAtMap(settlement.position), 40.0f,
                                    Theme::Get().selection.WithAlpha(0.5f), 0.5f);
            }
        }

        const f32 quarrySize = config.Float("render/spriteScale/quarry", 18.0f);
        for (const MineSite& mine : world.Mines())
        {
            renderer.DrawSprite(SpriteId::Quarry, mine.position, map.WorldHeightAtMap(mine.position),
                                quarrySize, Color::FromRGB(0x9AA3AB), 0.5f);
        }

        // Brush outline follows the cursor so the radius is never a guess.
        if (!UI::Get().WantsMouse())
        {
            const Vec2 mapPosition = renderer.GetCamera().ScreenToMap(Input::Get().MousePosition());
            renderer.DrawSprite(SpriteId::Circle, mapPosition, map.WorldHeightAtMap(mapPosition),
                                static_cast<f32>(m_brushRadius) * 2.0f,
                                Theme::Get().accent.WithAlpha(0.25f), 0.5f);
        }
    }
}
