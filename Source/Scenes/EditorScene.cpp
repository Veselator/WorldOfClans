#include "EditorScene.h"
#include "SceneManager.h"

#include "../Audio/AudioSystem.h"
#include "../Core/Config.h"
#include "../Game/Systems/RoadSystem.h"
#include "../Core/Settings.h"
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
        Renderer::Get().SetBordersVisible(true);   // the title screen leaves them off
        Renderer::Get().SetFogEnabled(false);       // the designer sees the whole map
        AudioSystem::Get().SetMood(MusicMood::Menu);
        Renderer::Get().SetClearColor(Color::FromRGB(0x05121a));

        NamePool::Get().Load();
        World::Get().Reset();

        m_maps = MapLoader::ListMaps();
        LoadThumbnails();

        // The editor no longer decides for the designer which map he meant: it opens on the
        // shelf and waits. Something has to be in the world meanwhile, so the first map is
        // loaded behind the window - or bare terrain if there is nothing at all.
        if (!m_maps.empty()) LoadMap(m_maps.front().folder);
        else GenerateTerrain();

        m_browserOpen = true;
        m_browserScroll = 0.0f;
    }

    void EditorScene::ResizeMap()
    {
        World& world = World::Get();
        MapData& map = world.MutableMap();

        const u8 water = TerrainDatabase::Get().IndexOf("water");
        map.Resize(static_cast<u32>(std::max(256, m_genWidth)),
                   static_cast<u32>(std::max(256, m_genHeight)), water);

        m_description.width = map.PixelWidth();
        m_description.height = map.PixelHeight();
        m_genWidth = static_cast<i32>(map.PixelWidth());
        m_genHeight = static_cast<i32>(map.PixelHeight());

        map.RebuildElevation(ConfigManager::Get().Int("render/terrainSmoothPasses", 4));
        map.ComputeFordableWater(ConfigManager::Get().Int("map/fordableWaterRadius", 3));
        RebuildColorLayer();
        PushToRenderer(true);

        m_message = "Розмір карти змінено";
        m_messageTimer = 3.0f;
    }

    bool EditorScene::CreateMap()
    {
        if (m_newFolder.empty()) return false;

        m_mapName = m_newName;
        m_folderName = m_newFolder;
        m_genWidth = std::max(256, m_newWidth);
        m_genHeight = std::max(256, m_newHeight);

        GenerateTerrain();
        m_description.name = m_mapName;
        m_browserOpen = false;
        return true;
    }

    void EditorScene::OnExit()
    {
        ReleaseThumbnails();
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

        // A random seed is drawn here rather than typed, and then written back into the
        // field: whatever the generator used is what the designer sees, and can keep.
        if (m_randomSeed)
        {
            m_genSeed = GlobalRandom().Range(1, 2000000000);
            m_seedText = std::to_string(m_genSeed);
        }

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
        renderer.SetShoreMask(map.BuildShoreMask(ConfigManager::Get().Int("render/water/foamReach", 3)),
                               map.TileWidth(), map.TileHeight());

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
        LoadThumbnails();
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
                        // Painting a terrain type paints a terrain type, and nothing else.
                        // It used to drag the height up with it, which meant a designer
                        // could not lay out a coastline without reshaping the ground.
                        tile.terrain = static_cast<u8>(m_terrainIndex);
                        const TerrainInfo& info = TerrainDatabase::Get().At(tile.terrain);

                        // Nothing here touches tile.height: that belongs to the height brush.
                        // Water still clears what cannot grow in it, and needs the elevation
                        // pass so the shoreline is re-flattened, but the painted height is
                        // kept so land repainted here later comes back at its old level.
                        if (info.water) { tile.forest = 0.0f; tile.field = 0.0f; }
                        m_dirtyColor = true;
                        m_dirtyHeight = true;
                    }
                    break;

                case EditorTool::Height:
                {
                    if (TerrainDatabase::Get().At(tile.terrain).water) break;

                    if (m_heightMode == HeightMode::Level)
                    {
                        // Towards the chosen height at the brush's strength, so a plateau
                        // can be laid in a few passes rather than snapped in one.
                        tile.height += (m_heightTarget - tile.height) * Clamp01(amount);
                    }
                    else
                    {
                        tile.height = Clamp01(tile.height + (erase ? -amount : amount) * 0.25f);
                    }
                    m_dirtyHeight = true;
                    break;
                }

                case EditorTool::Forest:
                    tile.forest = Clamp01(tile.forest + (erase ? -amount : amount));
                    break;

                case EditorTool::Field:
                    // Only the plain soils take a plough; the brush will not paint sand,
                    // hillside or highland however hard the designer scrubs at them.
                    if (TerrainDatabase::Get().At(tile.terrain).arable || erase)
                    {
                        tile.field = Clamp01(tile.field + (erase ? -amount : amount));
                    }
                    break;

                case EditorTool::Road:
                    if (falloff > 0.55f) tile.road = erase ? 0 : 1;
                    break;

                default:
                    break;
                }
            }
        }

        // Smoothing the elevation and re-running the ford search are whole-map passes.
        // Doing them inside the brush, every frame of a drag, is what made the editor
        // crawl; they now happen once when the stroke pauses. See FlushEdits.
        TouchRegion(center, span);
    }

    void EditorScene::TouchRegion(const Coord& center, i32 span)
    {
        const Coord low{ center.x - span - 1, center.y - span - 1 };
        const Coord high{ center.x + span + 1, center.y + span + 1 };

        if (m_dirtyMax.x < m_dirtyMin.x)
        {
            m_dirtyMin = low;
            m_dirtyMax = high;
            return;
        }
        m_dirtyMin = { std::min(m_dirtyMin.x, low.x), std::min(m_dirtyMin.y, low.y) };
        m_dirtyMax = { std::max(m_dirtyMax.x, high.x), std::max(m_dirtyMax.y, high.y) };
    }

    void EditorScene::FlushEdits()
    {
        World& world = World::Get();
        MapData& map = world.MutableMap();
        Renderer& renderer = Renderer::Get();

        if (m_dirtyHeight)
        {
            map.RebuildElevation(ConfigManager::Get().Int("render/terrainSmoothPasses", 4));
            map.ComputeFordableWater(ConfigManager::Get().Int("map/fordableWaterRadius", 3));
            m_dirtyHeight = false;
        }

        // Only the touched rectangle of the colour layer is re-rasterised. The full layer is
        // 8 MB for a 1920x1080 map, and rewriting it for every brush stroke was pure waste.
        if (m_dirtyColor && m_dirtyMax.x >= m_dirtyMin.x)
        {
            const i32 tilePixels = static_cast<i32>(map.TilePixels());
            const i32 x0 = std::max(0, m_dirtyMin.x * tilePixels);
            const i32 y0 = std::max(0, m_dirtyMin.y * tilePixels);
            const i32 x1 = std::min(static_cast<i32>(map.PixelWidth()), (m_dirtyMax.x + 1) * tilePixels);
            const i32 y1 = std::min(static_cast<i32>(map.PixelHeight()), (m_dirtyMax.y + 1) * tilePixels);

            std::vector<u8>& pixels = map.ColorPixels();
            for (i32 y = y0; y < y1; ++y)
            {
                for (i32 x = x0; x < x1; ++x)
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
            renderer.SetTerrainColor(map.ColorPixels(), map.PixelWidth(), map.PixelHeight());
            m_dirtyColor = false;
        }

        renderer.SetTreeMask(map.BuildForestMask(), map.TileWidth(), map.TileHeight());
        renderer.SetFieldMask(map.BuildFieldMask(), map.TileWidth(), map.TileHeight());
        renderer.SetShoreMask(map.BuildShoreMask(ConfigManager::Get().Int("render/water/foamReach", 3)),
                               map.TileWidth(), map.TileHeight());
        RoadSystem::Get().MarkDirty();
        RoadSystem::Get().UploadLayer(world);

        const u32 step = static_cast<u32>(ConfigManager::Get().Int("render/terrainMeshStep", 8));
        renderer.SetTerrainMesh(MapLoader::BuildMesh(map, step));

        m_dirtyMin = { 0, 0 };
        m_dirtyMax = { -1, -1 };
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

        // The rock is under the hills and the highland; nobody sinks a quarry into a meadow.
        const TerrainInfo& ground = world.Map().TerrainAtMap(mapPosition);
        if (!ground.mineable || !ground.passable)
        {
            m_message = "Шахту можна закласти лише на пагорбах або в нагір'ї";
            m_messageTimer = 3.0f;
            return;
        }

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

        // Panning is screen-relative, as in the game: W always means "up the screen",
        // however far the map has been spun. Moving the focus along the raw map axes was
        // what made the editor feel as though it were dragging sideways.
        const f32 panSpeed = config.Float("camera/panSpeed", 900.0f) / camera.Zoom();
        Vec2 pan;
        if (input.IsKeyDown(Key::A) || input.IsKeyDown(Key::Left)) pan.x -= 1.0f;
        if (input.IsKeyDown(Key::D) || input.IsKeyDown(Key::Right)) pan.x += 1.0f;
        if (input.IsKeyDown(Key::W) || input.IsKeyDown(Key::Up)) pan.y -= 1.0f;
        if (input.IsKeyDown(Key::S) || input.IsKeyDown(Key::Down)) pan.y += 1.0f;

        if (pan.LengthSq() > 0.0f)
        {
            camera.PanScreenRelative(pan.Normalized() * (panSpeed * deltaTime));
            camera.ClampToBounds();
        }

        // The cursor near an edge nudges the view, exactly as it does in a party.
        const f32 margin = Settings::Get().edgeScroll;
        if (margin > 0.0f && !ui.WantsMouse())
        {
            const Vec2 mouse = input.MousePosition();
            Vec2 edge;
            if (mouse.x < margin) edge.x -= 1.0f;
            if (mouse.x > viewport.x - margin) edge.x += 1.0f;
            if (mouse.y < margin) edge.y -= 1.0f;
            if (mouse.y > viewport.y - margin) edge.y += 1.0f;
            if (edge.LengthSq() > 0.0f)
            {
                camera.PanScreenRelative(edge.Normalized() * (panSpeed * deltaTime));
                camera.ClampToBounds();
            }
        }

        const f32 rotateSpeed = Settings::Get().rotateSpeed;
        if (input.IsKeyDown(Key::Q)) camera.RotateBy(-rotateSpeed * deltaTime);
        if (input.IsKeyDown(Key::E)) camera.RotateBy(rotateSpeed * deltaTime);
        if (input.WasKeyPressed(Key::R)) camera.ResetRotation();

        const f32 wheel = input.WheelDelta();
        if (wheel != 0.0f && !ui.WantsMouse())
        {
            const f32 step = config.Float("camera/zoomStep", 1.12f);
            camera.ZoomAt(wheel > 0.0f ? step : 1.0f / step, input.MousePosition());
        }
    }

    Vec2 EditorScene::ScreenToTerrain(const Vec2& screenPoint) const
    {
        // The brush must land where the cursor points on the *ground*, not where the ray
        // crosses the water datum: at this camera angle a hill of any size throws the two
        // apart by tens of map units, and the paint went in below the highlight.
        const Camera& camera = Renderer::Get().GetCamera();
        const MapData& map = World::Get().Map();

        Vec2 position = camera.ScreenToMap(screenPoint, 0.0f);
        for (int i = 0; i < 2; ++i)
        {
            position = camera.ScreenToMap(screenPoint, map.WorldHeightAtMap(position));
        }
        return position;
    }

    void EditorScene::Update(f32 deltaTime)
    {
        if (m_messageTimer > 0.0f) m_messageTimer -= deltaTime;
        UpdateCamera(deltaTime);

        Input& input = Input::Get();
        UI& ui = UI::Get();
        if (ui.WantsMouse()) return;

        const Vec2 mapPosition = ScreenToTerrain(input.MousePosition());

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

        case EditorTool::Height:
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

        // Every heavy pass - smoothing, the ford search, the colour layer, the mesh - waits
        // for the stroke to pause. Painting stays at frame rate; the catch-up costs one hitch
        // a quarter second after the brush lifts.
        if (m_meshRefreshTimer > 0.0f)
        {
            m_meshRefreshTimer -= deltaTime;
            if (m_meshRefreshTimer <= 0.0f) FlushEdits();
        }

        if (CoverageSystem::Get().IsDirty())
        {
            CoverageSystem::Get().RecomputeBlocking(World::Get());
        }

        if (input.WasKeyPressed(Key::Escape) && !UI::Get().WantsKeyboard())
        {
            // One step at a time, same as in the game. Escape never throws away an unsaved
            // map: the way out of the editor is the button in the browser.
            if (m_browserOpen)
            {
                // Nothing open behind it means there is nowhere to go back to.
                if (World::Get().Map().IsValid()) m_browserOpen = false;
                else SceneManager::Get().RequestBack();
            }
            else if (m_inspected != kInvalidId)
            {
                m_inspected = kInvalidId;
            }
            else
            {
                m_browserOpen = true;
                m_browserScroll = 0.0f;
            }
        }
    }

    void EditorScene::DrawMapBounds()
    {
        Renderer& renderer = Renderer::Get();
        const Camera& camera = renderer.GetCamera();
        const MapData& map = World::Get().Map();
        if (!map.IsValid()) return;

        // The edge of the world, drawn as a dashed white line that stands on the ground it
        // crosses rather than floating over it: the designer needs to see where the map
        // stops even where the coast does not.
        const f32 w = static_cast<f32>(map.PixelWidth());
        const f32 h = static_cast<f32>(map.PixelHeight());
        const Vec2 corners[4] = { { 0.0f, 0.0f }, { w, 0.0f }, { w, h }, { 0.0f, h } };

        const f32 step = ConfigManager::Get().Float("editor/boundsDashStep", 48.0f);
        const f32 thickness = ConfigManager::Get().Float("editor/boundsThickness", 1.6f);

        for (int edge = 0; edge < 4; ++edge)
        {
            const Vec2 from = corners[edge];
            const Vec2 to = corners[(edge + 1) % 4];
            const f32 length = Distance(from, to);
            const i32 segments = std::max(2, static_cast<i32>(length / step));

            // Every other segment is drawn: that is what makes it a dashed line, and the
            // gaps are what let the coastline underneath still read.
            for (i32 i = 0; i < segments; i += 2)
            {
                const f32 t0 = static_cast<f32>(i) / static_cast<f32>(segments);
                const f32 t1 = static_cast<f32>(i + 1) / static_cast<f32>(segments);

                const Vec2 a{ from.x + (to.x - from.x) * t0, from.y + (to.y - from.y) * t0 };
                const Vec2 b{ from.x + (to.x - from.x) * t1, from.y + (to.y - from.y) * t1 };

                renderer.UILine(camera.MapToScreen(a, map.WorldHeightAtMap(a)),
                                camera.MapToScreen(b, map.WorldHeightAtMap(b)),
                                Color(1.0f, 1.0f, 1.0f, 0.85f), thickness);
            }
        }
    }

    void EditorScene::LoadThumbnails()
    {
        ReleaseThumbnails();
        m_thumbnails.assign(m_maps.size(), 0);

        for (size_t i = 0; i < m_maps.size(); ++i)
        {
            ImageData portrait;
            if (!MapLoader::EnsureMinimap(m_maps[i].folder, portrait)) continue;
            m_thumbnails[i] = Renderer::Get().CreateUITexture(portrait.pixels, portrait.width, portrait.height);
        }
    }

    void EditorScene::ReleaseThumbnails()
    {
        for (u32 handle : m_thumbnails) Renderer::Get().ReleaseUITexture(handle);
        m_thumbnails.clear();
    }

    void EditorScene::DrawMapBrowser()
    {
        if (!m_browserOpen) return;

        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        const Vec2 viewport = renderer.ViewportSize();

        renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, theme.shadow.WithAlpha(0.72f));

        const f32 width = 620.0f;
        const f32 height = 560.0f;
        const Rect panel{ (viewport.x - width) * 0.5f, (viewport.y - height) * 0.5f, width, height };
        ui.Panel(panel, "Карти");

        const f32 innerX = panel.x + theme.padding * 2.0f;
        const f32 innerW = panel.w - theme.padding * 4.0f;
        f32 y = panel.y + theme.headerHeight + theme.padding;

        auto row = [&](f32 rowHeight)
        {
            const Rect r{ innerX, y, innerW, rowHeight };
            y += rowHeight + 5.0f;
            return r;
        };

        // --- what already exists ------------------------------------------------------------
        ui.Label(row(20.0f), "ВІДКРИТИ", theme.accent);

        const f32 listHeight = 210.0f;
        const Rect listArea{ innerX, y, innerW, listHeight };
        y += listHeight + 10.0f;

        const f32 entryHeight = 58.0f;
        const Rect content = ui.BeginScroll(listArea, m_maps.size() * entryHeight, m_browserScroll);
        if (m_maps.empty())
        {
            ui.LabelCentered({ content.x, content.y + 12.0f, content.w, 22.0f },
                             "Жодної карти ще немає", theme.textDim);
        }
        for (size_t i = 0; i < m_maps.size(); ++i)
        {
            const Rect r{ content.x, content.y + i * entryHeight, content.w, entryHeight - 4.0f };
            const bool current = m_maps[i].folder == m_folderName;
            if (ui.ListItem(r, "", current))
            {
                if (LoadMap(m_maps[i].folder)) m_browserOpen = false;
            }

            // The map's own portrait, kept in proportion inside a fixed frame.
            const Rect frame{ r.x + 5.0f, r.y + 4.0f, 78.0f, r.h - 8.0f };
            renderer.UIRect(frame, theme.panelAlt);
            const u32 thumbnail = i < m_thumbnails.size() ? m_thumbnails[i] : 0;
            if (thumbnail != 0 && m_maps[i].width > 0 && m_maps[i].height > 0)
            {
                const f32 want = static_cast<f32>(m_maps[i].width) / static_cast<f32>(m_maps[i].height);
                f32 w = frame.w;
                f32 h = w / want;
                if (h > frame.h) { h = frame.h; w = h * want; }
                renderer.UIImage(thumbnail,
                                 { frame.x + (frame.w - w) * 0.5f, frame.y + (frame.h - h) * 0.5f, w, h },
                                 Color(1.0f, 1.0f, 1.0f, 1.0f));
            }

            renderer.UIText(m_maps[i].name, { frame.Right() + 10.0f, r.y + 8.0f },
                            current ? theme.accent : theme.textStrong);

            char size[48];
            std::snprintf(size, sizeof(size), "%u x %u", m_maps[i].width, m_maps[i].height);
            renderer.UIText(size, { frame.Right() + 10.0f, r.y + 28.0f }, theme.textDim, 0.9f);
        }
        ui.EndScroll(content.y + m_maps.size() * entryHeight);

        // --- or something new ----------------------------------------------------------------
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 10.0f;
        ui.Label(row(20.0f), "НОВА КАРТА", theme.accent);

        {
            const Rect r = row(24.0f);
            ui.Label({ r.x, r.y, 110.0f, r.h }, "Назва", theme.textDim);
            ui.TextField({ r.x + 115.0f, r.y, r.w - 115.0f, r.h }, "newMapName", m_newName, 40);
        }
        {
            const Rect r = row(24.0f);
            ui.Label({ r.x, r.y, 110.0f, r.h }, "Тека", theme.textDim);
            ui.TextField({ r.x + 115.0f, r.y, r.w - 115.0f, r.h }, "newMapFolder", m_newFolder, 32);
        }
        {
            // A free size, not a menu of three: the map is whatever the designer needs.
            const Rect r = row(24.0f);
            const f32 halfWidth = (r.w - 12.0f) * 0.5f;
            ui.Stepper({ r.x, r.y, halfWidth, r.h }, "Ширина", m_newWidth, 256, 8192);
            ui.Stepper({ r.x + halfWidth + 12.0f, r.y, halfWidth, r.h }, "Висота", m_newHeight, 256, 8192);
        }
        {
            const Rect r = row(18.0f);
            char note[96];
            std::snprintf(note, sizeof(note), "Сітка симуляції: %d x %d клітин",
                          m_newWidth / 4, m_newHeight / 4);
            ui.Label(r, note, theme.textDim);
        }

        if (ui.Button(row(30.0f), "Створити карту")) CreateMap();

        // --- and the way out ------------------------------------------------------------------
        const f32 buttonY = panel.Bottom() - 44.0f;
        const f32 buttonWidth = (innerW - 12.0f) * 0.5f;
        if (ui.Button({ innerX, buttonY, buttonWidth, 30.0f }, "Продовжити редагування",
                      !m_maps.empty() || World::Get().Map().IsValid()))
        {
            m_browserOpen = false;
        }
        if (ui.Button({ innerX + buttonWidth + 12.0f, buttonY, buttonWidth, 30.0f }, "У меню"))
        {
            SceneManager::Get().RequestBack();
        }
    }

    void EditorScene::Render()
    {
        // The browser is modal: nothing behind it answers the mouse while it is up.
        if (m_browserOpen)
        {
            const Vec2 viewport = Renderer::Get().ViewportSize();
            const f32 width = 620.0f;
            const f32 height = 560.0f;
            UI::Get().SetModalRegion({ (viewport.x - width) * 0.5f, (viewport.y - height) * 0.5f,
                                       width, height });
        }

        DrawObjects();
        DrawMapBounds();
        DrawToolbar();
        DrawInspector();
        DrawMapBrowser();

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
            const Vec2 mapPosition = ScreenToTerrain(Input::Get().MousePosition());
            renderer.DrawSprite(SpriteId::Circle, mapPosition, map.WorldHeightAtMap(mapPosition),
                                static_cast<f32>(m_brushRadius) * 2.0f,
                                Theme::Get().accent.WithAlpha(0.25f), 0.5f);
        }
    }
}
