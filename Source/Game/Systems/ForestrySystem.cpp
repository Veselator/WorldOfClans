#include "ForestrySystem.h"
#include "../Factories/EvaluatorFactory.h"
#include "../World/RaceDatabase.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Render/Renderer.h"

#include <algorithm>
#include <cmath>

namespace woc
{
    void ForestrySystem::Tick(World& world)
    {
        Harvest(world);
        Regrow(world);
        GrowFields(world);
        m_layersDirty = true;
    }

    // =====================================================================================
    // Planting a wood
    // =====================================================================================

    void ForestrySystem::PriceStand(f32 area, ResourceData& cost, i32& days)
    {
        ConfigManager& config = ConfigManager::Get();

        // A plot is a thousand square map units - about what a gang of peasants can put in
        // over a season. Everything below is quoted per one of those.
        const f32 plots = std::max(0.0f, area) / 1000.0f;

        cost = ResourceData{};
        cost.money = plots * config.Float("forestry/plantMoneyPerPlot", 5.0f);
        cost.food = plots * config.Float("forestry/plantFoodPerPlot", 1.6f);

        const f32 work = plots * config.Float("forestry/plantDaysPerPlot", 6.0f);
        days = static_cast<i32>(std::clamp(work,
                                           config.Float("forestry/plantMinDays", 240.0f),
                                           config.Float("forestry/plantMaxDays", 1400.0f)));
    }

    PlantingPlan ForestrySystem::PlanPlanting(World& world, EntityId clanId, const Vec2& centre) const
    {
        PlantingPlan plan;
        plan.centre = centre;

        const Clan* clan = world.FindClan(clanId);
        if (!clan) { plan.problem = "Немає роду"; return plan; }

        ConfigManager& config = ConfigManager::Get();
        const MapData& map = world.Map();

        plan.radius = config.Float("forestry/plantRadius", 150.0f);

        // Peasants walk to work. A wood is planted within reach of a holding of one's own,
        // not on the far side of the map.
        f32 nearest = 1e9f;
        for (EntityId settlementId : clan->settlements)
        {
            const Settlement* seat = world.FindSettlement(settlementId);
            if (!seat || seat->UnderConstruction()) continue;
            nearest = std::min(nearest, Distance(seat->position, centre));
        }
        if (nearest > config.Float("forestry/plantMaxDistanceFromSeat", 620.0f))
        {
            plan.problem = "Задалеко від ваших поселень";
            return plan;
        }

        // Count what can actually be planted: dry, passable ground that is not already
        // wood. Ploughland can be given back to the trees, but sand and rock cannot.
        const i32 span = std::max(1, static_cast<i32>(plan.radius / map.TilePixels()));
        const Coord origin = map.ToTile(centre);
        const f32 target = config.Float("forestry/plantTargetDensity", 0.85f);

        for (i32 dy = -span; dy <= span; ++dy)
        {
            for (i32 dx = -span; dx <= span; ++dx)
            {
                if (dx * dx + dy * dy > span * span) continue;
                const Coord probe{ origin.x + dx, origin.y + dy };
                if (!map.InBounds(probe)) continue;

                const Tile& tile = map.At(probe);
                const TerrainInfo& info = TerrainDatabase::Get().At(tile.terrain);
                if (!info.arable) continue;                    // only the plains take a wood
                if (tile.field > 0.2f) continue;               // nobody plants trees in the rye
                if (tile.forest >= target - 0.05f) continue;   // already wooded
                ++plan.tiles;
            }
        }

        if (plan.tiles <= 0)
        {
            plan.problem = "Тут нічого садити: ліс береться лише на вільній рівнині — не на ріллі й не там, де він уже стоїть";
            return plan;
        }

        // Only the ground there is actually something to plant on is paid for: a stand
        // half of which is already wood costs half as much and takes half as long.
        const f32 tileArea = static_cast<f32>(map.TilePixels()) * static_cast<f32>(map.TilePixels());
        PriceStand(static_cast<f32>(plan.tiles) * tileArea, plan.cost, plan.days);
        plan.days = std::max(1, plan.days);

        plan.valid = true;
        return plan;
    }

