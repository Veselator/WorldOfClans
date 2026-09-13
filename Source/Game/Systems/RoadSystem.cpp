#include "RoadSystem.h"
#include "CoverageSystem.h"
#include "../Map/Pathfinder.h"
#include "../Map/TerrainTypes.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"
#include "../../Render/Camera.h"
#include "../../Render/Renderer.h"

#include <algorithm>
#include <cmath>

namespace woc
{
    namespace
    {
        /// Road grade 1 is a cleared, packed track. Higher grades are reserved for later.
        constexpr u8 kRoadGrade = 1;
    }

    f32 RoadSystem::MetresPerUnit() const
    {
        return ConfigManager::Get().Float("roads/metresPerUnit", 12.0f);
    }

    bool RoadSystem::AreConnected(const World& world, EntityId a, EntityId b) const
    {
        const Settlement* first = world.FindSettlement(a);
        const Settlement* second = world.FindSettlement(b);
        if (!first || !second) return false;

        const f32 tolerance = static_cast<f32>(world.Map().TilePixels()) * 2.5f;
        auto joins = [&](const Vec2& head, const Vec2& tail)
        {
            return (Distance(head, first->position) < tolerance && Distance(tail, second->position) < tolerance) ||
                   (Distance(head, second->position) < tolerance && Distance(tail, first->position) < tolerance);
        };

        for (const RoadSegment& segment : world.Roads())
        {
            if (segment.points.size() < 2) continue;
            if (joins(segment.points.front(), segment.points.back())) return true;
        }
        for (const RoadProject& project : m_projects)
        {
            if (project.tiles.size() < 2) continue;
            if (joins(world.Map().ToMap(project.tiles.front()),
                      world.Map().ToMap(project.tiles.back()))) return true;
        }
        return false;
    }

    f32 RoadSystem::GroundEffort(const MapData& map, const Coord& tile)
    {
        if (!map.InBounds(tile)) return 1.0f;

        const Tile& t = map.At(tile);
        const TerrainInfo& info = TerrainDatabase::Get().At(t.terrain);
        ConfigManager& config = ConfigManager::Get();

        // A track already laid is the cheapest ground there is: the cuttings are dug, the
        // stumps are out, and the work is widening and metalling rather than road-making.
        if (t.road > 0) return config.Float("roads/existingRoadEffort", 0.18f);

        // Hills and highland are what a road is actually spent on. `moveCost` is the same
        // figure a marching column feels, so the two agree about what hard country is.
        f32 effort = 1.0f + std::max(0.0f, info.moveCost - 1.0f) *
                            config.Float("roads/slopeEffort", 1.8f);
        effort *= 1.0f + t.forest * config.Float("roads/forestEffort", 0.55f);
        return std::clamp(effort, 0.2f, config.Float("roads/maxEffort", 3.2f));
    }

    f32 RoadSystem::BuildCost(const MapData& map, const Coord& tile)
    {
        if (!map.InBounds(tile)) return -1.0f;

        const Tile& t = map.At(tile);
        const TerrainInfo& info = TerrainDatabase::Get().At(t.terrain);

        if (info.water)
        {
            // A crossing is possible but dear, so the route finds the narrows by itself.
            // Over a bridge that is already standing it is merely a stretch of road.
            if (t.bridged) return ConfigManager::Get().Float("roads/existingRoadEffort", 0.18f);
            return ConfigManager::Get().Float("roads/bridgeRouteCost", 9.0f);
        }
        if (!info.passable) return -1.0f;   // nobody cuts a road through a cliff

        // The route follows the same effort the price is worked out from, so a road that
        // looks cheap on the map is cheap in the treasury: A* prefers an existing track and
        // level ground, and goes round a ridge rather than over it.
        return std::max(0.15f, GroundEffort(map, tile));
    }

    // =========================================================================================
    // Planning
    // =========================================================================================

