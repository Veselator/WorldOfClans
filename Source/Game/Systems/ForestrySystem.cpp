#include "ForestrySystem.h"
#include "../Factories/EvaluatorFactory.h"
#include "../World/RaceDatabase.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Render/Renderer.h"

#include <algorithm>

namespace woc
{
    void ForestrySystem::Tick(World& world)
    {
        Harvest(world);
        Regrow(world);
        GrowFields(world);
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
                if (info.water || !info.passable) continue;
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
                    if (info.soil < minimumSoil || info.water || !info.passable) continue;

                    // Ploughing pushes back the treeline a little as it goes.
                    const f32 target = Clamp01(demand * (1.0f - static_cast<f32>(dx * dx + dy * dy) /
                                                          static_cast<f32>(span * span + 1)));
                    if (tile.field < target)
                    {
                        tile.field = std::min(target, tile.field + growth);
                        tile.forest = std::max(0.0f, tile.forest - growth * 0.5f);
                    }
                }
            }
        }
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