    bool ForestrySystem::BeginPlanting(World& world, EntityId clanId, const PlantingPlan& plan)
    {
        if (!plan.valid) return false;

        Clan* clan = world.FindClan(clanId);
        if (!clan || !clan->resources.CanAfford(plan.cost)) return false;

        clan->resources -= plan.cost;

        Plantation stand;
        stand.clan = clanId;
        stand.centre = plan.centre;
        stand.radius = plan.radius;
        stand.daysTotal = static_cast<f32>(std::max(1, plan.days));
        stand.target = ConfigManager::Get().Float("forestry/plantTargetDensity", 0.85f);

        const Settlement* nearest = world.NearestSettlement(plan.centre, 1e9f, clanId);
        stand.label = nearest ? "Ліс коло " + nearest->name : std::string("Новий ліс");

        world.Log(stand.label + ": селяни беруться саджати ліс (" +
                  std::to_string(plan.days) + " дн.)", clan->color);
        m_plantations.push_back(std::move(stand));
        return true;
    }

    void ForestrySystem::TickPlantations(World& world, f32 days)
    {
        if (m_plantations.empty() || days <= 0.0f) return;

        MapData& map = world.MutableMap();

        for (Plantation& stand : m_plantations)
        {
            const f32 before = stand.Progress();
            stand.daysDone += days;
            const f32 after = stand.Progress();
            if (after <= before) continue;

            // The saplings put on exactly the growth the season is worth. Writing the
            // density straight in, rather than tracking a per-tile plan, keeps a stand
            // cheap to carry and lets the ordinary regrowth rules take it from there.
            const f32 step = (after - before) * stand.target;
            const i32 span = std::max(1, static_cast<i32>(stand.radius / map.TilePixels()));
            const Coord origin = map.ToTile(stand.centre);

            for (i32 dy = -span; dy <= span; ++dy)
            {
                for (i32 dx = -span; dx <= span; ++dx)
                {
                    if (dx * dx + dy * dy > span * span) continue;
                    const Coord probe{ origin.x + dx, origin.y + dy };
                    if (!map.InBounds(probe)) continue;

                    Tile& tile = map.At(probe);
                    const TerrainInfo& info = TerrainDatabase::Get().At(tile.terrain);
                    if (!info.arable) continue;
                    if (tile.field > 0.2f) continue;   // the ploughland was not part of the bargain
                    if (tile.forest >= stand.target) continue;

                    tile.forest = std::min(stand.target, tile.forest + step);
                }
            }
            m_layersDirty = true;
        }

        for (auto it = m_plantations.begin(); it != m_plantations.end(); )
        {
            if (it->daysDone < it->daysTotal) { ++it; continue; }
            world.Log(it->label + ": ліс піднявся", Color::FromRGB(0x4D7A2D));
            it = m_plantations.erase(it);
        }
    }

    Json ForestrySystem::ToJson() const
    {
        Json root = Json::MakeObject();
        Json stands = Json::MakeArray();
        for (const Plantation& stand : m_plantations)
        {
            Json node = Json::MakeObject();
            node["clan"] = static_cast<i64>(stand.clan);
            node["x"] = stand.centre.x;
            node["y"] = stand.centre.y;
            node["radius"] = stand.radius;
            node["daysTotal"] = stand.daysTotal;
            node["daysDone"] = stand.daysDone;
            node["target"] = stand.target;
            node["label"] = stand.label;
            stands.Push(node);
        }
        root["plantations"] = stands;
        return root;
    }

    void ForestrySystem::FromJson(const Json& node)
    {
        m_plantations.clear();
        for (const Json& entry : node["plantations"].AsArray())
        {
            Plantation stand;
            stand.clan = static_cast<EntityId>(entry["clan"].AsInt(0));
            stand.centre = { entry["x"].AsFloat(0.0f), entry["y"].AsFloat(0.0f) };
            stand.radius = entry["radius"].AsFloat(150.0f);
            stand.daysTotal = entry["daysTotal"].AsFloat(1.0f);
            stand.daysDone = entry["daysDone"].AsFloat(0.0f);
            stand.target = entry["target"].AsFloat(0.85f);
            stand.label = entry["label"].AsString();
            m_plantations.push_back(std::move(stand));
        }
        m_layersDirty = true;
    }