    RoadPlan RoadSystem::Plan(World& world, EntityId fromSettlement, EntityId toSettlement) const
    {
        RoadPlan plan;
        plan.from = fromSettlement;
        plan.to = toSettlement;

        const Settlement* a = world.FindSettlement(fromSettlement);
        const Settlement* b = world.FindSettlement(toSettlement);
        if (!a || !b || a->id == b->id)
        {
            plan.problem = "Немає куди прокладати";
            return plan;
        }

        const MapData& map = world.Map();
        const PathResult path = Pathfinder::Get().FindPath(map, map.ToTile(a->position),
                                                           map.ToTile(b->position), &RoadSystem::BuildCost);
        if (!path.found || path.tiles.size() < 2)
        {
            plan.problem = "Шлях не знайдено";
            return plan;
        }

        ConfigManager& config = ConfigManager::Get();
        const f32 metresPerUnit = config.Float("roads/metresPerUnit", 12.0f);
        const f32 tileMetres = static_cast<f32>(map.TilePixels()) * metresPerUnit;

        // Measure the route, and weigh every metre of it by the ground it crosses: a metre
        // over a ridge is several metres' worth of work, a metre along an existing track is
        // a fraction of one. `metres` stays the honest length, so the figure the player is
        // shown is still the length of his road; `effort` is what he pays for.
        f32 effortMetres = 0.0f;
        for (size_t i = 1; i < path.tiles.size(); ++i)
        {
            const Coord& previous = path.tiles[i - 1];
            const Coord& tile = path.tiles[i];
            const bool diagonal = previous.x != tile.x && previous.y != tile.y;
            const f32 metres = tileMetres * (diagonal ? 1.41421356f : 1.0f);

            plan.metres += metres;
            if (TerrainDatabase::Get().At(map.At(tile).terrain).water && !map.At(tile).bridged)
            {
                plan.bridgeMetres += metres;
            }
            else
            {
                effortMetres += metres * GroundEffort(map, tile);
            }
        }

        // Price it. Both rates are per conceptual metre; a bridge simply costs far more of
        // everything than the same metre of packed earth would.
        plan.cost.money = effortMetres * config.Float("roads/moneyPerMetre", 0.06f) +
                          plan.bridgeMetres * config.Float("roads/bridgeMoneyPerMetre", 1.1f);
        plan.cost.stone = effortMetres * config.Float("roads/stonePerMetre", 0.025f) +
                          plan.bridgeMetres * config.Float("roads/bridgeStonePerMetre", 0.4f);
        plan.cost.wood = effortMetres * config.Float("roads/woodPerMetre", 0.012f) +
                         plan.bridgeMetres * config.Float("roads/bridgeWoodPerMetre", 0.6f);

        // And the same weighting sets the pace: a road over the hills is not only dearer,
        // it takes a season longer.
        plan.days = std::max(1, static_cast<i32>(
            effortMetres / 1000.0f * config.Float("roads/daysPerKilometre", 7.0f) +
            plan.bridgeMetres / 1000.0f * config.Float("roads/bridgeDaysPerKilometre", 90.0f)));

        plan.tiles = path.tiles;
        plan.valid = true;
        return plan;
    }

    // =========================================================================================
    // Building
    // =========================================================================================

    bool RoadSystem::Begin(World& world, EntityId clanId, const RoadPlan& plan)
    {
        if (!plan.valid) return false;

        Clan* clan = world.FindClan(clanId);
        if (!clan || !clan->resources.CanAfford(plan.cost)) return false;

        clan->resources -= plan.cost;

        RoadProject project;
        project.clan = clanId;
        project.tiles = plan.tiles;
        project.daysTotal = static_cast<f32>(std::max(1, plan.days));

        const Settlement* a = world.FindSettlement(plan.from);
        const Settlement* b = world.FindSettlement(plan.to);
        if (a && b) project.label = a->name + " — " + b->name;
        m_projects.push_back(std::move(project));

        const i32 kilometres = static_cast<i32>(plan.metres / 1000.0f + 0.5f);
        world.Log("Розпочато шлях " + (a && b ? a->name + " — " + b->name : std::string("невідомо")) +
                  " (" + std::to_string(kilometres) + " км)", clan->color);
        return true;
    }

    void RoadSystem::Stamp(World& world, const Coord& tile)
    {
        MapData& map = world.MutableMap();
        if (!map.InBounds(tile)) return;

        Tile& target = map.At(tile);
        target.road = std::max(target.road, kRoadGrade);

        // Where the route meets water, the crossing is part of the road: the bridge goes up
        // with it, and from that moment both armies and coverage can pass.
        if (TerrainDatabase::Get().At(target.terrain).water) target.bridged = true;
        m_dirty = true;
    }