    void ForestrySystem::Harvest(World& world)
    {
        MapData& map = world.MutableMap();
        const f32 workRadius = ConfigManager::Get().Float("forestry/fieldMaxRadius", 46.0f) * 2.0f;

        for (auto& [id, settlement] : world.Settlements())
        {
            Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(settlement, map);
            const f32 harvest = evaluator->ForestHarvest();
            if (harvest <= 0.0f) continue;

            // Elves take far less than they give back; that is the point of their affinity.
            const RaceInfo& race = RaceDatabase::Get().Race(settlement.raceId);
            const f32 appetite = harvest / std::max(0.2f, race.forestAffinity);

            const Coord center = map.ToTile(settlement.position);
            const i32 span = std::max(1, static_cast<i32>(workRadius / map.TilePixels()));

            // Spend the month's cutting budget on the densest tiles first.
            f32 remaining = appetite;
            for (i32 ring = 1; ring <= span && remaining > 0.0f; ++ring)
            {
                for (i32 dy = -ring; dy <= ring && remaining > 0.0f; ++dy)
                {
                    for (i32 dx = -ring; dx <= ring; ++dx)
                    {
                        if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                        const Coord probe{ center.x + dx, center.y + dy };
                        if (!map.InBounds(probe)) continue;

                        Tile& tile = map.At(probe);
                        if (tile.forest <= 0.01f) continue;

                        const f32 taken = std::min(tile.forest, 0.08f);
                        tile.forest -= taken;
                        remaining -= taken;
                        if (remaining <= 0.0f) break;
                    }
                }
            }
        }
    }

    void ForestrySystem::Regrow(World& world)
    {
        MapData& map = world.MutableMap();
        ConfigManager& config = ConfigManager::Get();

        const f32 rate = config.Float("forestry/regrowthPerMonth", 0.35f) * 0.02f;
        const f32 threshold = config.Float("forestry/regrowthNeighbourThreshold", 0.25f);

        const i32 width = static_cast<i32>(map.TileWidth());
        const i32 height = static_cast<i32>(map.TileHeight());
        std::vector<Tile>& tiles = map.Tiles();

        // Seed dispersal: a tile only greens up if its neighbours already carry trees,
        // so cleared land stays cleared unless a wood survives beside it.
        std::vector<f32> gain(tiles.size(), 0.0f);
        for (i32 y = 0; y < height; ++y)
        {
            for (i32 x = 0; x < width; ++x)
            {
                const Coord here{ x, y };
                const size_t index = map.Index(here);
                const Tile& tile = tiles[index];

                const TerrainInfo& info = TerrainDatabase::Get().At(tile.terrain);
                // A wood grows on the plains. Sand will not hold it, and the hillsides and
                // the highland are too thin and too cold for anything to spread across.
                if (!info.arable) continue;
                if (tile.field > 0.3f) continue;     // ploughed land is kept clear
                if (tile.forest >= 0.98f) continue;

                f32 neighbourhood = 0.0f;
                for (i32 dy = -1; dy <= 1; ++dy)
                {
                    for (i32 dx = -1; dx <= 1; ++dx)
                    {
                        const Coord probe{ x + dx, y + dy };
                        if (!map.InBounds(probe)) continue;
                        neighbourhood += tiles[map.Index(probe)].forest;
                    }
                }
                neighbourhood /= 9.0f;
                if (neighbourhood < threshold * 0.25f) continue;

                gain[index] = rate * neighbourhood * (info.soil > 0.2f ? 1.0f : 0.4f);
            }
        }

        for (size_t i = 0; i < tiles.size(); ++i)
        {
            if (gain[i] > 0.0f) tiles[i].forest = Clamp01(tiles[i].forest + gain[i]);
        }
    }

    void ForestrySystem::GrowFields(World& world)
    {
        MapData& map = world.MutableMap();
        ConfigManager& config = ConfigManager::Get();

        const f32 growth = config.Float("forestry/fieldGrowthPerMonth", 0.6f) * 0.05f;
        const f32 baseRadius = config.Float("forestry/fieldMaxRadius", 46.0f);
        const f32 minimumSoil = config.Float("forestry/fieldRequiresSoil", 0.35f);
        const f32 clearing = config.Float("forestry/fieldClearingPerMonth", 0.22f);

        for (auto& [id, settlement] : world.Settlements())
        {
            if (settlement.kind == SettlementKind::Castle) continue;

            Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(settlement, map);
            const f32 radius = baseRadius + evaluator->FieldRadiusBonus();

            // Fields scale with the mouths that need feeding.
            const f32 demand = Clamp01(static_cast<f32>(settlement.population) / 900.0f);
            const f32 effectiveRadius = radius * (0.45f + demand * 0.75f);

            const Coord center = map.ToTile(settlement.position);
            const i32 span = std::max(1, static_cast<i32>(effectiveRadius / map.TilePixels()));

            for (i32 dy = -span; dy <= span; ++dy)
            {
                for (i32 dx = -span; dx <= span; ++dx)
                {
                    if (dx * dx + dy * dy > span * span) continue;
                    const Coord probe{ center.x + dx, center.y + dy };
                    if (!map.InBounds(probe)) continue;

                    Tile& tile = map.At(probe);
                    const TerrainInfo& info = TerrainDatabase::Get().At(tile.terrain);
                    // Only the three plain soils are ploughed. Sand, hillside and highland
                    // may feed goats, but no furrow is ever cut in them.
                    if (!info.arable) continue;
                    if (info.soil < minimumSoil || info.water || !info.passable) continue;

                    const f32 target = Clamp01(demand * (1.0f - static_cast<f32>(dx * dx + dy * dy) /
                                                          static_cast<f32>(span * span + 1)));
                    if (tile.field >= target) continue;

                    // No furrow is cut under standing timber. The village clears the wood
                    // first - by itself, over a season or two - and only the bare ground
                    // behind the axemen is ploughed.
                    if (tile.forest > 0.02f)
                    {
                        tile.forest = std::max(0.0f, tile.forest - clearing);
                        continue;
                    }

                    tile.field = std::min(target, tile.field + growth);
                }
            }
        }
    }

    void ForestrySystem::ClearUnarableFields(World& world)
    {
        MapData& map = world.MutableMap();
        if (!map.IsValid()) return;

        const TerrainDatabase& terrain = TerrainDatabase::Get();
        bool changed = false;
        for (Tile& tile : map.Tiles())
        {
            if (tile.field <= 0.0f) continue;
            if (terrain.At(tile.terrain).arable) continue;
            tile.field = 0.0f;
            changed = true;
        }
        if (changed) m_layersDirty = true;
    }

    void ForestrySystem::ClearUnforestable(World& world)
    {
        MapData& map = world.MutableMap();
        if (!map.IsValid()) return;

        const TerrainDatabase& terrain = TerrainDatabase::Get();
        bool changed = false;
        for (Tile& tile : map.Tiles())
        {
            if (tile.forest <= 0.0f) continue;
            if (terrain.At(tile.terrain).arable) continue;
            tile.forest = 0.0f;
            changed = true;
        }
        if (changed) m_layersDirty = true;
    }

    void ForestrySystem::UploadLayers(World& world)
    {
        if (!m_layersDirty) return;
        const MapData& map = world.Map();
        Renderer& renderer = Renderer::Get();
        renderer.SetTreeMask(map.BuildForestMask(), map.TileWidth(), map.TileHeight());
        renderer.SetFieldMask(map.BuildFieldMask(), map.TileWidth(), map.TileHeight());
        m_layersDirty = false;
    }
}