    void RoadSystem::Tick(World& world, f32 days)
    {
        if (m_projects.empty()) return;

        for (RoadProject& project : m_projects)
        {
            project.daysDone += days;
            const f32 fraction = Clamp01(project.daysDone / std::max(1.0f, project.daysTotal));
            const size_t target = static_cast<size_t>(fraction * project.tiles.size() + 0.5f);

            bool bridged = false;
            while (project.stamped < target && project.stamped < project.tiles.size())
            {
                const Coord& tile = project.tiles[project.stamped];
                bridged = bridged || TerrainDatabase::Get().At(world.Map().At(tile).terrain).water;
                Stamp(world, tile);
                ++project.stamped;
            }

            // A new bridge changes what coverage can reach, so the borders must be redrawn.
            if (bridged) CoverageSystem::Get().MarkDirty();
        }

        // Retire what is finished, recording the finished line in the world's road list.
        for (auto it = m_projects.begin(); it != m_projects.end(); )
        {
            if (it->stamped < it->tiles.size()) { ++it; continue; }

            RoadSegment segment;
            segment.level = kRoadGrade;
            segment.points.reserve(it->tiles.size());
            for (const Coord& tile : it->tiles) segment.points.push_back(world.Map().ToMap(tile));
            world.Roads().push_back(std::move(segment));

            const Clan* clan = world.FindClan(it->clan);
            world.Log("Шлях " + it->label + " прокладено",
                      clan ? clan->color : Color::FromRGB(0xB9C0C8));
            CoverageSystem::Get().MarkDirty();
            it = m_projects.erase(it);
        }
    }

    Json RoadSystem::ToJson() const
    {
        Json root = Json::MakeObject();
        Json list = Json::MakeArray();
        for (const RoadProject& project : m_projects)
        {
            Json node = Json::MakeObject();
            node["clan"] = static_cast<i64>(project.clan);
            Json tiles = Json::MakeArray();
            for (const Coord& tile : project.tiles)
            {
                tiles.Push(static_cast<i64>(tile.x));
                tiles.Push(static_cast<i64>(tile.y));
            }
            node["tiles"] = tiles;
            node["daysTotal"] = project.daysTotal;
            node["daysDone"] = project.daysDone;
            node["stamped"] = static_cast<i64>(project.stamped);
            node["label"] = project.label;
            list.Push(node);
        }
        root["projects"] = list;
        return root;
    }

    void RoadSystem::FromJson(const Json& root)
    {
        m_projects.clear();
        for (const Json& node : root["projects"].AsArray())
        {
            RoadProject project;
            project.clan = static_cast<EntityId>(node["clan"].AsNumber(0.0));
            const Json& tiles = node["tiles"];
            for (size_t i = 0; i + 1 < tiles.Size(); i += 2)
            {
                project.tiles.push_back({ tiles[i].AsInt(0), tiles[i + 1].AsInt(0) });
            }
            project.daysTotal = node["daysTotal"].AsFloat(1.0f);
            project.daysDone = node["daysDone"].AsFloat(0.0f);
            project.stamped = static_cast<size_t>(std::max(0, node["stamped"].AsInt(0)));
            project.label = node["label"].AsString();
            m_projects.push_back(std::move(project));
        }
        m_dirty = true;
    }

    void RoadSystem::Reset()
    {
        m_projects.clear();
        m_lastMask.clear();
        m_dirty = true;
    }

    void RoadSystem::StampExisting(World& world)
    {
        MapData& map = world.MutableMap();
        if (!map.IsValid()) return;

        for (const RoadSegment& segment : world.Roads())
        {
            for (size_t i = 0; i < segment.points.size(); ++i)
            {
                const Coord tile = map.ToTile(segment.points[i]);
                Stamp(world, tile);

                // Points are stored sparsely, so fill the gap between consecutive ones.
                if (i == 0) continue;
                const Coord previous = map.ToTile(segment.points[i - 1]);
                const i32 steps = std::max(std::abs(tile.x - previous.x), std::abs(tile.y - previous.y));
                for (i32 step = 1; step < steps; ++step)
                {
                    const f32 t = static_cast<f32>(step) / static_cast<f32>(steps);
                    Stamp(world, { previous.x + static_cast<i32>((tile.x - previous.x) * t + 0.5f),
                                   previous.y + static_cast<i32>((tile.y - previous.y) * t + 0.5f) });
                }
            }
        }
        m_dirty = true;
    }

    void RoadSystem::BuildLayer(const MapData& map, std::vector<u8>& mask) const
    {
        // The layer is drawn at the map's own pixel resolution rather than on the coarse
        // simulation grid. A road is a few pixels wide either way, but at this resolution
        // its edge is a pixel rather than a whole tile, which is the whole difference
        // between a track and a smear.
        const u32 width = map.PixelWidth();
        const u32 height = map.PixelHeight();
        mask.assign(static_cast<size_t>(width) * height, 0);
        if (width == 0 || height == 0) return;

        const f32 halfWidth = ConfigManager::Get().Float("render/roads/width", 3.0f);
        const i32 reach = static_cast<i32>(std::ceil(halfWidth)) + 1;

        // Paints a capsule of `halfWidth` pixels around the segment `from`-`to`.
        auto stroke = [&](const Vec2& from, const Vec2& to, u8 value)
        {
            const f32 dx = to.x - from.x;
            const f32 dy = to.y - from.y;
            const f32 lengthSq = dx * dx + dy * dy;

            const i32 minX = std::max(0, static_cast<i32>(std::min(from.x, to.x)) - reach);
            const i32 maxX = std::min(static_cast<i32>(width) - 1, static_cast<i32>(std::max(from.x, to.x)) + reach);
            const i32 minY = std::max(0, static_cast<i32>(std::min(from.y, to.y)) - reach);
            const i32 maxY = std::min(static_cast<i32>(height) - 1, static_cast<i32>(std::max(from.y, to.y)) + reach);

            for (i32 y = minY; y <= maxY; ++y)
            {
                for (i32 x = minX; x <= maxX; ++x)
                {
                    const f32 px = static_cast<f32>(x) + 0.5f;
                    const f32 py = static_cast<f32>(y) + 0.5f;

                    f32 t = 0.0f;
                    if (lengthSq > 0.0001f)
                    {
                        t = Clamp01(((px - from.x) * dx + (py - from.y) * dy) / lengthSq);
                    }
                    const f32 cx = from.x + dx * t;
                    const f32 cy = from.y + dy * t;
                    const f32 distance = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
                    if (distance > halfWidth) continue;

                    u8& texel = mask[static_cast<size_t>(y) * width + static_cast<size_t>(x)];
                    texel = std::max(texel, value);
                }
            }
        };

        // Every road tile joins to its road neighbours; half-segments meeting at the tile
        // centre make junctions and corners knit themselves together with no special case.
        static const i32 kSteps[4][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } };
        for (u32 ty = 0; ty < map.TileHeight(); ++ty)
        {
            for (u32 tx = 0; tx < map.TileWidth(); ++tx)
            {
                const Coord here{ static_cast<i32>(tx), static_cast<i32>(ty) };
                const Tile& tile = map.At(here);
                if (tile.road == 0) continue;

                const Vec2 centre = map.ToMap(here);
                const u8 grade = tile.bridged ? 255u : 128u;
                stroke(centre, centre, grade);

                for (const auto& step : kSteps)
                {
                    const Coord other{ here.x + step[0], here.y + step[1] };
                    if (!map.InBounds(other) || map.At(other).road == 0) continue;

                    const Vec2 to = map.ToMap(other);
                    const Vec2 middle{ (centre.x + to.x) * 0.5f, (centre.y + to.y) * 0.5f };
                    stroke(centre, middle, grade);
                    stroke(middle, to, map.At(other).bridged ? 255u : 128u);
                }
            }
        }
    }

    void RoadSystem::UploadLayer(World& world)
    {
        if (!m_dirty) return;
        m_dirty = false;

        const MapData& map = world.Map();
        if (!map.IsValid()) return;

        std::vector<u8> mask;
        BuildLayer(map, mask);
        if (mask == m_lastMask) return;

        m_lastMask = mask;
        Renderer::Get().SetRoadMask(mask, map.PixelWidth(), map.PixelHeight());
    }
}
